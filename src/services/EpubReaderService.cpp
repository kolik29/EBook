#include "EpubReaderService.h"
#include "../config/Constants.h"
#include "../utils/DebugLog.h"

#include <new>
#include <utility>

namespace {
    constexpr int READER_LOG_COUNT = 3;
    const char *READER_LOG_DIR = "/logs";
    const char *READER_LOG_PATHS[READER_LOG_COUNT] = {
        "/logs/book_launch_0.log",
        "/logs/book_launch_1.log",
        "/logs/book_launch_2.log"
    };

    uint32_t hashPath(const String &value) {
        uint32_t hash = 2166136261UL;

        for (int i = 0; i < value.length(); i++) {
            hash ^= static_cast<uint8_t>(value[i]);
            hash *= 16777619UL;
        }

        return hash;
    }

    String extensionFromPath(const String &path) {
        int end = path.length();
        const int queryPos = path.indexOf('?');
        const int fragmentPos = path.indexOf('#');

        if (queryPos >= 0 && queryPos < end) {
            end = queryPos;
        }

        if (fragmentPos >= 0 && fragmentPos < end) {
            end = fragmentPos;
        }

        const int slashPos = path.lastIndexOf('/');
        const int dotPos = path.lastIndexOf('.');

        if (dotPos <= slashPos || dotPos < 0 || dotPos >= end) {
            return ".img";
        }

        String ext = path.substring(dotPos, end);
        ext.toLowerCase();

        return ext.length() <= 6 ? ext : ".img";
    }

    String imageCachePathForEpub(const String &epubPath, const String &resourcePath) {
        const int slashPos = epubPath.lastIndexOf('/');
        const String folder = slashPos >= 0
            ? epubPath.substring(0, slashPos)
            : "/";

        return folder
            + "/reader_image_"
            + String(static_cast<unsigned long>(hashPath(resourcePath)), HEX)
            + extensionFromPath(resourcePath);
    }
}

EpubReaderService::EpubReaderService(
    fs::FS &fs,
    EpubParserService &parser,
    DisplayDriver &display
)
    : m_fs(fs),
      m_parser(parser),
      m_display(display),
      m_paginator(
          Constants::HTML_PAGE_CONTENT_WIDTH_PX,
          Constants::HTML_PAGE_CONTENT_HEIGHT_PX) {
}

bool EpubReaderService::openBook(const String &epubPath) {
    clearLastError();
    startBookLaunchLog(epubPath);
    logBookLaunchAction("openBook started");

    Serial.print("READER: open ");
    Serial.println(epubPath);

    m_epubPath = epubPath;
    m_pageIndexPath = getPageIndexPathForEpub(epubPath);
    m_readerStatePath = getReaderStatePathForEpub(epubPath);
    m_currentSpineIndex = 0;
    m_currentPageIndex = 0;
    m_lastNavigationDirection = 0;
    clearCurrentSpineCache();
    clearCurrentRenderedPages();
    m_opened = false;

    if (!m_parser.parseBookStructure(epubPath, m_structure)) {
        Serial.println("READER: failed to parse book structure");
        String message = "Cannot parse book structure. ";
        message += parserErrorOrFallback("No parser details available.");
        failWithMessage(
            "EPUB error",
            message
        );
        logBookLaunchResult(false, m_lastError);
        return false;
    }

    String parsedMessage = "book structure parsed: title=\"";
    parsedMessage += m_structure.title;
    parsedMessage += "\" spineItems=";
    parsedMessage += String(static_cast<int>(m_structure.spine.size()));
    logBookLaunchAction(parsedMessage);

    if (m_structure.spine.empty()) {
        Serial.println("READER: empty spine");
        failWithMessage(
            "EPUB error",
            "Book has no readable chapters. The OPF spine is empty or does not reference HTML/XHTML files."
        );
        logBookLaunchResult(false, m_lastError);
        return false;
    }

    m_opened = true;

    if (!buildPageIndex()) {
        Serial.println("READER: failed to build page index");
        String message = "Cannot build page index. ";
        message += m_lastError.isEmpty()
            ? String("No page index details available.")
            : m_lastError;
        failWithMessage(
            "EPUB error",
            message
        );
        m_opened = false;
        logBookLaunchResult(false, m_lastError);
        return false;
    }

    ReadingState state;

    if (loadReadingState(state)) {
        Serial.println("READER: reading state loaded");
        String stateMessage = "reading state loaded: globalPage=";
        stateMessage += String(state.globalPage);
        stateMessage += " totalPages=";
        stateMessage += String(state.totalPages);
        logBookLaunchAction(stateMessage);

        if (state.totalPages == m_totalPageCount
            && state.spineIndex >= 0
            && state.spineIndex < static_cast<int>(m_structure.spine.size())) {
            m_currentSpineIndex = state.spineIndex;
            m_currentPageIndex = state.pageIndex;

            Serial.print("READER: restore by spine/page: ");
            Serial.print(m_currentSpineIndex);
            Serial.print(" / ");
            Serial.println(m_currentPageIndex);

            const bool ok = loadCurrentSpineItem(m_currentPageIndex);
            logBookLaunchResult(ok, ok ? "restored saved spine/page" : m_lastError);
            if (!ok) {
                m_opened = false;
            }
            return ok;
        }

        Serial.println("READER: total pages changed, restore by global page");
        logBookLaunchAction("reading state total pages changed; restoring by global page");

        if (setPositionByGlobalPage(state.globalPage)) {
            const bool ok = loadCurrentSpineItem(m_currentPageIndex);
            logBookLaunchResult(ok, ok ? "restored saved global page" : m_lastError);
            if (!ok) {
                m_opened = false;
            }
            return ok;
        }
    }

    Serial.println("READER: no valid reading state, open first page");
    logBookLaunchAction("no valid reading state; opening first page");

    m_currentSpineIndex = 0;
    m_currentPageIndex = 0;

    if (!loadCurrentSpineItem(0)) {
        logBookLaunchResult(false, m_lastError);
        m_opened = false;
        return false;
    }

    saveReadingState();
    logBookLaunchResult(true, "opened first page");

    return true;
}

bool EpubReaderService::goToGlobalPage(int globalPage) {
    String requestMessage = "goToGlobalPage requested: ";
    requestMessage += String(globalPage);
    logBookLaunchAction(requestMessage);

    if (!m_opened) {
        Serial.println("READER: goToGlobalPage called but no book is open");
        failWithMessage("Reader error", "Cannot navigate to page: no EPUB book is open.");
        return false;
    }

    if (!setPositionByGlobalPage(globalPage)) {
        Serial.print("READER: goToGlobalPage failed to set position: ");
        Serial.println(globalPage);
        String message = "Cannot navigate to page ";
        message += String(globalPage);
        message += ". Total pages: ";
        message += String(m_totalPageCount);
        message += ".";
        failWithMessage("Page error", message);
        return false;
    }

    m_lastNavigationDirection = 0;

    if (!loadCurrentSpineItem(m_currentPageIndex)) {
        return false;
    }

    saveReadingState();
    String doneMessage = "goToGlobalPage completed: currentPage=";
    doneMessage += String(getGlobalPageNumber());
    logBookLaunchAction(doneMessage);
    return true;
}

bool EpubReaderService::loadCurrentSpineItem(int preferredPageIndex) {
    if (!m_opened) {
        setLastError("Cannot load EPUB section because no book is open.");
        String message = "ERROR: ";
        message += m_lastError;
        logBookLaunchAction(message);
        return false;
    }

    if (m_currentSpineIndex < 0
        || m_currentSpineIndex >= static_cast<int>(m_structure.spine.size())) {
        String message = "Invalid spine index ";
        message += String(m_currentSpineIndex);
        message += " for spine size ";
        message += String(static_cast<int>(m_structure.spine.size()));
        message += ".";
        failWithMessage("Read error", message);
        return false;
    }

    const String spineDescription = describeCurrentSpineItem();
    String readMessage = "reading spine item: ";
    readMessage += spineDescription;
    logBookLaunchAction(readMessage);

    if (!ensureCurrentSpineHtml()) {
        return false;
    }

    const int knownPageCount = getCurrentSpinePageCount();

    if (preferredPageIndex < 0) {
        m_currentPageIndex = knownPageCount - 1;
    } else if (preferredPageIndex >= knownPageCount) {
        m_currentPageIndex = knownPageCount - 1;
    } else {
        m_currentPageIndex = preferredPageIndex;
    }

    if (m_currentPageIndex < 0) {
        m_currentPageIndex = 0;
    }

    int actualPageCount = 0;
    const unsigned long prepareStartedAt = millis();

    if (!loadRenderedPage(m_currentPageIndex, m_currentRenderedPage, actualPageCount)) {
        if (m_lastError.isEmpty()) {
            String message = "HTML paginator failed on EPUB section ";
            message += spineDescription;
            message += ".";
            failWithMessage("Read error", message);
        }

        return false;
    }

    m_currentRenderedPageReady = true;

    Serial.print("READER: page prepare ms = ");
    Serial.println(millis() - prepareStartedAt);

    if (m_currentSpineIndex >= 0
        && m_currentSpineIndex < static_cast<int>(m_spinePageCounts.size())
        && actualPageCount > 0) {
        m_spinePageCounts[m_currentSpineIndex] = actualPageCount;
    }

    Serial.print("READER: spine ");
    Serial.print(m_currentSpineIndex + 1);
    Serial.print(" / ");
    Serial.println(static_cast<int>(m_structure.spine.size()));

    Serial.print("READER: pages in section = ");
    Serial.println(actualPageCount);
    String paginatedMessage = "spine item paginated: pages=";
    paginatedMessage += String(actualPageCount);
    paginatedMessage += " selectedPageIndex=";
    paginatedMessage += String(m_currentPageIndex);
    logBookLaunchAction(paginatedMessage);

    renderCurrentPage();

    return true;
}

bool EpubReaderService::loadRenderedPage(
    int pageIndex,
    HtmlRenderPage &outPage,
    int &actualPageCount
) {
    outPage = HtmlRenderPage();
    actualPageCount = getCurrentSpinePageCount();
    clearCurrentRenderBuffer();

    if (findRenderedPageInCache(m_currentSpineIndex, pageIndex, outPage)) {
        Serial.print("READER: rendered page cache hit: ");
        Serial.println(pageIndex);

        String message = "rendered page cache hit: pageIndex=";
        message += String(pageIndex);
        logBookLaunchAction(message);
        return true;
    }

    HtmlRenderPage renderedPage;

    try {
        const bool ok = m_paginator.paginatePage(
            m_currentSpineHtml,
            m_structure.spine[m_currentSpineIndex].path,
            pageIndex,
            actualPageCount,
            renderedPage,
            actualPageCount
        );

        if (!ok) {
            String message = "HTML paginator could not render page ";
            message += String(pageIndex + 1);
            message += " in EPUB section ";
            message += describeCurrentSpineItem();
            message += ".";
            failWithMessage("Read error", message);
            return false;
        }
    } catch (const std::bad_alloc &) {
        String message = "Not enough memory to paginate EPUB section ";
        message += describeCurrentSpineItem();
        message += ".";
        failWithMessage("Read error", message);
        return false;
    } catch (...) {
        String message = "HTML paginator failed on EPUB section ";
        message += describeCurrentSpineItem();
        message += ".";
        failWithMessage("Read error", message);
        return false;
    }

    cacheRenderedPage(m_currentSpineIndex, pageIndex, renderedPage);
    outPage = std::move(renderedPage);
    return true;
}

bool EpubReaderService::ensureCurrentSpineHtml() {
    if (m_cachedSpineIndex == m_currentSpineIndex && !m_currentSpineHtml.empty()) {
        Serial.println("READER: using cached spine html");
        logBookLaunchAction("using cached spine html");
        return true;
    }

    clearCurrentRenderedPages();
    clearCurrentSpineCache();

    if (!m_parser.readSpineItemHtml(
        m_epubPath,
        m_structure.spine[m_currentSpineIndex],
        m_currentSpineHtml
    )) {
        Serial.println("READER: failed to read spine item");
        Serial.print("READER: parser error: ");
        Serial.println(parserErrorOrFallback("No parser details available."));

        String message = "Cannot read EPUB section ";
        message += describeCurrentSpineItem();
        message += ". ";
        message += parserErrorOrFallback("No parser details available.");
        failWithMessage(
            "Read error",
            message
        );
        clearCurrentSpineCache();
        return false;
    }

    m_cachedSpineIndex = m_currentSpineIndex;

    Serial.print("READER: cached spine html bytes = ");
    Serial.println(static_cast<unsigned long>(m_currentSpineHtml.length()));

    String message = "cached spine html bytes=";
    message += String(static_cast<unsigned long>(m_currentSpineHtml.length()));
    logBookLaunchAction(message);

    return true;
}

void EpubReaderService::clearCurrentSpineCache() {
    std::string emptyHtml;
    std::swap(m_currentSpineHtml, emptyHtml);
    m_cachedSpineIndex = -1;
}

void EpubReaderService::releaseMemoryForWifi() {
    if (!m_opened) {
        return;
    }

    clearCurrentSpineCache();
    clearCurrentRenderedPages();
    Serial.println("READER: released cached page data for WiFi");
}

void EpubReaderService::showCurrentPage() {
    if (m_opened && !m_currentRenderedPageReady) {
        loadCurrentSpineItem(m_currentPageIndex);
        return;
    }

    renderCurrentPage();
}

void EpubReaderService::renderCurrentPage() {
    if (!m_opened || !m_currentRenderedPageReady) {
        return;
    }

    if (m_currentPageIndex < 0) {
        m_currentPageIndex = 0;
    }

    String title = m_structure.title.length() > 0
        ? m_structure.title
        : "EPUB book";

    const int globalPage = getGlobalPageNumber();

    Serial.print("READER: render global page ");
    Serial.print(globalPage);
    Serial.print(" / ");
    Serial.println(m_totalPageCount);
    String renderMessage = "render page: global=";
    renderMessage += String(globalPage);
    renderMessage += "/";
    renderMessage += String(m_totalPageCount);
    renderMessage += " spine=";
    renderMessage += String(m_currentSpineIndex + 1);
    renderMessage += " pageIndex=";
    renderMessage += String(m_currentPageIndex);
    logBookLaunchAction(renderMessage);

    HtmlImageLoader imageLoader = [this](
        const String &path,
        uint8_t *&outData,
        size_t &outSize,
        String &outFilePath
    ) {
        outData = nullptr;
        outSize = 0;
        outFilePath = "";

        const String cachePath = imageCachePathForEpub(m_epubPath, path);

        if (m_fs.exists(cachePath)) {
            outFilePath = cachePath;
            return true;
        }

        if (m_parser.extractResourceToFile(
            m_epubPath,
            path,
            5 * 1024 * 1024,
            cachePath
        )) {
            outFilePath = cachePath;
            return true;
        }

        if (m_parser.extractResourceData(
            m_epubPath,
            path,
            5 * 1024 * 1024,
            outData,
            outSize
        )) {
            return true;
        }

        String message = "WARN: image load failed: ";
        message += path;
        message += " - ";
        message += parserErrorOrFallback("No parser details available.");
        logBookLaunchAction(message);

        return false;
    };

    m_display.showHtmlPage(
        title,
        m_currentRenderedPage,
        globalPage,
        m_totalPageCount,
        imageLoader
    );

    clearCurrentRenderBuffer();
    prefetchAdjacentPages();
}

void EpubReaderService::clearCurrentRenderedPages() {
    clearCurrentRenderBuffer();

    std::vector<RenderedPageCacheEntry> emptyCache;
    std::swap(m_renderedPageCache, emptyCache);
}

void EpubReaderService::clearCurrentRenderBuffer() {
    HtmlRenderPage emptyPage;
    std::swap(m_currentRenderedPage, emptyPage);
    m_currentRenderedPageReady = false;
}

bool EpubReaderService::findRenderedPageInCache(
    int spineIndex,
    int pageIndex,
    HtmlRenderPage &outPage
) const {
    for (const RenderedPageCacheEntry &entry : m_renderedPageCache) {
        if (entry.spineIndex == spineIndex && entry.pageIndex == pageIndex) {
            outPage = entry.page;
            return true;
        }
    }

    return false;
}

void EpubReaderService::cacheRenderedPage(
    int spineIndex,
    int pageIndex,
    const HtmlRenderPage &page
) {
    try {
        for (RenderedPageCacheEntry &entry : m_renderedPageCache) {
            if (entry.spineIndex == spineIndex && entry.pageIndex == pageIndex) {
                entry.page = page;
                pruneRenderedPageCache();
                return;
            }
        }

        RenderedPageCacheEntry entry;
        entry.spineIndex = spineIndex;
        entry.pageIndex = pageIndex;
        entry.page = page;
        m_renderedPageCache.push_back(std::move(entry));
        pruneRenderedPageCache();
    } catch (...) {
        Serial.println("READER: rendered page cache cleared after allocation failure");
        std::vector<RenderedPageCacheEntry> emptyCache;
        std::swap(m_renderedPageCache, emptyCache);
    }
}

void EpubReaderService::pruneRenderedPageCache() {
    for (auto it = m_renderedPageCache.begin(); it != m_renderedPageCache.end(); ) {
        int distance = it->pageIndex - m_currentPageIndex;
        if (distance < 0) {
            distance = -distance;
        }

        if (it->spineIndex != m_currentSpineIndex || distance > 1) {
            it = m_renderedPageCache.erase(it);
        } else {
            ++it;
        }
    }
}

void EpubReaderService::prefetchAdjacentPages() {
    if (!m_opened || m_currentSpineHtml.empty()) {
        return;
    }

    const int pageCount = getCurrentSpinePageCount();
    if (pageCount <= 1) {
        return;
    }

    const int firstCandidate = m_lastNavigationDirection < 0
        ? m_currentPageIndex - 1
        : m_currentPageIndex + 1;
    const int secondCandidate = m_lastNavigationDirection < 0
        ? m_currentPageIndex + 1
        : m_currentPageIndex - 1;
    const int candidates[2] = { firstCandidate, secondCandidate };

    for (int candidate : candidates) {
        if (candidate < 0 || candidate >= pageCount) {
            continue;
        }

        HtmlRenderPage cachedPage;
        if (findRenderedPageInCache(m_currentSpineIndex, candidate, cachedPage)) {
            continue;
        }

        HtmlRenderPage page;
        int actualPageCount = pageCount;

        try {
            if (m_paginator.paginatePage(
                m_currentSpineHtml,
                m_structure.spine[m_currentSpineIndex].path,
                candidate,
                pageCount,
                page,
                actualPageCount
            )) {
                cacheRenderedPage(m_currentSpineIndex, candidate, page);

                Serial.print("READER: prefetched rendered page: ");
                Serial.println(candidate);
            }
        } catch (...) {
            Serial.print("READER: prefetch skipped after paginator failure: ");
            Serial.println(candidate);
        }

        break;
    }
}

void EpubReaderService::nextPage() {
    if (!m_opened) {
        logBookLaunchAction("nextPage ignored: no book is open");
        return;
    }

    if (m_currentPageIndex + 1 < getCurrentSpinePageCount()) {
        m_lastNavigationDirection = 1;

        if (!loadCurrentSpineItem(m_currentPageIndex + 1)) {
            return;
        }

        saveReadingState();
        String message = "nextPage completed: globalPage=";
        message += String(getGlobalPageNumber());
        logBookLaunchAction(message);
        return;
    }

    if (m_currentSpineIndex + 1 < static_cast<int>(m_structure.spine.size())) {
        m_currentSpineIndex++;
        m_lastNavigationDirection = 1;

        if (loadCurrentSpineItem(0)) {
            saveReadingState();
            String message = "nextPage loaded next spine: globalPage=";
            message += String(getGlobalPageNumber());
            logBookLaunchAction(message);
        }

        return;
    }

    Serial.println("READER: already at last page");
    logBookLaunchAction("nextPage ignored: already at last page");
}

void EpubReaderService::prevPage() {
    if (!m_opened) {
        logBookLaunchAction("prevPage ignored: no book is open");
        return;
    }

    if (m_currentPageIndex > 0) {
        m_lastNavigationDirection = -1;

        if (!loadCurrentSpineItem(m_currentPageIndex - 1)) {
            return;
        }

        saveReadingState();
        String message = "prevPage completed: globalPage=";
        message += String(getGlobalPageNumber());
        logBookLaunchAction(message);
        return;
    }

    if (m_currentSpineIndex > 0) {
        m_currentSpineIndex--;
        m_lastNavigationDirection = -1;

        if (loadCurrentSpineItem(-1)) {
            saveReadingState();
            String message = "prevPage loaded previous spine: globalPage=";
            message += String(getGlobalPageNumber());
            logBookLaunchAction(message);
        }

        return;
    }

    Serial.println("READER: already at first page");
    logBookLaunchAction("prevPage ignored: already at first page");
}

bool EpubReaderService::buildPageIndex() {
    m_spinePageCounts.clear();
    m_totalPageCount = 0;
    clearLastError();

    if (loadPageIndexCache()) {
        Serial.println("READER: page index loaded from cache");

        Serial.print("READER: total pages = ");
        Serial.println(m_totalPageCount);
        String message = "page index loaded from cache: totalPages=";
        message += String(m_totalPageCount);
        logBookLaunchAction(message);

        return true;
    }

    Serial.println("READER: page index cache not found or invalid");
    Serial.println("READER: building page index");
    logBookLaunchAction("page index cache unavailable; rebuilding");

    int failedSections = 0;
    String firstFailure = "";

    for (int i = 0; i < static_cast<int>(m_structure.spine.size()); i++) {
        std::string html;

        if (!m_parser.readSpineItemHtml(
            m_epubPath,
            m_structure.spine[i],
            html
        )) {
            Serial.print("READER: failed to read spine for page index: ");
            Serial.println(i);

            String failure = "section ";
            failure += String(i + 1);
            failure += " (";
            failure += m_structure.spine[i].path;
            failure += "): ";
            failure += parserErrorOrFallback("No parser details available.");

            if (firstFailure.isEmpty()) {
                firstFailure = failure;
            }

            failedSections++;
            String message = "WARN: page index skipped ";
            message += failure;
            logBookLaunchAction(message);
            m_spinePageCounts.push_back(0);
            continue;
        }

        int pageCount = 0;

        try {
            pageCount = m_paginator.countPages(
                html,
                m_structure.spine[i].path
            );
        } catch (const std::bad_alloc &) {
            String failure = "section ";
            failure += String(i + 1);
            failure += " (";
            failure += m_structure.spine[i].path;
            failure += "): not enough memory to paginate";

            if (firstFailure.isEmpty()) {
                firstFailure = failure;
            }

            failedSections++;
            String message = "WARN: page index skipped ";
            message += failure;
            logBookLaunchAction(message);
            m_spinePageCounts.push_back(0);
            std::string emptyHtml;
            std::swap(html, emptyHtml);
            continue;
        } catch (...) {
            String failure = "section ";
            failure += String(i + 1);
            failure += " (";
            failure += m_structure.spine[i].path;
            failure += "): HTML paginator failed";

            if (firstFailure.isEmpty()) {
                firstFailure = failure;
            }

            failedSections++;
            String message = "WARN: page index skipped ";
            message += failure;
            logBookLaunchAction(message);
            m_spinePageCounts.push_back(0);
            std::string emptyHtml;
            std::swap(html, emptyHtml);
            continue;
        }

        m_spinePageCounts.push_back(pageCount);
        m_totalPageCount += pageCount;

        Serial.print("READER: section ");
        Serial.print(i + 1);
        Serial.print(" pages = ");
        Serial.println(pageCount);
        String message = "page index section ";
        message += String(i + 1);
        message += "/";
        message += String(static_cast<int>(m_structure.spine.size()));
        message += " pages=";
        message += String(pageCount);
        logBookLaunchAction(message);
        std::string emptyHtml;
        std::swap(html, emptyHtml);
    }

    Serial.print("READER: total pages = ");
    Serial.println(m_totalPageCount);
    String builtMessage = "page index built: totalPages=";
    builtMessage += String(m_totalPageCount);
    logBookLaunchAction(builtMessage);

    if (m_totalPageCount <= 0) {
        if (firstFailure.isEmpty()) {
            setLastError("All sections produced zero pages.");
        } else {
            String message = "No sections could be indexed. First failure: ";
            message += firstFailure;
            setLastError(message);
        }
        return false;
    }

    if (failedSections > 0) {
        String message = "WARN: page index built with skipped sections=";
        message += String(failedSections);
        logBookLaunchAction(message);
    }

    if (!savePageIndexCache()) {
        logBookLaunchAction("WARN: page index was built but could not be saved to cache");
    }

    return true;
}

int EpubReaderService::getGlobalPageNumber() const {
    int page = 1;

    for (int i = 0; i < m_currentSpineIndex; i++) {
        if (i >= 0 && i < static_cast<int>(m_spinePageCounts.size())) {
            page += m_spinePageCounts[i];
        }
    }

    page += m_currentPageIndex;

    return page;
}

int EpubReaderService::getCurrentSpinePageCount() const {
    if (m_currentSpineIndex >= 0
        && m_currentSpineIndex < static_cast<int>(m_spinePageCounts.size())
        && m_spinePageCounts[m_currentSpineIndex] > 0) {
        return m_spinePageCounts[m_currentSpineIndex];
    }

    return 1;
}

String EpubReaderService::getPageIndexPathForEpub(const String &epubPath) const {
    const int slashPos = epubPath.lastIndexOf('/');

    if (slashPos < 0) {
        return "/page_index.txt";
    }

    return epubPath.substring(0, slashPos + 1) + "page_index.txt";
}

bool EpubReaderService::loadPageIndexCache() {
    if (m_pageIndexPath.isEmpty()) {
        return false;
    }

    if (!m_fs.exists(m_pageIndexPath)) {
        Serial.print("READER: page index file not found: ");
        Serial.println(m_pageIndexPath);
        String message = "page index cache missing: ";
        message += m_pageIndexPath;
        logBookLaunchAction(message);
        return false;
    }

    File file = m_fs.open(m_pageIndexPath, "r");

    if (!file || file.isDirectory()) {
        Serial.print("READER: failed to open page index file: ");
        Serial.println(m_pageIndexPath);
        String message = "page index cache cannot be opened: ";
        message += m_pageIndexPath;
        logBookLaunchAction(message);
        return false;
    }

    int version = 0;
    int charsPerLine = 0;
    int linesPerPage = 0;
    int spineCount = 0;
    int totalPages = 0;
    String countsLine = "";

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.isEmpty()) {
            continue;
        }

        const int separatorPos = line.indexOf('=');

        if (separatorPos < 0) {
            continue;
        }

        String key = line.substring(0, separatorPos);
        String value = line.substring(separatorPos + 1);

        key.trim();
        value.trim();

        if (key == "version") {
            version = value.toInt();
        } else if (key == "charsPerLine") {
            charsPerLine = value.toInt();
        } else if (key == "linesPerPage") {
            linesPerPage = value.toInt();
        } else if (key == "spineCount") {
            spineCount = value.toInt();
        } else if (key == "totalPages") {
            totalPages = value.toInt();
        } else if (key == "counts") {
            countsLine = value;
        }
    }

    file.close();

    if (version != Constants::PAGE_INDEX_VERSION) {
        Serial.println("READER: page index version mismatch");
        logBookLaunchAction("page index cache ignored: version mismatch");
        return false;
    }

    if (charsPerLine != Constants::HTML_PAGE_CONTENT_WIDTH_PX
        || linesPerPage != Constants::HTML_PAGE_CONTENT_HEIGHT_PX) {
        Serial.println("READER: page index layout settings changed");
        logBookLaunchAction("page index cache ignored: layout settings changed");
        return false;
    }

    if (spineCount != static_cast<int>(m_structure.spine.size())) {
        Serial.println("READER: page index spine count mismatch");
        logBookLaunchAction("page index cache ignored: spine count mismatch");
        return false;
    }

    if (totalPages <= 0 || countsLine.isEmpty()) {
        Serial.println("READER: page index is empty or broken");
        logBookLaunchAction("page index cache ignored: empty or broken");
        return false;
    }

    std::vector<int> counts;
    int start = 0;

    while (start <= countsLine.length()) {
        int commaPos = countsLine.indexOf(',', start);

        if (commaPos < 0) {
            commaPos = countsLine.length();
        }

        String item = countsLine.substring(start, commaPos);
        item.trim();

        if (!item.isEmpty()) {
            counts.push_back(item.toInt());
        }

        if (commaPos >= countsLine.length()) {
            break;
        }

        start = commaPos + 1;
    }

    if (static_cast<int>(counts.size()) != spineCount) {
        Serial.println("READER: page index counts size mismatch");
        logBookLaunchAction("page index cache ignored: counts size mismatch");
        return false;
    }

    int calculatedTotal = 0;

    for (int count : counts) {
        if (count < 0) {
            Serial.println("READER: page index has invalid page count");
            logBookLaunchAction("page index cache ignored: invalid page count");
            return false;
        }

        calculatedTotal += count;
    }

    if (calculatedTotal != totalPages) {
        Serial.println("READER: page index total pages mismatch");
        logBookLaunchAction("page index cache ignored: total pages mismatch");
        return false;
    }

    m_spinePageCounts = counts;
    m_totalPageCount = totalPages;

    Serial.print("READER: page index loaded: ");
    Serial.println(m_pageIndexPath);

    return true;
}

bool EpubReaderService::savePageIndexCache() {
    if (m_pageIndexPath.isEmpty()) {
        return false;
    }

    if (m_totalPageCount <= 0 || m_spinePageCounts.empty()) {
        return false;
    }

    File file = m_fs.open(m_pageIndexPath, "w");

    if (!file) {
        Serial.print("READER: failed to write page index file: ");
        Serial.println(m_pageIndexPath);
        String message = "page index cache write failed: ";
        message += m_pageIndexPath;
        logBookLaunchAction(message);
        return false;
    }

    file.print("version=");
    file.println(Constants::PAGE_INDEX_VERSION);

    file.print("charsPerLine=");
    file.println(Constants::HTML_PAGE_CONTENT_WIDTH_PX);

    file.print("linesPerPage=");
    file.println(Constants::HTML_PAGE_CONTENT_HEIGHT_PX);

    file.print("spineCount=");
    file.println(static_cast<int>(m_structure.spine.size()));

    file.print("totalPages=");
    file.println(m_totalPageCount);

    file.print("counts=");

    for (int i = 0; i < static_cast<int>(m_spinePageCounts.size()); i++) {
        if (i > 0) {
            file.print(",");
        }

        file.print(m_spinePageCounts[i]);
    }

    file.println();
    file.close();

    Serial.print("READER: page index saved: ");
    Serial.println(m_pageIndexPath);
    String message = "page index cache saved: ";
    message += m_pageIndexPath;
    logBookLaunchAction(message);

    return true;
}

String EpubReaderService::getReaderStatePathForEpub(const String &epubPath) const {
    const int slashPos = epubPath.lastIndexOf('/');

    if (slashPos < 0) {
        return "/reader_state.txt";
    }

    return epubPath.substring(0, slashPos + 1) + "reader_state.txt";
}

bool EpubReaderService::loadReadingState(ReadingState &state) {
    state = ReadingState();

    if (m_readerStatePath.isEmpty()) {
        return false;
    }

    if (!m_fs.exists(m_readerStatePath)) {
        Serial.print("READER: state file not found: ");
        Serial.println(m_readerStatePath);
        String message = "reading state missing: ";
        message += m_readerStatePath;
        logBookLaunchAction(message);
        return false;
    }

    File file = m_fs.open(m_readerStatePath, "r");

    if (!file || file.isDirectory()) {
        Serial.print("READER: failed to open state file: ");
        Serial.println(m_readerStatePath);
        String message = "reading state cannot be opened: ";
        message += m_readerStatePath;
        logBookLaunchAction(message);
        return false;
    }

    String stateEpubPath = "";
    int stateVersion = 0;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.isEmpty()) {
            continue;
        }

        const int separatorPos = line.indexOf('=');

        if (separatorPos < 0) {
            continue;
        }

        String key = line.substring(0, separatorPos);
        String value = line.substring(separatorPos + 1);

        key.trim();
        value.trim();

        if (key == "version") {
            stateVersion = value.toInt();
        } else if (key == "epubPath") {
            stateEpubPath = value;
        } else if (key == "spineIndex") {
            state.spineIndex = value.toInt();
        } else if (key == "pageIndex") {
            state.pageIndex = value.toInt();
        } else if (key == "globalPage") {
            state.globalPage = value.toInt();
        } else if (key == "totalPages") {
            state.totalPages = value.toInt();
        }
    }

    file.close();

    if (stateVersion != Constants::PAGE_INDEX_VERSION) {
        Serial.println("READER: state file version mismatch");
        logBookLaunchAction("reading state ignored: version mismatch");
        return false;
    }

    if (!stateEpubPath.isEmpty() && stateEpubPath != m_epubPath) {
        Serial.println("READER: state epubPath mismatch");
        logBookLaunchAction("reading state ignored: epubPath mismatch");
        return false;
    }

    if (state.globalPage < 1) {
        state.globalPage = 1;
    }

    state.valid = true;

    Serial.print("READER: loaded state from ");
    Serial.println(m_readerStatePath);

    Serial.print("READER: state global page = ");
    Serial.println(state.globalPage);

    Serial.print("READER: state total pages = ");
    Serial.println(state.totalPages);
    String message = "reading state loaded from: ";
    message += m_readerStatePath;
    logBookLaunchAction(message);

    return true;
}

bool EpubReaderService::saveReadingState() {
    if (!m_opened || m_readerStatePath.isEmpty()) {
        return false;
    }

    File file = m_fs.open(m_readerStatePath, "w");

    if (!file) {
        Serial.print("READER: failed to write state file: ");
        Serial.println(m_readerStatePath);
        String message = "reading state write failed: ";
        message += m_readerStatePath;
        logBookLaunchAction(message);
        return false;
    }

    const int globalPage = getGlobalPageNumber();

    file.print("version=");
    file.println(Constants::PAGE_INDEX_VERSION);

    file.print("epubPath=");
    file.println(m_epubPath);

    file.print("spineIndex=");
    file.println(m_currentSpineIndex);

    file.print("pageIndex=");
    file.println(m_currentPageIndex);

    file.print("globalPage=");
    file.println(globalPage);

    file.print("totalPages=");
    file.println(m_totalPageCount);

    file.close();

    Serial.print("READER: saved state page ");
    Serial.print(globalPage);
    Serial.print(" / ");
    Serial.println(m_totalPageCount);
    String message = "reading state saved: globalPage=";
    message += String(globalPage);
    message += "/";
    message += String(m_totalPageCount);
    logBookLaunchAction(message);

    return true;
}

bool EpubReaderService::setPositionByGlobalPage(int globalPage) {
    if (globalPage < 1) {
        globalPage = 1;
    }

    if (m_totalPageCount > 0 && globalPage > m_totalPageCount) {
        globalPage = m_totalPageCount;
    }

    int remaining = globalPage;

    for (int i = 0; i < static_cast<int>(m_spinePageCounts.size()); i++) {
        const int sectionPages = m_spinePageCounts[i];

        if (sectionPages <= 0) {
            continue;
        }

        if (remaining <= sectionPages) {
            m_currentSpineIndex = i;
            m_currentPageIndex = remaining - 1;

            Serial.print("READER: restored global page ");
            Serial.print(globalPage);
            Serial.print(" as spine ");
            Serial.print(m_currentSpineIndex);
            Serial.print(", page ");
            Serial.println(m_currentPageIndex);

            return true;
        }

        remaining -= sectionPages;
    }

    return false;
}

String EpubReaderService::parserErrorOrFallback(const String &fallback) const {
    const String parserError = m_parser.getLastError();
    return parserError.isEmpty() ? fallback : parserError;
}

String EpubReaderService::describeCurrentSpineItem() const {
    String description = "#";
    description += String(m_currentSpineIndex + 1);
    description += "/";
    description += String(static_cast<int>(m_structure.spine.size()));

    if (m_currentSpineIndex >= 0
        && m_currentSpineIndex < static_cast<int>(m_structure.spine.size())) {
        description += " ";
        description += m_structure.spine[m_currentSpineIndex].path;
    }

    return description;
}

void EpubReaderService::failWithMessage(const String &title, const String &message) {
    setLastError(message);
    String logMessage = "ERROR: ";
    logMessage += title;
    logMessage += ": ";
    logMessage += message;
    logBookLaunchAction(logMessage);

    String screenMessage = message;
    if (screenMessage.length() > 260) {
        screenMessage = screenMessage.substring(0, 257);
        screenMessage += "...";
    }

    m_display.showMessage(title, screenMessage);
}

void EpubReaderService::setLastError(const String &message) {
    m_lastError = message;
}

void EpubReaderService::clearLastError() {
    m_lastError = "";
}

void EpubReaderService::startBookLaunchLog(const String &epubPath) {
    rotateBookLaunchLogs();

    File file = m_fs.open(READER_LOG_PATHS[0], "w");
    if (!file) {
        Serial.print("READER: failed to create launch log: ");
        Serial.println(READER_LOG_PATHS[0]);
        return;
    }

    file.println("EBook reader launch log");
    file.print("startedMs=");
    file.println(static_cast<unsigned long>(millis()));
    file.print("epubPath=");
    file.println(epubPath);
    file.println();
    file.close();
}

void EpubReaderService::rotateBookLaunchLogs() {
    if (!m_fs.exists(READER_LOG_DIR)) {
        if (!m_fs.mkdir(READER_LOG_DIR)) {
            Serial.print("READER: failed to create log dir: ");
            Serial.println(READER_LOG_DIR);
            return;
        }
    }

    if (m_fs.exists(READER_LOG_PATHS[READER_LOG_COUNT - 1])) {
        m_fs.remove(READER_LOG_PATHS[READER_LOG_COUNT - 1]);
    }

    for (int i = READER_LOG_COUNT - 2; i >= 0; i--) {
        if (!m_fs.exists(READER_LOG_PATHS[i])) {
            continue;
        }

        if (m_fs.exists(READER_LOG_PATHS[i + 1])) {
            m_fs.remove(READER_LOG_PATHS[i + 1]);
        }

        if (!m_fs.rename(READER_LOG_PATHS[i], READER_LOG_PATHS[i + 1])) {
            Serial.print("READER: failed to rotate log: ");
            Serial.print(READER_LOG_PATHS[i]);
            Serial.print(" -> ");
            Serial.println(READER_LOG_PATHS[i + 1]);
        }
    }
}

void EpubReaderService::logBookLaunchAction(const String &message) {
    File file = m_fs.open(READER_LOG_PATHS[0], "a");
    if (!file) {
        return;
    }

    file.print("[");
    file.print(static_cast<unsigned long>(millis()));
    file.print("] ");
    file.println(message);
    file.close();
}

void EpubReaderService::logBookLaunchResult(bool ok, const String &message) {
    String line = ok ? "RESULT: OK" : "RESULT: FAILED";

    if (!message.isEmpty()) {
        line += " - ";
        line += message;
    }

    logBookLaunchAction(line);
}
