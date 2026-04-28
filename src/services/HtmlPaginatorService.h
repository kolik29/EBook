#pragma once

#include <Arduino.h>
#include <vector>

#include "../models/HtmlRenderModels.h"

class HtmlPaginatorService {
public:
    HtmlPaginatorService(int pageWidthPx, int pageHeightPx);

    std::vector<HtmlRenderPage> paginate(const String &html, const String &baseFilePath) const;
    int countPages(const String &html, const String &baseFilePath) const;
    bool paginatePage(
        const String &html,
        const String &baseFilePath,
        int pageIndex,
        HtmlRenderPage &outPage,
        int &outPageCount
    ) const;

private:
    struct CssRule {
        String selector;
        String declarations;
    };

    struct TagInfo {
        String name;
        String raw;
        bool closing = false;
        bool selfClosing = false;
    };

    struct CurrentLine {
        std::vector<HtmlTextRun> runs;
        HtmlTextStyle style;
        int widthPx = 0;
        int heightPx = 0;
        bool active = false;
    };

    struct PageCollector {
        std::vector<HtmlRenderPage> *pages = nullptr;
        HtmlRenderPage *targetPage = nullptr;
        int targetPageIndex = -1;
        int pageCount = 0;
    };

    int m_pageWidthPx;
    int m_pageHeightPx;

    void paginateInternal(
        const String &html,
        const String &baseFilePath,
        PageCollector &collector
    ) const;

    void collectCssRules(const String &html, std::vector<CssRule> &rules) const;
    void appendCssRulesFromBlock(const String &css, std::vector<CssRule> &rules) const;

    bool readTag(const String &html, int start, TagInfo &tag, int &nextPos) const;
    String getAttributeValue(const String &rawTag, const String &attributeName) const;
    String getCssDeclarationValue(const String &declarations, const String &propertyName) const;

    void applyTagStyle(const String &tagName, HtmlTextStyle &style) const;
    void applyCssRules(
        const String &tagName,
        const String &rawTag,
        const std::vector<CssRule> &rules,
        HtmlTextStyle &style
    ) const;
    void applyDeclarations(const String &declarations, HtmlTextStyle &style) const;

    void appendText(
        const String &text,
        const HtmlTextStyle &style,
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void appendWord(
        const String &word,
        const HtmlTextStyle &style,
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void flushLine(
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void addSpacer(
        int heightPx,
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void addRule(
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void addImage(
        const String &rawTag,
        const HtmlTextStyle &style,
        const String &baseFilePath,
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        CurrentLine &line,
        int &cursorY
    ) const;
    void finishPageIfNeeded(
        int nextHeightPx,
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        int &cursorY
    ) const;
    void pushPageIfNotEmpty(
        PageCollector &collector,
        HtmlRenderPage &currentPage,
        int &cursorY
    ) const;

    int measureText(const String &text, const HtmlTextStyle &style) const;
    int lineHeight(const HtmlTextStyle &style) const;
    int charWidth(const HtmlTextStyle &style) const;
    int parseCssPx(const String &value, int fallbackPx, int parentPx) const;

    bool isBlockTag(const String &tagName) const;
    bool isParagraphTag(const String &tagName) const;
    bool isHeadingTag(const String &tagName) const;
    bool isIgnoredContentTag(const String &tagName) const;

    String decodeHtmlEntities(String value) const;
    String resolveRelativePath(const String &baseFilePath, const String &relativePath) const;
    String normalizePath(const String &path) const;
    String tagNameFromSelector(String selector) const;
};
