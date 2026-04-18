#include "LibraryService.h"

#include <Arduino.h>
#include <ArduinoJson.h>

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
    File file = m_fs.open(path, "w");
    if (!file) {
        Serial.print("LIB: failed to open file for write: ");
        Serial.println(path);
        return false;
    }

    size_t written = file.print(content);
    file.close();

    if (written != content.length()) {
        Serial.print("LIB: failed to fully write file: ");
        Serial.println(path);
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