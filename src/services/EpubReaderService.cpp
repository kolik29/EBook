#include "EpubReaderService.h"

EpubReaderService::EpubReaderService(
    fs::FS &fs,
    EpubParserService &parser,
    DisplayDriver &display
)
    : m_fs(fs),
      m_parser(parser),
      m_display(display),
      m_paginator(PAGE_MAX_CHARS_PER_LINE, PAGE_MAX_LINES_PER_PAGE) {
}

bool EpubReaderService::openBook(const String &epubPath) {
    Serial.print("READER: open ");
    Serial.println(epubPath);

    m_epubPath = epubPath;
    m_pageIndexPath = getPageIndexPathForEpub(epubPath);
    m_readerStatePath = getReaderStatePathForEpub(epubPath);
    m_currentSpineIndex = 0;
    m_currentPageIndex = 0;
    m_currentPages.clear();
    m_opened = false;

    if (!m_parser.parseBookStructure(epubPath, m_structure)) {
        Serial.println("READER: failed to parse book structure");
        m_display.showMessage("EPUB error", "Failed to parse book structure.");
        return false;
    }

    if (m_structure.spine.empty()) {
        Serial.println("READER: empty spine");
        m_display.showMessage("EPUB error", "Book spine is empty.");
        return false;
    }

    m_opened = true;

    if (!buildPageIndex()) {
        Serial.println("READER: failed to build page index");
        m_display.showMessage("EPUB error", "Failed to build page index.");
        return false;
    }

    ReadingState state;

    if (loadReadingState(state)) {
        Serial.println("READER: reading state loaded");

        if (state.totalPages == m_totalPageCount
            && state.spineIndex >= 0
            && state.spineIndex < static_cast<int>(m_structure.spine.size())) {
            m_currentSpineIndex = state.spineIndex;
            m_currentPageIndex = state.pageIndex;

            Serial.print("READER: restore by spine/page: ");
            Serial.print(m_currentSpineIndex);
            Serial.print(" / ");
            Serial.println(m_currentPageIndex);

            return loadCurrentSpineItem(m_currentPageIndex);
        }

        Serial.println("READER: total pages changed, restore by global page");

        if (setPositionByGlobalPage(state.globalPage)) {
            return loadCurrentSpineItem(m_currentPageIndex);
        }
    }

    Serial.println("READER: no valid reading state, open first page");

    m_currentSpineIndex = 0;
    m_currentPageIndex = 0;

    if (!loadCurrentSpineItem(0)) {
        return false;
    }

    saveReadingState();

    return true;
}

bool EpubReaderService::loadCurrentSpineItem(int preferredPageIndex) {
    if (!m_opened) {
        return false;
    }

    if (m_currentSpineIndex < 0
        || m_currentSpineIndex >= static_cast<int>(m_structure.spine.size())) {
        return false;
    }

    String text;

    if (!m_parser.readSpineItemText(
        m_epubPath,
        m_structure.spine[m_currentSpineIndex],
        text
    )) {
        Serial.println("READER: failed to read spine item");
        m_display.showMessage("Read error", "Failed to read EPUB section.");
        return false;
    }

    m_currentPages = m_paginator.paginate(text);

    if (m_currentPages.empty()) {
        m_currentPages.push_back("");
    }

    if (preferredPageIndex < 0) {
        m_currentPageIndex = static_cast<int>(m_currentPages.size()) - 1;
    } else if (preferredPageIndex >= static_cast<int>(m_currentPages.size())) {
        m_currentPageIndex = static_cast<int>(m_currentPages.size()) - 1;
    } else {
        m_currentPageIndex = preferredPageIndex;
    }

    Serial.print("READER: spine ");
    Serial.print(m_currentSpineIndex + 1);
    Serial.print(" / ");
    Serial.println(static_cast<int>(m_structure.spine.size()));

    Serial.print("READER: pages in section = ");
    Serial.println(static_cast<int>(m_currentPages.size()));

    renderCurrentPage();

    return true;
}

void EpubReaderService::showCurrentPage() {
    renderCurrentPage();
}

void EpubReaderService::renderCurrentPage() {
    if (!m_opened || m_currentPages.empty()) {
        return;
    }

    if (m_currentPageIndex < 0) {
        m_currentPageIndex = 0;
    }

    if (m_currentPageIndex >= static_cast<int>(m_currentPages.size())) {
        m_currentPageIndex = static_cast<int>(m_currentPages.size()) - 1;
    }

    String title = m_structure.title.length() > 0
        ? m_structure.title
        : "EPUB book";

    const int globalPage = getGlobalPageNumber();

    Serial.print("READER: render global page ");
    Serial.print(globalPage);
    Serial.print(" / ");
    Serial.println(m_totalPageCount);

    m_display.showTextPage(
        title,
        m_currentPages[m_currentPageIndex],
        globalPage,
        m_totalPageCount
    );
}

void EpubReaderService::nextPage() {
    if (!m_opened) {
        return;
    }

    if (m_currentPageIndex + 1 < static_cast<int>(m_currentPages.size())) {
        m_currentPageIndex++;
        renderCurrentPage();
        saveReadingState();
        return;
    }

    if (m_currentSpineIndex + 1 < static_cast<int>(m_structure.spine.size())) {
        m_currentSpineIndex++;

        if (loadCurrentSpineItem(0)) {
            saveReadingState();
        }

        return;
    }

    Serial.println("READER: already at last page");
}

void EpubReaderService::prevPage() {
    if (!m_opened) {
        return;
    }

    if (m_currentPageIndex > 0) {
        m_currentPageIndex--;
        renderCurrentPage();
        saveReadingState();
        return;
    }

    if (m_currentSpineIndex > 0) {
        m_currentSpineIndex--;

        if (loadCurrentSpineItem(-1)) {
            saveReadingState();
        }

        return;
    }

    Serial.println("READER: already at first page");
}

bool EpubReaderService::buildPageIndex() {
    m_spinePageCounts.clear();
    m_totalPageCount = 0;

    if (loadPageIndexCache()) {
        Serial.println("READER: page index loaded from cache");

        Serial.print("READER: total pages = ");
        Serial.println(m_totalPageCount);

        return true;
    }

    Serial.println("READER: page index cache not found or invalid");
    Serial.println("READER: building page index");

    for (int i = 0; i < static_cast<int>(m_structure.spine.size()); i++) {
        String text;

        if (!m_parser.readSpineItemText(
            m_epubPath,
            m_structure.spine[i],
            text
        )) {
            Serial.print("READER: failed to read spine for page index: ");
            Serial.println(i);

            m_spinePageCounts.push_back(0);
            continue;
        }

        std::vector<String> pages = m_paginator.paginate(text);
        const int pageCount = static_cast<int>(pages.size());

        m_spinePageCounts.push_back(pageCount);
        m_totalPageCount += pageCount;

        Serial.print("READER: section ");
        Serial.print(i + 1);
        Serial.print(" pages = ");
        Serial.println(pageCount);
    }

    Serial.print("READER: total pages = ");
    Serial.println(m_totalPageCount);

    if (m_totalPageCount <= 0) {
        return false;
    }

    savePageIndexCache();

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
        return false;
    }

    File file = m_fs.open(m_pageIndexPath, "r");

    if (!file || file.isDirectory()) {
        Serial.print("READER: failed to open page index file: ");
        Serial.println(m_pageIndexPath);
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

    if (version != 1) {
        Serial.println("READER: page index version mismatch");
        return false;
    }

    if (charsPerLine != PAGE_MAX_CHARS_PER_LINE
        || linesPerPage != PAGE_MAX_LINES_PER_PAGE) {
        Serial.println("READER: page index layout settings changed");
        return false;
    }

    if (spineCount != static_cast<int>(m_structure.spine.size())) {
        Serial.println("READER: page index spine count mismatch");
        return false;
    }

    if (totalPages <= 0 || countsLine.isEmpty()) {
        Serial.println("READER: page index is empty or broken");
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
        return false;
    }

    int calculatedTotal = 0;

    for (int count : counts) {
        if (count < 0) {
            Serial.println("READER: page index has invalid page count");
            return false;
        }

        calculatedTotal += count;
    }

    if (calculatedTotal != totalPages) {
        Serial.println("READER: page index total pages mismatch");
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
        return false;
    }

    file.println("version=1");

    file.print("charsPerLine=");
    file.println(PAGE_MAX_CHARS_PER_LINE);

    file.print("linesPerPage=");
    file.println(PAGE_MAX_LINES_PER_PAGE);

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
        return false;
    }

    File file = m_fs.open(m_readerStatePath, "r");

    if (!file || file.isDirectory()) {
        Serial.print("READER: failed to open state file: ");
        Serial.println(m_readerStatePath);
        return false;
    }

    String stateEpubPath = "";

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

        if (key == "epubPath") {
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

    if (!stateEpubPath.isEmpty() && stateEpubPath != m_epubPath) {
        Serial.println("READER: state epubPath mismatch");
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
        return false;
    }

    const int globalPage = getGlobalPageNumber();

    file.println("version=1");

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