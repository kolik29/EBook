#pragma once

#include <Arduino.h>
#include <FS.h>
#include <string>
#include <vector>

#include "EpubParserService.h"
#include "HtmlPaginatorService.h"
#include "../drivers/DisplayDriver.h"
#include "../models/EpubModels.h"
#include "../models/HtmlRenderModels.h"
#include "../config/Constants.h"

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
    void releaseMemoryForWifi();

    // Navigate to a global page within the currently open book.
    // Returns false if the book is not open or the page index is out of range.
    bool goToGlobalPage(int globalPage);

    // Returns the epub path of the currently open book (empty if none).
    const String &getCurrentEpubPath() const { return m_epubPath; }

private:
    struct ReadingState {
        int spineIndex = 0;
        int pageIndex = 0;
        int globalPage = 1;
        int totalPages = 0;
        bool valid = false;
    };

    struct RenderedPageCacheEntry {
        int spineIndex = -1;
        int pageIndex = -1;
        HtmlRenderPage page;
    };

    fs::FS &m_fs;
    EpubParserService &m_parser;
    DisplayDriver &m_display;
    HtmlPaginatorService m_paginator;

    String m_epubPath;
    String m_pageIndexPath;
    String m_readerStatePath;
    String m_lastError;

    EpubBookStructure m_structure;

    int m_currentSpineIndex = 0;
    int m_currentPageIndex = 0;
    int m_cachedSpineIndex = -1;

    bool m_opened = false;

    std::string m_currentSpineHtml;
    HtmlRenderPage m_currentRenderedPage;
    bool m_currentRenderedPageReady = false;
    std::vector<RenderedPageCacheEntry> m_renderedPageCache;
    std::vector<int> m_spinePageCounts;
    int m_totalPageCount = 0;
    int m_lastNavigationDirection = 0;

    bool buildPageIndex();
    bool loadPageIndexCache();
    bool savePageIndexCache();
    String getPageIndexPathForEpub(const String &epubPath) const;

    String getReaderStatePathForEpub(const String &epubPath) const;
    bool loadReadingState(ReadingState &state);
    bool saveReadingState();
    bool setPositionByGlobalPage(int globalPage);

    bool loadCurrentSpineItem(int preferredPageIndex = 0);
    bool loadRenderedPage(int pageIndex, HtmlRenderPage &outPage, int &actualPageCount);
    bool findRenderedPageInCache(int spineIndex, int pageIndex, HtmlRenderPage &outPage) const;
    void cacheRenderedPage(int spineIndex, int pageIndex, const HtmlRenderPage &page);
    void pruneRenderedPageCache();
    void prefetchAdjacentPages();
    bool ensureCurrentSpineHtml();
    void clearCurrentSpineCache();
    void clearCurrentRenderedPages();
    void clearCurrentRenderBuffer();
    void renderCurrentPage();

    int getGlobalPageNumber() const;
    int getCurrentSpinePageCount() const;
    String parserErrorOrFallback(const String &fallback) const;
    String describeCurrentSpineItem() const;
    void failWithMessage(const String &title, const String &message);
    void setLastError(const String &message);
    void clearLastError();

    void startBookLaunchLog(const String &epubPath);
    void rotateBookLaunchLogs();
    void logBookLaunchAction(const String &message);
    void logBookLaunchResult(bool ok, const String &message);
};
