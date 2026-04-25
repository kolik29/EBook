#include "EpubReaderService.h"

EpubReaderService::EpubReaderService(EpubParserService &parser, DisplayDriver &display)
    : m_parser(parser),
      m_display(display),
      m_paginator(40, 30) {
}

bool EpubReaderService::openBook(const String &epubPath) {
    Serial.print("READER: open ");
    Serial.println(epubPath);

    m_epubPath = epubPath;
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

    return loadCurrentSpineItem();
}

bool EpubReaderService::loadCurrentSpineItem() {
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
    m_currentPageIndex = 0;

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
        return;
    }

    if (m_currentSpineIndex + 1 < static_cast<int>(m_structure.spine.size())) {
        m_currentSpineIndex++;
        loadCurrentSpineItem();
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
        return;
    }

    if (m_currentSpineIndex > 0) {
        m_currentSpineIndex--;

        if (loadCurrentSpineItem()) {
            m_currentPageIndex = static_cast<int>(m_currentPages.size()) - 1;
            renderCurrentPage();
        }

        return;
    }

    Serial.println("READER: already at first page");
}

bool EpubReaderService::buildPageIndex() {
    m_spinePageCounts.clear();
    m_totalPageCount = 0;

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

    return m_totalPageCount > 0;
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