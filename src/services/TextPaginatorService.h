#pragma once

#include <Arduino.h>
#include <vector>

class TextPaginatorService {
public:
    TextPaginatorService(
        int maxCharsPerLine,
        int maxLinesPerPage
    );

    std::vector<String> paginate(const String &text) const;

private:
    int m_maxCharsPerLine;
    int m_maxLinesPerPage;

    void appendParagraph(
        const String &paragraph,
        std::vector<String> &pages,
        String &currentPage,
        int &currentLineCount
    ) const;

    void appendLine(
        const String &line,
        std::vector<String> &pages,
        String &currentPage,
        int &currentLineCount
    ) const;

    void flushPage(
        std::vector<String> &pages,
        String &currentPage,
        int &currentLineCount
    ) const;
};