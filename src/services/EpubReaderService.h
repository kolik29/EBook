#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

#include "EpubParserService.h"
#include "TextPaginatorService.h"
#include "../drivers/DisplayDriver.h"
#include "../models/EpubModels.h"

class EpubReaderService {
public:
    EpubReaderService(
        fs::FS &fs,
        EpubParserService &parser,
        DisplayDriver &display
    );

    bool openBook(const String &epubPath);
    void showCurrentPage();
    void nextPage();
    void prevPage();

private:
    static constexpr int PAGE_MAX_CHARS_PER_LINE = 40;
    static constexpr int PAGE_MAX_LINES_PER_PAGE = 30;

    struct ReadingState {
        int spineIndex = 0;
        int pageIndex = 0;
        int globalPage = 1;
        int totalPages = 0;
        bool valid = false;
    };

    fs::FS &m_fs;
    EpubParserService &m_parser;
    DisplayDriver &m_display;
    TextPaginatorService m_paginator;

    String m_epubPath;
    String m_pageIndexPath;
    String m_readerStatePath;

    EpubBookStructure m_structure;

    int m_currentSpineIndex = 0;
    int m_currentPageIndex = 0;

    bool m_opened = false;

    std::vector<String> m_currentPages;
    std::vector<int> m_spinePageCounts;
    int m_totalPageCount = 0;

    bool buildPageIndex();
    bool loadPageIndexCache();
    bool savePageIndexCache();
    String getPageIndexPathForEpub(const String &epubPath) const;

    String getReaderStatePathForEpub(const String &epubPath) const;
    bool loadReadingState(ReadingState &state);
    bool saveReadingState();
    bool setPositionByGlobalPage(int globalPage);

    bool loadCurrentSpineItem(int preferredPageIndex = 0);
    void renderCurrentPage();

    int getGlobalPageNumber() const;
};