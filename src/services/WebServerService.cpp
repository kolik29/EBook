#include "WebServerService.h"
#include "EpubParserService.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SD.h>

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

    JsonDocument doc;
    JsonArray books = doc.to<JsonArray>();

    for (const BookItem &item : library.books) {
        JsonObject bookObj = books.add<JsonObject>();
        bookObj["id"]     = item.id;
        bookObj["folder"] = item.folder;
        bookObj["title"]  = item.title;
        bookObj["author"] = item.author;
        bookObj["img"]    = item.cover;
        bookObj["active"] = (item.folder == library.activeBookFolder);

        JsonObject pageObj = bookObj["page"].to<JsonObject>();
        pageObj["total"]   = item.page.total;
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

            if (gUploadFile) {
                gUploadFile.close();
            }

            if (gUploadFailed) {
                server.send(500, "application/json; charset=utf-8", R"({"ok":false,"message":"upload failed"})");
                Serial.println("WEB: upload book failed");

                gUploadFolder = "";
                gUploadOriginalFilename = "";
                gUploadFailed = false;
                return;
            }

            if (!m_libraryService) {
                server.send(500, "application/json; charset=utf-8", R"({"ok":false,"message":"library service not set"})");
                Serial.println("WEB: upload failed, library service not set");

                gUploadFolder = "";
                gUploadOriginalFilename = "";
                return;
            }

            BookItem createdBook;

            const String epubPath = m_libraryService->getItemsPath() + "/" + gUploadFolder + "/original.epub";

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
                server.send(500, "application/json; charset=utf-8", R"({"ok":false,"message":"failed to add book to library"})");
                Serial.println("WEB: upload failed, cannot update library");

                gUploadFolder = "";
                gUploadOriginalFilename = "";
                return;
            }

            String response = "{";
            response += "\"ok\":true,";
            response += "\"message\":\"upload accepted\",";
            response += "\"id\":" + String(createdBook.id) + ",";
            response += "\"folder\":\"" + createdBook.folder + "\"";
            response += "}";

            server.send(200, "application/json; charset=utf-8", response);
            Serial.println("WEB: upload book completed");

            gUploadFolder = "";
            gUploadOriginalFilename = "";
        },
        [this]() {
            HTTPUpload &upload = server.upload();

            if (!m_libraryService) {
                gUploadFailed = true;
                return;
            }

            if (upload.status == UPLOAD_FILE_START) {
                gUploadFailed = false;
                gUploadOriginalFilename = sanitizeFileName(upload.filename);
                gUploadFolder = generateBookFolderId();

                const String bookDir = m_libraryService->getItemsPath() + "/" + gUploadFolder;
                const String filePath = bookDir + "/original.epub";

                Serial.print("WEB: upload started: ");
                Serial.println(gUploadOriginalFilename);

                if (!SD.exists(bookDir)) {
                    if (!SD.mkdir(bookDir)) {
                        Serial.print("WEB: failed to create dir: ");
                        Serial.println(bookDir);
                        gUploadFailed = true;
                        return;
                    }
                }

                gUploadFile = SD.open(filePath, FILE_WRITE);
                if (!gUploadFile) {
                    Serial.print("WEB: failed to open upload file: ");
                    Serial.println(filePath);
                    gUploadFailed = true;
                    return;
                }

                Serial.print("WEB: upload file path: ");
                Serial.println(filePath);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (gUploadFailed || !gUploadFile) {
                    return;
                }

                size_t written = gUploadFile.write(upload.buf, upload.currentSize);
                if (written != upload.currentSize) {
                    Serial.println("WEB: upload write failed");
                    gUploadFailed = true;
                    gUploadFile.close();
                    gUploadFile = File();
                    return;
                }

                Serial.print("WEB: upload chunk size: ");
                Serial.println(upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (gUploadFile) {
                    gUploadFile.close();
                    gUploadFile = File();
                }

                Serial.print("WEB: upload finished, total size: ");
                Serial.println(upload.totalSize);
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                if (gUploadFile) {
                    gUploadFile.close();
                    gUploadFile = File();
                }

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

                server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"book deleted"})");
                return;
            }
        }

        if (method == HTTP_PATCH) {
            int bookId = 0;
            if (extractBookId(uri, bookId)) {
                String body = server.arg("plain");

                Serial.print("WEB: update current page requested, id=");
                Serial.println(bookId);
                Serial.print("WEB: patch body: ");
                Serial.println(body);

                server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"book updated"})");
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