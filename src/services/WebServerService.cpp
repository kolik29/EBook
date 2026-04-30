#include "WebServerService.h"
#include "EpubParserService.h"
#include "HtmlPaginatorService.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <new>
#include <string>
#include <utility>

#include "../config/Constants.h"
#include "../models/EpubModels.h"

static WebServer server(80);

static File gUploadFile;
static String gUploadFolder;
static String gUploadOriginalFilename;
static bool gUploadFailed = false;

static String sanitizeFileName(const String &fileName) {
    String result = fileName;
    result.replace("\\", "_");
    result.replace("/", "_");
    result.replace(" ", "_");
    result.replace(":", "_");
    result.replace("*", "_");
    result.replace("?", "_");
    result.replace("\"", "_");
    result.replace("<", "_");
    result.replace(">", "_");
    result.replace("|", "_");
    return result;
}

static String generateBookFolderId() {
    uint32_t rnd = (uint32_t)esp_random();
    return "bk_" + String((unsigned long)millis()) + "_" + String(rnd, HEX);
}

static String stripExtension(const String &fileName) {
    int dot = fileName.lastIndexOf('.');
    if (dot < 0) {
        return fileName;
    }

    return fileName.substring(0, dot);
}

static void listLittleFsRoot() {
    Serial.println("WEB: listing LittleFS root");

    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("WEB: failed to open LittleFS root");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        Serial.print("WEB: file: ");
        Serial.print(file.name());
        Serial.print(" (");
        Serial.print(file.size());
        Serial.println(" bytes)");
        file = root.openNextFile();
    }
}

static String getContentType(const String &path) {
    String lowerPath = path;
    lowerPath.toLowerCase();

    if (lowerPath.endsWith(".html")) return "text/html; charset=utf-8";
    if (lowerPath.endsWith(".css")) return "text/css; charset=utf-8";
    if (lowerPath.endsWith(".js")) return "application/javascript; charset=utf-8";
    if (lowerPath.endsWith(".json")) return "application/json; charset=utf-8";
    if (lowerPath.endsWith(".svg")) return "image/svg+xml";
    if (lowerPath.endsWith(".png")) return "image/png";
    if (lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg")) return "image/jpeg";
    if (lowerPath.endsWith(".gif")) return "image/gif";
    if (lowerPath.endsWith(".webp")) return "image/webp";
    if (lowerPath.endsWith(".ico")) return "image/x-icon";
    if (lowerPath.endsWith(".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static bool serveFile(const String &requestPath) {
    String path = requestPath;

    if (path == "/") {
        path = "/index.html";
    }

    if (!LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        Serial.print("WEB: failed to open file: ");
        Serial.println(path);
        return false;
    }

    const String contentType = getContentType(path);
    server.streamFile(file, contentType);
    file.close();

    Serial.print("WEB: served ");
    Serial.println(path);

    return true;
}

static bool serveSdFile(const String &requestPath) {
    if (!requestPath.startsWith("/books/cover/")) {
        return false;
    }

    if (!SD.exists(requestPath)) {
        return false;
    }

    File file = SD.open(requestPath, "r");
    if (!file || file.isDirectory()) {
        Serial.print("WEB: failed to open SD file: ");
        Serial.println(requestPath);
        return false;
    }

    const String contentType = getContentType(requestPath);
    server.streamFile(file, contentType);
    file.close();

    Serial.print("WEB: served SD file ");
    Serial.println(requestPath);

    return true;
}

static bool isDigitsOnly(const String &value) {
    if (value.isEmpty()) {
        return false;
    }

    for (size_t i = 0; i < value.length(); i++) {
        if (!isDigit(value[i])) {
            return false;
        }
    }

    return true;
}

static bool extractBookId(const String &uri, int &bookId) {
    if (!uri.startsWith("/books/")) {
        return false;
    }

    String tail = uri.substring(String("/books/").length());
    if (!isDigitsOnly(tail)) {
        return false;
    }

    bookId = tail.toInt();
    return true;
}

static bool extractBookPaginationId(const String &uri, int &bookId) {
    const String suffix = "/pagination";

    if (!uri.startsWith("/books/") || !uri.endsWith(suffix)) {
        return false;
    }

    String middle = uri.substring(String("/books/").length(), uri.length() - suffix.length());
    if (!isDigitsOnly(middle)) {
        return false;
    }

    bookId = middle.toInt();
    return true;
}

static bool extractCurrentPageFromBody(const String &body, uint32_t &currentPage) {
    currentPage = 0;

    if (body.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (!error && doc["currentPage"].is<int>()) {
        const int value = doc["currentPage"].as<int>();
        if (value >= 1) {
            currentPage = static_cast<uint32_t>(value);
            return true;
        }
        return false;
    }

    if (isDigitsOnly(body)) {
        currentPage = static_cast<uint32_t>(body.toInt());
        return currentPage >= 1;
    }

    return false;
}

static bool ensureBookCover(LibraryService *libraryService, BookItem &item) {
    if (!libraryService) {
        return false;
    }

    if (!item.cover.isEmpty() && SD.exists(item.cover)) {
        return false;
    }

    const String epubPath =
        libraryService->getItemsPath() + "/" + item.folder + "/original.epub";

    if (!SD.exists(epubPath)) {
        return false;
    }

    EpubParserService epubParser(SD);
    EpubMetadata metadata;

    if (!epubParser.readMetadata(epubPath, metadata) || !metadata.hasCover) {
        return false;
    }

    String coverPath = "";
    const String coverBasePath = libraryService->getCoverPath() + "/" + item.folder;

    if (!epubParser.extractCoverToFile(epubPath, metadata, coverBasePath, coverPath)) {
        Serial.print("WEB: failed to repair cover for ");
        Serial.println(item.folder);
        return false;
    }

    item.cover = coverPath;

    Serial.print("WEB: repaired cover for ");
    Serial.print(item.folder);
    Serial.print(": ");
    Serial.println(item.cover);

    return true;
}

static void sendBooksFromLibrary(LibraryService *libraryService) {
    if (!libraryService) {
        server.send(500, "application/json; charset=utf-8",
            R"({"ok":false,"message":"library service not set"})");
        return;
    }

    LibraryData library;
    if (!libraryService->loadLibrary(library)) {
        server.send(500, "application/json; charset=utf-8",
            R"({"ok":false,"message":"failed to load library"})");
        return;
    }

    bool libraryChanged = false;

    for (BookItem &item : library.books) {
        libraryChanged = ensureBookCover(libraryService, item) || libraryChanged;
    }

    if (libraryChanged && !libraryService->saveLibrary(library)) {
        Serial.println("WEB: failed to save repaired cover paths");
    }

    JsonDocument doc;
    JsonArray books = doc.to<JsonArray>();

    for (const BookItem &item : library.books) {
        JsonObject bookObj = books.add<JsonObject>();
        bookObj["id"] = item.id;
        bookObj["folder"] = item.folder;
        bookObj["title"] = item.title;
        bookObj["author"] = item.author;
        bookObj["img"] = item.cover;
        bookObj["active"] = (item.folder == library.activeBookFolder);

        JsonObject pageObj = bookObj["page"].to<JsonObject>();
        pageObj["total"] = item.page.total;
        pageObj["current"] = item.page.current;
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json; charset=utf-8", json);

    Serial.print("WEB: /books sent, books=");
    Serial.println(library.books.size());
}

static void sendBookPaginationFromLibrary(LibraryService *libraryService, int bookId) {
    if (!libraryService) {
        server.send(500, "application/json; charset=utf-8", R"({"ok":false,"message":"library service not set"})");
        return;
    }

    LibraryData library;
    if (!libraryService->loadLibrary(library)) {
        server.send(500, "application/json; charset=utf-8", R"({"ok":false,"message":"failed to load library"})");
        return;
    }

    for (const BookItem &item : library.books) {
        if ((int)item.id == bookId) {
            JsonDocument doc;
            doc["id"] = item.id;
            doc["total"] = item.page.total;
            doc["current"] = item.page.current;

            String json;
            serializeJson(doc, json);
            server.send(200, "application/json; charset=utf-8", json);
            return;
        }
    }

    server.send(404, "application/json; charset=utf-8", R"({"ok":false,"message":"book not found"})");
}

static void closeUploadFileIfOpen() {
    if (gUploadFile) {
        gUploadFile.close();
        gUploadFile = File();
    }
}

static void resetUploadState() {
    closeUploadFileIfOpen();
    gUploadFolder = "";
    gUploadOriginalFilename = "";
    gUploadFailed = false;
}

static String buildUploadBookDir(LibraryService *libraryService) {
    if (!libraryService || gUploadFolder.isEmpty()) {
        return "";
    }

    return libraryService->getItemsPath() + "/" + gUploadFolder;
}

static String buildUploadFinalPath(LibraryService *libraryService) {
    const String bookDir = buildUploadBookDir(libraryService);
    if (bookDir.isEmpty()) {
        return "";
    }

    return bookDir + "/original.epub";
}

static String buildUploadTempPath(LibraryService *libraryService) {
    const String finalPath = buildUploadFinalPath(libraryService);
    if (finalPath.isEmpty()) {
        return "";
    }

    return finalPath + ".part";
}

static bool removeSdFileIfExists(const String &path) {
    if (path.isEmpty()) {
        return true;
    }

    if (!SD.exists(path)) {
        return true;
    }

    if (!SD.remove(path)) {
        Serial.print("WEB: failed to remove file: ");
        Serial.println(path);
        return false;
    }

    Serial.print("WEB: removed file: ");
    Serial.println(path);

    return true;
}

static void cleanupUploadArtifacts(LibraryService *libraryService, const String &coverPath = "") {
    closeUploadFileIfOpen();

    if (!coverPath.isEmpty()) {
        removeSdFileIfExists(coverPath);
    }

    if (libraryService && !gUploadFolder.isEmpty()) {
        if (!libraryService->removeBookFolder(gUploadFolder)) {
            Serial.print("WEB: failed to cleanup upload folder: ");
            Serial.println(gUploadFolder);
        }
    }
}

WebServerService::WebServerService() {
}

void WebServerService::setLibraryService(LibraryService *libraryService) {
    m_libraryService = libraryService;
}

bool WebServerService::begin(
    std::function<void()> onDisableWifi,
    std::function<void()> onActivity
) {
    if (m_running) {
        return true;
    }

    m_onDisableWifi = onDisableWifi;
    m_onActivity = onActivity;
    m_disableWifiRequested = false;

    if (!LittleFS.begin(false)) {
        Serial.println("WEB: LittleFS mount failed");
        return false;
    }

    listLittleFsRoot();

    server.on("/", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        if (LittleFS.exists("/index.html")) {
            File file = LittleFS.open("/index.html", "r");
            if (!file || file.isDirectory()) {
                server.send(404, "text/plain", "Not found");
                return;
            }

            server.streamFile(file, "text/html; charset=utf-8");
            file.close();

            Serial.println("WEB: served /index.html");
            return;
        }

        server.send(404, "text/plain", "index.html not found");
    });

    server.on("/disable-wifi", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"wifi disable scheduled"})");
        m_disableWifiRequested = true;

        Serial.println("WEB: disable-wifi requested");
    });

    server.on("/rotate-display", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"display rotation"})");
        Serial.println("WEB: rotate-display requested");
    });

    server.on("/refresh-display", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"display refresh"})");
        Serial.println("WEB: refresh-display requested");
    });

    server.on("/books", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        Serial.println("WEB: get books requested");
        sendBooksFromLibrary(m_libraryService);
        Serial.println("WEB: get books response sent");
    });

    server.on(
        "/books/upload",
        HTTP_POST,
        [this]() {
            if (m_onActivity) {
                m_onActivity();
            }

            closeUploadFileIfOpen();

            if (!m_libraryService) {
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"library service not set"})");
                Serial.println("WEB: upload failed, library service not set");
                resetUploadState();
                return;
            }

            if (gUploadFailed) {
                cleanupUploadArtifacts(m_libraryService);
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"upload failed"})");
                Serial.println("WEB: upload book failed");
                resetUploadState();
                return;
            }

            const String tempPath = buildUploadTempPath(m_libraryService);
            const String epubPath = buildUploadFinalPath(m_libraryService);

            if (tempPath.isEmpty() || epubPath.isEmpty() || !SD.exists(tempPath)) {
                cleanupUploadArtifacts(m_libraryService);
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"temporary upload file not found"})");
                Serial.println("WEB: upload failed, temp file not found");
                resetUploadState();
                return;
            }

            if (SD.exists(epubPath) && !SD.remove(epubPath)) {
                cleanupUploadArtifacts(m_libraryService);
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"failed to replace existing epub file"})");
                Serial.println("WEB: upload failed, cannot remove old epub");
                resetUploadState();
                return;
            }

            if (!SD.rename(tempPath, epubPath)) {
                cleanupUploadArtifacts(m_libraryService);
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"failed to finalize uploaded file"})");
                Serial.println("WEB: upload failed, cannot rename temp epub");
                resetUploadState();
                return;
            }

            BookItem createdBook;

            EpubParserService epubParser(SD);
            EpubMetadata metadata;

            if (!epubParser.readMetadata(epubPath, metadata)) {
                Serial.println("WEB: failed to parse EPUB metadata, using fallback values");
            }

            const String title = metadata.title.isEmpty()
                ? stripExtension(gUploadOriginalFilename)
                : metadata.title;

            const String author = metadata.author.isEmpty()
                ? "Unknown"
                : metadata.author;

            String coverPath = "";

            if (metadata.hasCover) {
                const String coverBasePath = m_libraryService->getCoverPath() + "/" + gUploadFolder;

                if (!epubParser.extractCoverToFile(epubPath, metadata, coverBasePath, coverPath)) {
                    Serial.println("WEB: failed to extract cover, using empty cover path");
                    coverPath = "";
                }
            }

            if (!m_libraryService->addBook(
                    gUploadFolder,
                    title,
                    author,
                    coverPath,
                    createdBook
                )) {
                cleanupUploadArtifacts(m_libraryService, coverPath);
                server.send(500, "application/json; charset=utf-8",
                    R"({"ok":false,"message":"failed to add book to library"})");
                Serial.println("WEB: upload failed, cannot update library");
                resetUploadState();
                return;
            }

            // Build and cache page index at upload time so the device
            // never has to do it on first open.
            {
                EpubBookStructure structure;
                if (epubParser.parseBookStructure(epubPath, structure) && !structure.spine.empty()) {
                    HtmlPaginatorService paginator(
                        Constants::HTML_PAGE_CONTENT_WIDTH_PX,
                        Constants::HTML_PAGE_CONTENT_HEIGHT_PX
                    );

                    std::vector<int> spineCounts;
                    spineCounts.reserve(structure.spine.size());
                    uint32_t totalPages = 0;
                    bool pageIndexComplete = true;

                    for (const EpubSpineItem &spineItem : structure.spine) {
                        std::string spineHtml;
                        if (epubParser.readSpineItemHtml(epubPath, spineItem, spineHtml)) {
                            int cnt = 0;

                            try {
                                cnt = paginator.countPages(spineHtml, spineItem.path);
                            } catch (const std::bad_alloc &) {
                                Serial.print("WEB: page index skipped section, not enough memory: ");
                                Serial.println(spineItem.path);
                                pageIndexComplete = false;
                                cnt = 0;
                            } catch (...) {
                                Serial.print("WEB: page index skipped section, paginator error: ");
                                Serial.println(spineItem.path);
                                pageIndexComplete = false;
                                cnt = 0;
                            }

                            spineCounts.push_back(cnt);
                            totalPages += static_cast<uint32_t>(cnt);
                        } else {
                            pageIndexComplete = false;
                            spineCounts.push_back(0);
                        }

                        std::string emptySpineHtml;
                        std::swap(spineHtml, emptySpineHtml);
                    }

                    if (totalPages > 0 && pageIndexComplete) {
                        const String pageIndexPath =
                            buildUploadBookDir(m_libraryService) + "/page_index.txt";

                        File idxFile = SD.open(pageIndexPath, "w");
                        if (idxFile) {
                            idxFile.print("version=");
                            idxFile.println(Constants::PAGE_INDEX_VERSION);
                            idxFile.print("charsPerLine=");
                            idxFile.println(Constants::HTML_PAGE_CONTENT_WIDTH_PX);
                            idxFile.print("linesPerPage=");
                            idxFile.println(Constants::HTML_PAGE_CONTENT_HEIGHT_PX);
                            idxFile.print("spineCount=");
                            idxFile.println(static_cast<int>(structure.spine.size()));
                            idxFile.print("totalPages=");
                            idxFile.println(totalPages);
                            idxFile.print("counts=");
                            for (int i = 0; i < static_cast<int>(spineCounts.size()); i++) {
                                if (i > 0) idxFile.print(",");
                                idxFile.print(spineCounts[i]);
                            }
                            idxFile.println();
                            idxFile.close();
                            Serial.print("WEB: page index saved, total=");
                            Serial.println(totalPages);
                        } else {
                            Serial.println("WEB: failed to write page index");
                        }

                        m_libraryService->updateBookTotalPages(createdBook.folder, totalPages);
                        createdBook.page.total = totalPages;
                    } else if (!pageIndexComplete) {
                        Serial.println("WEB: page index not saved because one or more sections failed");
                    }
                } else {
                    Serial.println("WEB: could not parse book structure for page index");
                }
            }

            String response = "{";
            response += "\"ok\":true,";
            response += "\"message\":\"upload accepted\",";
            response += "\"id\":" + String(createdBook.id) + ",";
            response += "\"folder\":\"" + createdBook.folder + "\"";
            response += "}";

            server.send(200, "application/json; charset=utf-8", response);
            Serial.println("WEB: upload book completed");

            resetUploadState();
        },
        [this]() {
            HTTPUpload &upload = server.upload();

            if (!m_libraryService) {
                gUploadFailed = true;
                return;
            }

            if (upload.status == UPLOAD_FILE_START) {
                resetUploadState();

                gUploadFailed = false;
                gUploadOriginalFilename = sanitizeFileName(upload.filename);
                gUploadFolder = generateBookFolderId();

                const String bookDir = buildUploadBookDir(m_libraryService);
                const String tempPath = buildUploadTempPath(m_libraryService);

                Serial.print("WEB: upload started: ");
                Serial.println(gUploadOriginalFilename);

                if (bookDir.isEmpty() || tempPath.isEmpty()) {
                    Serial.println("WEB: upload failed, invalid upload paths");
                    gUploadFailed = true;
                    return;
                }

                if (!SD.exists(bookDir)) {
                    if (!SD.mkdir(bookDir)) {
                        Serial.print("WEB: failed to create dir: ");
                        Serial.println(bookDir);
                        gUploadFailed = true;
                        return;
                    }
                }

                if (SD.exists(tempPath) && !SD.remove(tempPath)) {
                    Serial.print("WEB: failed to remove temp upload file: ");
                    Serial.println(tempPath);
                    gUploadFailed = true;
                    return;
                }

                gUploadFile = SD.open(tempPath, "w");
                if (!gUploadFile) {
                    Serial.print("WEB: failed to open upload temp file: ");
                    Serial.println(tempPath);
                    gUploadFailed = true;
                    return;
                }

                Serial.print("WEB: upload temp file path: ");
                Serial.println(tempPath);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (gUploadFailed || !gUploadFile) {
                    return;
                }

                const size_t written = gUploadFile.write(upload.buf, upload.currentSize);
                if (written != upload.currentSize) {
                    Serial.println("WEB: upload write failed");
                    gUploadFailed = true;
                    closeUploadFileIfOpen();
                    return;
                }

                Serial.print("WEB: upload chunk size: ");
                Serial.println(upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (gUploadFile) {
                    gUploadFile.flush();
                    gUploadFile.close();
                    gUploadFile = File();
                }

                Serial.print("WEB: upload finished, total size: ");
                Serial.println(upload.totalSize);
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                closeUploadFileIfOpen();
                gUploadFailed = true;
                Serial.println("WEB: upload aborted");
            }
        }
    );

    server.onNotFound([this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        const String uri = server.uri();
        const HTTPMethod method = server.method();

        Serial.print("WEB: onNotFound uri=");
        Serial.print(uri);
        Serial.print(" method=");
        Serial.println((int)method);

        if (method == HTTP_DELETE) {
            int bookId = 0;
            if (extractBookId(uri, bookId)) {
                Serial.print("WEB: delete book requested, id=");
                Serial.println(bookId);

                if (!m_libraryService) {
                    server.send(500, "application/json; charset=utf-8",
                        R"({"ok":false,"message":"library service not set"})");
                    return;
                }

                if (!m_libraryService->deleteBook(static_cast<uint32_t>(bookId))) {
                    server.send(500, "application/json; charset=utf-8",
                        R"({"ok":false,"message":"failed to delete book"})");
                    return;
                }

                server.send(200, "application/json; charset=utf-8",
                    R"({"ok":true,"message":"book deleted"})");
                return;
            }
        }

        if (method == HTTP_PATCH) {
            int bookId = 0;
            if (extractBookId(uri, bookId)) {
                const String body = server.arg("plain");

                Serial.print("WEB: update current page requested, id=");
                Serial.println(bookId);
                Serial.print("WEB: patch body: ");
                Serial.println(body);

                if (!m_libraryService) {
                    server.send(500, "application/json; charset=utf-8",
                        R"({"ok":false,"message":"library service not set"})");
                    return;
                }

                uint32_t currentPage = 0;
                if (!extractCurrentPageFromBody(body, currentPage)) {
                    server.send(400, "application/json; charset=utf-8",
                        R"({"ok":false,"message":"invalid currentPage"})");
                    return;
                }

                BookItem updatedBook;
                if (!m_libraryService->setActiveBookAndCurrentPage(
                        static_cast<uint32_t>(bookId),
                        currentPage,
                        updatedBook
                    )) {
                    server.send(404, "application/json; charset=utf-8",
                        R"({"ok":false,"message":"book not found or failed to save state"})");
                    return;
                }

                String response = "{";
                response += "\"ok\":true,";
                response += "\"message\":\"book updated\",";
                response += "\"id\":" + String(updatedBook.id) + ",";
                response += "\"folder\":\"" + updatedBook.folder + "\",";
                response += "\"currentPage\":" + String(updatedBook.page.current);
                response += "}";

                server.send(200, "application/json; charset=utf-8", response);

                // Notify the device to navigate to the new page.
                // Done AFTER send() so HTTP response is already queued.
                if (m_onOpenBookPage) {
                    m_onOpenBookPage(updatedBook.folder, updatedBook.page.current);
                }

                return;
            }
        }

        if (method == HTTP_GET) {
            int bookId = 0;
            if (extractBookPaginationId(uri, bookId)) {
                Serial.print("WEB: get pagination requested, id=");
                Serial.println(bookId);

                sendBookPaginationFromLibrary(m_libraryService, bookId);
                return;
            }
        }

        if (serveSdFile(uri)) {
            return;
        }

        if (serveFile(uri)) {
            return;
        }

        if (uri.startsWith("/books") ||
            uri.startsWith("/disable-wifi") ||
            uri.startsWith("/rotate-display") ||
            uri.startsWith("/refresh-display")) {
            server.send(404, "application/json; charset=utf-8", R"({"ok":false,"message":"api route not found"})");
            return;
        }

        if (LittleFS.exists("/index.html")) {
            File file = LittleFS.open("/index.html", "r");
            if (!file || file.isDirectory()) {
                server.send(500, "text/plain", "Failed to open index.html");
                return;
            }
            server.streamFile(file, "text/html; charset=utf-8");
            file.close();

            Serial.print("WEB: fallback -> /index.html for ");
            Serial.println(uri);
            return;
        }

        server.send(404, "text/plain", "Not found");
    });

    server.begin();
    m_running = true;

    Serial.println("WEB: server started");
    return true;
}

void WebServerService::update() {
    if (!m_running) {
        return;
    }

    server.handleClient();

    if (m_disableWifiRequested) {
        m_disableWifiRequested = false;

        if (m_onDisableWifi) {
            m_onDisableWifi();
        }
    }
}

void WebServerService::stop() {
    if (!m_running) {
        return;
    }

    server.stop();
    m_running = false;
    m_disableWifiRequested = false;

    Serial.println("WEB: server stopped");
}

bool WebServerService::isRunning() const {
    return m_running;
}
