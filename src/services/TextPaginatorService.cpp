#include "TextPaginatorService.h"
#include "BookTextCodec.h"

TextPaginatorService::TextPaginatorService(
    int maxCharsPerLine,
    int maxLinesPerPage
)
    : m_maxCharsPerLine(maxCharsPerLine),
      m_maxLinesPerPage(maxLinesPerPage) {
}

std::vector<String> TextPaginatorService::paginate(const String &text) const {
    std::vector<String> pages;

    String normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace("\r", "\n");

    String currentPage = "";
    int currentLineCount = 0;

    int start = 0;

    while (start <= normalized.length()) {
        int end = normalized.indexOf('\n', start);

        if (end < 0) {
            end = normalized.length();
        }

        String paragraph = normalized.substring(start, end);
        paragraph.trim();

        if (paragraph.isEmpty()) {
            appendLine("", pages, currentPage, currentLineCount);
        } else {
            appendParagraph(paragraph, pages, currentPage, currentLineCount);
        }

        if (end >= normalized.length()) {
            break;
        }

        start = end + 1;
    }

    flushPage(pages, currentPage, currentLineCount);

    if (pages.empty()) {
        pages.push_back("");
    }

    return pages;
}

void TextPaginatorService::appendParagraph(
    const String &paragraph,
    std::vector<String> &pages,
    String &currentPage,
    int &currentLineCount
) const {
    int start = 0;

    while (start < paragraph.length()) {
        while (start < paragraph.length() && paragraph[start] == ' ') {
            start++;
        }

        if (start >= paragraph.length()) {
            break;
        }

        const String remaining = paragraph.substring(start);
        String prefix = BookTextCodec::utf8PrefixByCodepoints(
            remaining,
            m_maxCharsPerLine
        );

        if (prefix.isEmpty()) {
            prefix = paragraph.substring(start, start + 1);
        }

        int end = start + prefix.length();

        if (end >= paragraph.length()) {
            end = paragraph.length();
        } else {
            int spaceIndex = paragraph.lastIndexOf(' ', end);

            if (spaceIndex > start) {
                end = spaceIndex;
            }
        }

        String line = paragraph.substring(start, end);
        line.trim();

        appendLine(line, pages, currentPage, currentLineCount);

        start = end;
    }
}

void TextPaginatorService::appendLine(
    const String &line,
    std::vector<String> &pages,
    String &currentPage,
    int &currentLineCount
) const {
    if (currentLineCount >= m_maxLinesPerPage) {
        flushPage(pages, currentPage, currentLineCount);
    }

    currentPage += line;
    currentPage += "\n";
    currentLineCount++;
}

void TextPaginatorService::flushPage(
    std::vector<String> &pages,
    String &currentPage,
    int &currentLineCount
) const {
    currentPage.trim();

    if (!currentPage.isEmpty()) {
        pages.push_back(currentPage);
    }

    currentPage = "";
    currentLineCount = 0;
}
