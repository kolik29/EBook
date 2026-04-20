#include "LibraryService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace {
    bool removeFileIfExists(fs::FS &fs, const String &path) {
        if (path.isEmpty()) {
            return true;
        }

        if (!fs.exists(path)) {
            return true;
        }

        if (!fs.remove(path)) {
            Serial.print("LIB: failed to remove file: ");
            Serial.println(path);
            return false;
        }

        Serial.print("LIB: removed file: ");
        Serial.println(path);

        return true;
    }

    bool isSafeBookFolderName(const String &folder) {
        if (folder.isEmpty()) {
            return false;
        }

        if (folder.indexOf('/') >= 0 || folder.indexOf('\\') >= 0) {
            return false;
        }

        if (folder.indexOf("..") >= 0) {
            return false;
        }

        return true;
    }

    bool removeDirRecursive(fs::FS &fs, const String &path) {
        File dir = fs.open(path, "r");
        if (!dir || !dir.isDirectory()) {
            Serial.print("LIB: failed to open dir for removal: ");
            Serial.println(path);
            return false;
        }

        std::vector<String> entries;
        File entry = dir.openNextFile();

        while (entry) {
            entries.push_back(String(entry.path()));
            entry.close();
            entry = dir.openNextFile();
        }

        dir.close();

        for (const String &entryPath : entries) {
            File current = fs.open(entryPath, "r");
            const bool isDir = current && current.isDirectory();
            current.close();

            if (isDir) {
                if (!removeDirRecursive(fs, entryPath)) {
                    return false;
                }
            } else {
                if (!fs.remove(entryPath)) {
                    Serial.print("LIB: failed to remove file in dir: ");
                    Serial.println(entryPath);
                    return false;
                }

                Serial.print("LIB: removed file in dir: ");
                Serial.println(entryPath);
            }
        }

        if (!fs.rmdir(path)) {
            Serial.print("LIB: failed to remove dir: ");
            Serial.println(path);
            return false;
        }

        Serial.print("LIB: removed dir: ");
        Serial.println(path);

        return true;
    }
}

LibraryService::LibraryService(fs::FS &fs)
    : m_fs(fs) {
}

bool LibraryService::begin() {
    Serial.println("LIB: begin");

    if (!ensureBaseFolders()) {
        Serial.println("LIB: failed to ensure base folders");
        return false;
    }

    if (!ensureLibraryFile()) {
        Serial.println("LIB: failed to ensure library.json");
        return false;
    }

    Serial.println("LIB: storage initialized");
    return true;
}

bool LibraryService::loadLibrary(LibraryData &outLibrary) {
    String content;
    if (!readFile(getLibraryPath(), content)) {
        Serial.println("LIB: failed to read library.json");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);

    if (error) {
        Serial.print("LIB: failed to parse library.json: ");
        Serial.println(error.c_str());
        return false;
    }

    outLibrary.activeBookFolder = String((const char *)(doc["activeBookFolder"] | ""));
    outLibrary.books.clear();

    JsonArray books = doc["books"].as<JsonArray>();
    for (JsonObject bookObj : books) {
        BookItem item;
        item.id = bookObj["id"] | 0;
        item.folder = String((const char *)(bookObj["folder"] | ""));
        item.title = String((const char *)(bookObj["title"] | ""));
        item.author = String((const char *)(bookObj["author"] | ""));
        item.cover = String((const char *)(bookObj["cover"] | ""));
        item.page.current = bookObj["page"]["current"] | 1;
        item.page.total = bookObj["page"]["total"] | 0;

        outLibrary.books.push_back(item);
    }

    return true;
}

bool LibraryService::saveLibrary(const LibraryData &library) {
    JsonDocument doc;

    doc["activeBookFolder"] = library.activeBookFolder;

    JsonArray books = doc["books"].to<JsonArray>();

    for (const BookItem &item : library.books) {
        JsonObject bookObj = books.add<JsonObject>();
        bookObj["id"] = item.id;
        bookObj["folder"] = item.folder;
        bookObj["title"] = item.title;
        bookObj["author"] = item.author;
        bookObj["cover"] = item.cover;

        JsonObject pageObj = bookObj["page"].to<JsonObject>();
        pageObj["current"] = item.page.current;
        pageObj["total"] = item.page.total;
    }

    String output;
    serializeJsonPretty(doc, output);

    return writeFile(getLibraryPath(), output);
}

bool LibraryService::addBook(
    const String &folder,
    const String &title,
    const String &author,
    const String &coverPath,
    BookItem &outBook
) {
    if (!isSafeBookFolderName(folder)) {
        Serial.print("LIB: addBook failed, unsafe folder name: ");
        Serial.println(folder);
        return false;
    }

    LibraryData library;
    if (!loadLibrary(library)) {
        Serial.println("LIB: addBook failed, cannot load library");
        return false;
    }

    BookItem item;
    item.id = getNextBookId(library);
    item.folder = folder;
    item.title = title;
    item.author = author;
    item.cover = coverPath;
    item.page.current = 1;
    item.page.total = 0;

    library.books.push_back(item);

    if (library.activeBookFolder.isEmpty()) {
        library.activeBookFolder = folder;
    }

    if (!saveLibrary(library)) {
        Serial.println("LIB: addBook failed, cannot save library");
        return false;
    }

    outBook = item;

    Serial.print("LIB: book added, id=");
    Serial.print(item.id);
    Serial.print(" folder=");
    Serial.println(item.folder);

    return true;
}

bool LibraryService::deleteBook(uint32_t bookId) {
    LibraryData library;
    if (!loadLibrary(library)) {
        Serial.println("LIB: deleteBook failed, cannot load library");
        return false;
    }

    int foundIndex = -1;

    for (size_t i = 0; i < library.books.size(); i++) {
        if (library.books[i].id == bookId) {
            foundIndex = static_cast<int>(i);
            break;
        }
    }

    if (foundIndex < 0) {
        Serial.print("LIB: deleteBook failed, id not found: ");
        Serial.println(bookId);
        return false;
    }

    const BookItem removedBook = library.books[foundIndex];
    library.books.erase(library.books.begin() + foundIndex);

    if (library.activeBookFolder == removedBook.folder) {
        library.activeBookFolder = library.books.empty()
            ? ""
            : library.books.front().folder;
    }

    if (!saveLibrary(library)) {
        Serial.println("LIB: deleteBook failed, cannot save library");
        return false;
    }

    bool cleanupOk = true;

    if (!removedBook.cover.isEmpty()) {
        cleanupOk = removeFileIfExists(m_fs, removedBook.cover) && cleanupOk;
    }

    cleanupOk = removeBookFolder(removedBook.folder) && cleanupOk;

    if (!cleanupOk) {
        Serial.println("LIB: deleteBook completed with cleanup errors");
    }

    Serial.print("LIB: book deleted, id=");
    Serial.print(removedBook.id);
    Serial.print(" folder=");
    Serial.println(removedBook.folder);

    return true;
}

bool LibraryService::setActiveBookAndCurrentPage(uint32_t bookId, uint32_t currentPage, BookItem &outBook) {
    LibraryData library;
    if (!loadLibrary(library)) {
        Serial.println("LIB: setActiveBookAndCurrentPage failed, cannot load library");
        return false;
    }

    BookItem *targetBook = nullptr;

    for (BookItem &item : library.books) {
        if (item.id == bookId) {
            targetBook = &item;
            break;
        }
    }

    if (!targetBook) {
        Serial.print("LIB: setActiveBookAndCurrentPage failed, id not found: ");
        Serial.println(bookId);
        return false;
    }

    if (currentPage < 1) {
        currentPage = 1;
    }

    if (targetBook->page.total > 0 && currentPage > targetBook->page.total) {
        currentPage = targetBook->page.total;
    }

    targetBook->page.current = currentPage;
    library.activeBookFolder = targetBook->folder;

    if (!saveLibrary(library)) {
        Serial.println("LIB: setActiveBookAndCurrentPage failed, cannot save library");
        return false;
    }

    outBook = *targetBook;

    Serial.print("LIB: active book set, id=");
    Serial.print(outBook.id);
    Serial.print(" folder=");
    Serial.print(outBook.folder);
    Serial.print(" currentPage=");
    Serial.println(outBook.page.current);

    return true;
}

bool LibraryService::removeBookFolder(const String &folder) {
    if (!isSafeBookFolderName(folder)) {
        Serial.print("LIB: unsafe folder name: ");
        Serial.println(folder);
        return false;
    }

    const String bookDir = getItemsPath() + "/" + folder;

    if (!m_fs.exists(bookDir)) {
        return true;
    }

    return removeDirRecursive(m_fs, bookDir);
}

bool LibraryService::ensureBaseFolders() {
    const String booksRoot = getBooksRootPath();
    const String itemsPath = getItemsPath();
    const String coverPath = getCoverPath();

    if (!m_fs.exists(booksRoot)) {
        Serial.println("LIB: creating /books");
        if (!m_fs.mkdir(booksRoot)) {
            return false;
        }
    }

    if (!m_fs.exists(itemsPath)) {
        Serial.println("LIB: creating /books/items");
        if (!m_fs.mkdir(itemsPath)) {
            return false;
        }
    }

    if (!m_fs.exists(coverPath)) {
        Serial.println("LIB: creating /books/cover");
        if (!m_fs.mkdir(coverPath)) {
            return false;
        }
    }

    return true;
}

bool LibraryService::ensureLibraryFile() {
    const String libraryPath = getLibraryPath();

    if (m_fs.exists(libraryPath)) {
        Serial.println("LIB: library.json already exists");
        return true;
    }

    Serial.println("LIB: creating empty library.json");

    LibraryData emptyLibrary;
    emptyLibrary.activeBookFolder = "";
    emptyLibrary.books.clear();

    return saveLibrary(emptyLibrary);
}

bool LibraryService::readFile(const String &path, String &outContent) {
    File file = m_fs.open(path, "r");
    if (!file) {
        Serial.print("LIB: failed to open file for read: ");
        Serial.println(path);
        return false;
    }

    outContent = file.readString();
    file.close();
    return true;
}

bool LibraryService::writeFile(const String &path, const String &content) {
    const String tmpPath = path + ".tmp";

    if (m_fs.exists(tmpPath)) {
        m_fs.remove(tmpPath);
    }

    File file = m_fs.open(tmpPath, "w");
    if (!file) {
        Serial.print("LIB: failed to open temp file for write: ");
        Serial.println(tmpPath);
        return false;
    }

    const size_t written = file.print(content);
    file.close();

    if (written != content.length()) {
        Serial.print("LIB: failed to fully write temp file: ");
        Serial.println(tmpPath);
        m_fs.remove(tmpPath);
        return false;
    }

    if (m_fs.exists(path) && !m_fs.remove(path)) {
        Serial.print("LIB: failed to remove old file: ");
        Serial.println(path);
        m_fs.remove(tmpPath);
        return false;
    }

    if (!m_fs.rename(tmpPath, path)) {
        Serial.print("LIB: failed to rename temp file to final: ");
        Serial.println(path);
        m_fs.remove(tmpPath);
        return false;
    }

    Serial.print("LIB: wrote file: ");
    Serial.println(path);

    return true;
}

uint32_t LibraryService::getNextBookId(const LibraryData &library) const {
    uint32_t maxId = 0;

    for (const BookItem &item : library.books) {
        if (item.id > maxId) {
            maxId = item.id;
        }
    }

    return maxId + 1;
}

String LibraryService::getBooksRootPath() const {
    return "/books";
}

String LibraryService::getItemsPath() const {
    return "/books/items";
}

String LibraryService::getCoverPath() const {
    return "/books/cover";
}

String LibraryService::getLibraryPath() const {
    return "/books/library.json";
}