#pragma once

#include <Arduino.h>
#include <vector>

struct BookPageInfo {
    uint32_t current = 1;
    uint32_t total = 0;
};

struct BookItem {
    uint32_t id = 0;
    String folder;
    String title;
    String author;
    String cover;
    BookPageInfo page;
};

struct LibraryData {
    String activeBookFolder;
    std::vector<BookItem> books;
};