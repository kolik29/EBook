#pragma once

#include <FS.h>
#include "../models/BookModels.h"

class LibraryService {
public:
    explicit LibraryService(fs::FS &fs);

    bool begin();

    bool loadLibrary(LibraryData &outLibrary);
    bool saveLibrary(const LibraryData &library);

    bool addBook(
        const String &folder,
        const String &title,
        const String &author,
        const String &coverPath,
        BookItem &outBook
    );

    bool deleteBook(uint32_t bookId);
    bool removeBookFolder(const String &folder);
    bool setActiveBookAndCurrentPage(uint32_t bookId, uint32_t currentPage, BookItem &outBook);

    String getItemsPath() const;
    String getCoverPath() const;
    String getLibraryPath() const;
    String getBooksRootPath() const;

private:
    fs::FS &m_fs;

    bool ensureBaseFolders();
    bool ensureLibraryFile();

    bool readFile(const String &path, String &outContent);
    bool writeFile(const String &path, const String &content);

    uint32_t getNextBookId(const LibraryData &library) const;
};