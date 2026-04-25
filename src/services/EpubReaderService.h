#pragma once

#include <Arduino.h>
#include <vector>

#include "EpubParserService.h"
#include "TextPaginatorService.h"
#include "../drivers/DisplayDriver.h"
#include "../models/EpubModels.h"

class EpubReaderService {
public:
    EpubReaderService(EpubParserService &parser, DisplayDriver &display);

    bool openBook(const String &epubPath);
    void showCurrentPage();
    void nextPage();
    void prevPage();

private:
    EpubParserService &m_parser;
    DisplayDriver &m_display;
    TextPaginatorService m_paginator;

    String m_epubPath;
    EpubBookStructure m_structure;

    int m_currentSpineIndex = 0;
    int m_currentPageIndex = 0;

    bool m_opened = false;

    std::vector<String> m_currentPages;
    std::vector<int> m_spinePageCounts;
    int m_totalPageCount = 0;

    bool loadCurrentSpineItem();
    void renderCurrentPage();
    bool buildPageIndex();
    int getGlobalPageNumber() const;
};