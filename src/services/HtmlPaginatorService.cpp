#include "HtmlPaginatorService.h"

#include <ctype.h>
#include <utility>

#include "../config/Constants.h"

namespace {
    bool isSpaceChar(char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f';
    }

    String lowerCopy(String value) {
        value.toLowerCase();
        return value;
    }

    int indexOfIgnoreCase(const String &text, const String &pattern, int fromIndex = 0) {
        if (pattern.isEmpty()) {
            return fromIndex <= text.length() ? fromIndex : -1;
        }

        if (fromIndex < 0) {
            fromIndex = 0;
        }

        const int lastStart = text.length() - pattern.length();

        for (int i = fromIndex; i <= lastStart; i++) {
            bool match = true;

            for (int j = 0; j < pattern.length(); j++) {
                char a = text[i + j];
                char b = pattern[j];

                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<char>(a - 'A' + 'a');
                }

                if (b >= 'A' && b <= 'Z') {
                    b = static_cast<char>(b - 'A' + 'a');
                }

                if (a != b) {
                    match = false;
                    break;
                }
            }

            if (match) {
                return i;
            }
        }

        return -1;
    }

    bool classListContains(const String &classList, const String &className) {
        int start = 0;

        while (start <= classList.length()) {
            int end = classList.indexOf(' ', start);
            if (end < 0) {
                end = classList.length();
            }

            String item = classList.substring(start, end);
            item.trim();

            if (item == className) {
                return true;
            }

            if (end >= classList.length()) {
                break;
            }

            start = end + 1;
        }

        return false;
    }
}

HtmlPaginatorService::HtmlPaginatorService(int pageWidthPx, int pageHeightPx)
    : m_pageWidthPx(pageWidthPx),
      m_pageHeightPx(pageHeightPx) {
}

std::vector<HtmlRenderPage> HtmlPaginatorService::paginate(
    const String &html,
    const String &baseFilePath
) const {
    std::vector<HtmlRenderPage> pages;
    PageCollector collector;
    collector.pages = &pages;

    paginateInternal(html, baseFilePath, collector);

    if (pages.empty()) {
        pages.push_back(HtmlRenderPage());
    }

    return pages;
}

int HtmlPaginatorService::countPages(const String &html, const String &baseFilePath) const {
    PageCollector collector;
    paginateInternal(html, baseFilePath, collector);
    return collector.pageCount > 0 ? collector.pageCount : 1;
}

bool HtmlPaginatorService::paginatePage(
    const String &html,
    const String &baseFilePath,
    int pageIndex,
    HtmlRenderPage &outPage,
    int &outPageCount
) const {
    outPage = HtmlRenderPage();
    outPageCount = 0;

    if (pageIndex < 0) {
        pageIndex = 0;
    }

    PageCollector collector;
    collector.targetPage = &outPage;
    collector.targetPageIndex = pageIndex;

    paginateInternal(html, baseFilePath, collector);
    outPageCount = collector.pageCount > 0 ? collector.pageCount : 1;

    return pageIndex < outPageCount;
}

void HtmlPaginatorService::paginateInternal(
    const String &html,
    const String &baseFilePath,
    PageCollector &collector
) const {
    std::vector<CssRule> cssRules;
    std::vector<HtmlTextStyle> styleStack;

    collectCssRules(html, cssRules);

    HtmlRenderPage currentPage;
    currentPage.elements.reserve(40);
    CurrentLine line;
    HtmlTextStyle currentStyle;
    int cursorY = 0;
    int pos = 0;

    styleStack.push_back(currentStyle);

    while (pos < html.length()) {
        const int tagStart = html.indexOf('<', pos);

        if (tagStart < 0) {
            appendText(
                html.substring(pos),
                currentStyle,
                collector,
                currentPage,
                line,
                cursorY
            );
            break;
        }

        if (tagStart > pos) {
            appendText(
                html.substring(pos, tagStart),
                currentStyle,
                collector,
                currentPage,
                line,
                cursorY
            );
        }

        TagInfo tag;
        int nextPos = tagStart + 1;

        if (!readTag(html, tagStart, tag, nextPos)) {
            pos = tagStart + 1;
            continue;
        }

        const String tagName = tag.name;

        if (tagName.startsWith("!")) {
            pos = nextPos;
            continue;
        }

        if (isIgnoredContentTag(tagName) && !tag.closing) {
            String closeToken = "</" + tagName + ">";
            int closePos = indexOfIgnoreCase(html, closeToken, nextPos);
            pos = closePos >= 0
                ? closePos + closeToken.length()
                : nextPos;
            continue;
        }

        if (tag.closing) {
            if (isParagraphTag(tagName) || isHeadingTag(tagName) || tagName == "li") {
                addSpacer(isHeadingTag(tagName) ? 10 : 7, collector, currentPage, line, cursorY);
            } else if (isBlockTag(tagName)) {
                flushLine(collector, currentPage, line, cursorY);
            }

            if (styleStack.size() > 1) {
                styleStack.pop_back();
                currentStyle = styleStack.back();
            }

            pos = nextPos;
            continue;
        }

        if (tagName == "br") {
            flushLine(collector, currentPage, line, cursorY);
            pos = nextPos;
            continue;
        }

        if (tagName == "hr") {
            addRule(collector, currentPage, line, cursorY);
            pos = nextPos;
            continue;
        }

        if (tagName == "img" || tagName == "image") {
            addImage(tag.raw, currentStyle, baseFilePath, collector, currentPage, line, cursorY);
            pos = nextPos;
            continue;
        }

        HtmlTextStyle nextStyle = currentStyle;

        applyTagStyle(tagName, nextStyle);
        applyCssRules(tagName, tag.raw, cssRules, nextStyle);
        applyDeclarations(getAttributeValue(tag.raw, "style"), nextStyle);

        if (isParagraphTag(tagName) || isHeadingTag(tagName) || tagName == "li") {
            addSpacer(isHeadingTag(tagName) ? 10 : 6, collector, currentPage, line, cursorY);
        } else if (tagName == "blockquote") {
            addSpacer(8, collector, currentPage, line, cursorY);
        } else if (isBlockTag(tagName)) {
            flushLine(collector, currentPage, line, cursorY);
        }

        styleStack.push_back(nextStyle);
        currentStyle = nextStyle;

        if (tag.selfClosing && styleStack.size() > 1) {
            styleStack.pop_back();
            currentStyle = styleStack.back();
        }

        pos = nextPos;
    }

    flushLine(collector, currentPage, line, cursorY);
    pushPageIfNotEmpty(collector, currentPage, cursorY);
}

void HtmlPaginatorService::collectCssRules(const String &html, std::vector<CssRule> &rules) const {
    int searchPos = 0;

    while (true) {
        const int styleStart = indexOfIgnoreCase(html, "<style", searchPos);
        if (styleStart < 0) {
            break;
        }

        const int openEnd = html.indexOf('>', styleStart);
        if (openEnd < 0) {
            break;
        }

        const int closeStart = indexOfIgnoreCase(html, "</style>", openEnd + 1);
        if (closeStart < 0) {
            break;
        }

        appendCssRulesFromBlock(html.substring(openEnd + 1, closeStart), rules);
        searchPos = closeStart + 8;
    }
}

void HtmlPaginatorService::appendCssRulesFromBlock(
    const String &css,
    std::vector<CssRule> &rules
) const {
    int searchPos = 0;

    while (searchPos < css.length()) {
        const int openBrace = css.indexOf('{', searchPos);
        if (openBrace < 0) {
            break;
        }

        const int closeBrace = css.indexOf('}', openBrace + 1);
        if (closeBrace < 0) {
            break;
        }

        String selectors = css.substring(searchPos, openBrace);
        String declarations = css.substring(openBrace + 1, closeBrace);
        selectors.trim();
        declarations.trim();

        int selectorStart = 0;

        while (selectorStart <= selectors.length()) {
            int selectorEnd = selectors.indexOf(',', selectorStart);
            if (selectorEnd < 0) {
                selectorEnd = selectors.length();
            }

            String selector = selectors.substring(selectorStart, selectorEnd);
            selector.trim();
            selector.toLowerCase();

            if (!selector.isEmpty()) {
                CssRule rule;
                rule.selector = selector;
                rule.declarations = declarations;
                rules.push_back(rule);
            }

            if (selectorEnd >= selectors.length()) {
                break;
            }

            selectorStart = selectorEnd + 1;
        }

        searchPos = closeBrace + 1;
    }
}

bool HtmlPaginatorService::readTag(
    const String &html,
    int start,
    TagInfo &tag,
    int &nextPos
) const {
    const int end = html.indexOf('>', start);
    if (end < 0) {
        return false;
    }

    tag.raw = html.substring(start, end + 1);
    tag.raw.trim();
    tag.closing = false;
    tag.selfClosing = tag.raw.endsWith("/>");

    int nameStart = start + 1;

    while (nameStart < end && isSpaceChar(html[nameStart])) {
        nameStart++;
    }

    if (nameStart < end && html[nameStart] == '/') {
        tag.closing = true;
        nameStart++;
    }

    while (nameStart < end && isSpaceChar(html[nameStart])) {
        nameStart++;
    }

    int nameEnd = nameStart;
    while (nameEnd < end
        && !isSpaceChar(html[nameEnd])
        && html[nameEnd] != '/'
        && html[nameEnd] != '>') {
        nameEnd++;
    }

    tag.name = html.substring(nameStart, nameEnd);
    tag.name.toLowerCase();
    tag.name.trim();

    nextPos = end + 1;
    return !tag.name.isEmpty();
}

String HtmlPaginatorService::getAttributeValue(
    const String &rawTag,
    const String &attributeName
) const {
    String lowerTag = lowerCopy(rawTag);
    String lowerName = lowerCopy(attributeName);
    int searchPos = 0;

    while (true) {
        const int attrPos = lowerTag.indexOf(lowerName, searchPos);
        if (attrPos < 0) {
            return "";
        }

        const int nameEnd = attrPos + lowerName.length();

        if (attrPos > 0) {
            const char prev = lowerTag[attrPos - 1];
            if (!(isSpaceChar(prev) || prev == '<' || prev == '/')) {
                searchPos = nameEnd;
                continue;
            }
        }

        int pos = nameEnd;
        while (pos < rawTag.length() && isSpaceChar(rawTag[pos])) {
            pos++;
        }

        if (pos >= rawTag.length() || rawTag[pos] != '=') {
            searchPos = nameEnd;
            continue;
        }

        pos++;
        while (pos < rawTag.length() && isSpaceChar(rawTag[pos])) {
            pos++;
        }

        if (pos >= rawTag.length()) {
            return "";
        }

        const char quote = rawTag[pos];
        if (quote == '"' || quote == '\'') {
            const int valueStart = pos + 1;
            const int valueEnd = rawTag.indexOf(quote, valueStart);
            if (valueEnd < 0) {
                return "";
            }

            return decodeHtmlEntities(rawTag.substring(valueStart, valueEnd));
        }

        const int valueStart = pos;
        while (pos < rawTag.length()
            && !isSpaceChar(rawTag[pos])
            && rawTag[pos] != '>') {
            pos++;
        }

        return decodeHtmlEntities(rawTag.substring(valueStart, pos));
    }
}

String HtmlPaginatorService::getCssDeclarationValue(
    const String &declarations,
    const String &propertyName
) const {
    String normalized = declarations;
    normalized.replace("\n", ";");
    normalized.replace("\r", ";");

    String wanted = propertyName;
    wanted.trim();
    wanted.toLowerCase();

    int start = 0;

    while (start <= normalized.length()) {
        int end = normalized.indexOf(';', start);
        if (end < 0) {
            end = normalized.length();
        }

        String declaration = normalized.substring(start, end);
        declaration.trim();

        const int colon = declaration.indexOf(':');
        if (colon > 0) {
            String property = declaration.substring(0, colon);
            String value = declaration.substring(colon + 1);
            property.trim();
            property.toLowerCase();
            value.trim();

            if (property == wanted) {
                return value;
            }
        }

        if (end >= normalized.length()) {
            break;
        }

        start = end + 1;
    }

    return "";
}

void HtmlPaginatorService::applyTagStyle(const String &tagName, HtmlTextStyle &style) const {
    if (tagName == "b" || tagName == "strong") {
        style.bold = true;
    } else if (tagName == "i" || tagName == "em" || tagName == "cite") {
        style.italic = true;
    } else if (tagName == "u") {
        style.underline = true;
    } else if (tagName == "small") {
        style.size = HtmlTextSize::Small;
    } else if (tagName == "big") {
        style.size = HtmlTextSize::Large;
    } else if (tagName == "h1") {
        style.size = HtmlTextSize::Heading1;
        style.bold = true;
        style.align = HtmlTextAlign::Center;
    } else if (tagName == "h2") {
        style.size = HtmlTextSize::Heading2;
        style.bold = true;
    } else if (tagName == "h3" || tagName == "h4" || tagName == "h5" || tagName == "h6") {
        style.size = HtmlTextSize::Heading3;
        style.bold = true;
    } else if (tagName == "center") {
        style.align = HtmlTextAlign::Center;
    } else if (tagName == "blockquote") {
        style.indentPx += 36;
    } else if (tagName == "li") {
        style.indentPx += 22;
    } else if (tagName == "sub" || tagName == "sup") {
        style.size = HtmlTextSize::Small;
    }
}

void HtmlPaginatorService::applyCssRules(
    const String &tagName,
    const String &rawTag,
    const std::vector<CssRule> &rules,
    HtmlTextStyle &style
) const {
    const String classList = getAttributeValue(rawTag, "class");
    const String id = getAttributeValue(rawTag, "id");

    for (const CssRule &rule : rules) {
        const String selector = rule.selector;

        if (selector.startsWith(".")) {
            if (classListContains(classList, selector.substring(1))) {
                applyDeclarations(rule.declarations, style);
            }
            continue;
        }

        if (selector.startsWith("#")) {
            if (id == selector.substring(1)) {
                applyDeclarations(rule.declarations, style);
            }
            continue;
        }

        if (tagNameFromSelector(selector) == tagName) {
            applyDeclarations(rule.declarations, style);
        }
    }
}

void HtmlPaginatorService::applyDeclarations(const String &inputDeclarations, HtmlTextStyle &style) const {
    String declarations = inputDeclarations;
    declarations.replace("\n", ";");
    declarations.replace("\r", ";");

    int start = 0;

    while (start <= declarations.length()) {
        int end = declarations.indexOf(';', start);
        if (end < 0) {
            end = declarations.length();
        }

        String declaration = declarations.substring(start, end);
        declaration.trim();

        const int colon = declaration.indexOf(':');
        if (colon > 0) {
            String property = declaration.substring(0, colon);
            String value = declaration.substring(colon + 1);
            property.trim();
            property.toLowerCase();
            value.trim();
            value.toLowerCase();

            if (property == "font-weight") {
                style.bold = value == "bold" || value == "bolder" || value.toInt() >= 600;
            } else if (property == "font-style") {
                style.italic = value.indexOf("italic") >= 0 || value.indexOf("oblique") >= 0;
            } else if (property == "text-decoration") {
                style.underline = value.indexOf("underline") >= 0;
            } else if (property == "text-align") {
                if (value.indexOf("center") >= 0) {
                    style.align = HtmlTextAlign::Center;
                } else if (value.indexOf("right") >= 0) {
                    style.align = HtmlTextAlign::Right;
                } else {
                    style.align = HtmlTextAlign::Left;
                }
            } else if (property == "display" && value == "none") {
                style.hidden = true;
            } else if (property == "visibility" && value == "hidden") {
                style.hidden = true;
            } else if (property == "font-size") {
                if (value.indexOf("small") >= 0 || value.indexOf("0.") >= 0) {
                    style.size = HtmlTextSize::Small;
                } else if (value.indexOf("large") >= 0 || value.indexOf("120%") >= 0) {
                    style.size = HtmlTextSize::Large;
                } else {
                    const int px = parseCssPx(value, 0, m_pageWidthPx);
                    if (px >= 24) {
                        style.size = HtmlTextSize::Heading1;
                    } else if (px >= 18) {
                        style.size = HtmlTextSize::Heading2;
                    } else if (px > 0 && px <= 12) {
                        style.size = HtmlTextSize::Small;
                    }
                }
            } else if (property == "margin-left" || property == "padding-left") {
                style.indentPx += parseCssPx(value, 0, m_pageWidthPx);
            }
        }

        if (end >= declarations.length()) {
            break;
        }

        start = end + 1;
    }
}

void HtmlPaginatorService::appendText(
    const String &text,
    const HtmlTextStyle &style,
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    if (style.hidden || text.isEmpty()) {
        return;
    }

    String decoded = decodeHtmlEntities(text);
    String word = "";

    for (int i = 0; i < decoded.length(); i++) {
        const char c = decoded[i];

        if (isSpaceChar(c)) {
            if (!word.isEmpty()) {
                appendWord(word, style, collector, currentPage, line, cursorY);
                word = "";
            }
            continue;
        }

        word += c;
    }

    if (!word.isEmpty()) {
        appendWord(word, style, collector, currentPage, line, cursorY);
    }
}

void HtmlPaginatorService::appendWord(
    const String &word,
    const HtmlTextStyle &style,
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    if (word.isEmpty()) {
        return;
    }

    const int availableWidth = m_pageWidthPx - style.indentPx;
    const int spaceWidth = line.active ? measureText(" ", style) : 0;
    const int wordWidth = measureText(word, style);

    if (line.active && line.widthPx + spaceWidth + wordWidth > availableWidth) {
        flushLine(collector, currentPage, line, cursorY);
    }

    if (!line.active) {
        line.active = true;
        line.style = style;
        line.widthPx = 0;
        line.heightPx = lineHeight(style);
        line.runs.reserve(8);
    }

    if (line.widthPx > 0) {
        HtmlTextRun spaceRun;
        spaceRun.text = " ";
        spaceRun.style = style;
        spaceRun.widthPx = spaceWidth;
        line.runs.push_back(spaceRun);
        line.widthPx += spaceWidth;
    }

    if (wordWidth > availableWidth && word.length() > 1) {
        String remaining = word;

        while (!remaining.isEmpty()) {
            int maxChars = availableWidth / charWidth(style);
            if (maxChars < 1) {
                maxChars = 1;
            }

            String part = remaining.substring(0, maxChars);
            remaining = remaining.substring(part.length());

            HtmlTextRun run;
            run.text = part;
            run.style = style;
            run.widthPx = measureText(part, style);
            line.runs.push_back(run);
            line.widthPx += run.widthPx;

            if (!remaining.isEmpty()) {
                flushLine(collector, currentPage, line, cursorY);
                line.active = true;
                line.style = style;
                line.heightPx = lineHeight(style);
                line.runs.reserve(8);
            }
        }

        return;
    }

    HtmlTextRun run;
    run.text = word;
    run.style = style;
    run.widthPx = wordWidth;
    line.runs.push_back(run);
    line.widthPx += wordWidth;

    const int currentLineHeight = lineHeight(style);
    if (currentLineHeight > line.heightPx) {
        line.heightPx = currentLineHeight;
    }
}

void HtmlPaginatorService::flushLine(
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    if (!line.active || line.runs.empty()) {
        line = CurrentLine();
        return;
    }

    finishPageIfNeeded(line.heightPx, collector, currentPage, cursorY);

    HtmlRenderElement element;
    element.type = HtmlElementType::TextLine;
    element.style = line.style;
    element.runs = std::move(line.runs);
    element.widthPx = line.widthPx;
    element.heightPx = line.heightPx;
    element.contentWidthPx = m_pageWidthPx;
    currentPage.elements.push_back(std::move(element));

    cursorY += line.heightPx;
    line = CurrentLine();
}

void HtmlPaginatorService::addSpacer(
    int heightPx,
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    flushLine(collector, currentPage, line, cursorY);

    if (heightPx <= 0 || currentPage.elements.empty()) {
        return;
    }

    finishPageIfNeeded(heightPx, collector, currentPage, cursorY);

    HtmlRenderElement element;
    element.type = HtmlElementType::Spacer;
    element.heightPx = heightPx;
    currentPage.elements.push_back(std::move(element));
    cursorY += heightPx;
}

void HtmlPaginatorService::addRule(
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    flushLine(collector, currentPage, line, cursorY);
    finishPageIfNeeded(18, collector, currentPage, cursorY);

    HtmlRenderElement element;
    element.type = HtmlElementType::Rule;
    element.heightPx = 18;
    element.widthPx = m_pageWidthPx;
    element.contentWidthPx = m_pageWidthPx;
    currentPage.elements.push_back(std::move(element));
    cursorY += element.heightPx;
}

void HtmlPaginatorService::addImage(
    const String &rawTag,
    const HtmlTextStyle &style,
    const String &baseFilePath,
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    CurrentLine &line,
    int &cursorY
) const {
    flushLine(collector, currentPage, line, cursorY);

    if (style.hidden) {
        return;
    }

    String src = getAttributeValue(rawTag, "src");
    if (src.isEmpty()) {
        src = getAttributeValue(rawTag, "href");
    }

    if (src.isEmpty()) {
        return;
    }

    HtmlTextStyle imageStyle = style;
    applyDeclarations(getAttributeValue(rawTag, "style"), imageStyle);

    const String inlineStyle = getAttributeValue(rawTag, "style");
    const int availableWidth = m_pageWidthPx - imageStyle.indentPx;
    const int widthAttr = parseCssPx(getAttributeValue(rawTag, "width"), availableWidth, availableWidth);
    const int styleWidth = parseCssPx(getCssDeclarationValue(inlineStyle, "width"), 0, availableWidth);
    int imageWidth = styleWidth > 0 ? styleWidth : widthAttr;

    if (imageWidth <= 0 || imageWidth > availableWidth) {
        imageWidth = availableWidth;
    }

    const int styleHeight = parseCssPx(getCssDeclarationValue(inlineStyle, "height"), 0, m_pageHeightPx);
    const int heightAttr = parseCssPx(getAttributeValue(rawTag, "height"), 0, m_pageHeightPx);
    int imageHeight = heightAttr > 0
        ? heightAttr
        : (styleHeight > 0 ? styleHeight : Constants::HTML_DEFAULT_IMAGE_HEIGHT_PX);

    if (imageHeight > m_pageHeightPx) {
        imageHeight = m_pageHeightPx;
    }

    const int blockHeight = imageHeight + 10;
    finishPageIfNeeded(blockHeight, collector, currentPage, cursorY);

    HtmlRenderElement element;
    element.type = HtmlElementType::Image;
    element.style = imageStyle;
    element.imagePath = resolveRelativePath(baseFilePath, src);
    element.altText = getAttributeValue(rawTag, "alt");
    element.widthPx = imageWidth;
    element.heightPx = blockHeight;
    element.contentWidthPx = m_pageWidthPx;
    currentPage.elements.push_back(std::move(element));

    cursorY += blockHeight;
}

void HtmlPaginatorService::finishPageIfNeeded(
    int nextHeightPx,
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    int &cursorY
) const {
    if (cursorY > 0 && cursorY + nextHeightPx > m_pageHeightPx) {
        pushPageIfNotEmpty(collector, currentPage, cursorY);
    }
}

void HtmlPaginatorService::pushPageIfNotEmpty(
    PageCollector &collector,
    HtmlRenderPage &currentPage,
    int &cursorY
) const {
    if (!currentPage.elements.empty()) {
        const int pageIndex = collector.pageCount;

        if (collector.pages) {
            collector.pages->push_back(std::move(currentPage));
        } else if (collector.targetPage && pageIndex == collector.targetPageIndex) {
            *collector.targetPage = std::move(currentPage);
        }

        collector.pageCount++;
    }

    currentPage = HtmlRenderPage();
    currentPage.elements.reserve(40);
    cursorY = 0;
}

int HtmlPaginatorService::measureText(const String &text, const HtmlTextStyle &style) const {
    return text.length() * charWidth(style);
}

int HtmlPaginatorService::lineHeight(const HtmlTextStyle &style) const {
    switch (style.size) {
        case HtmlTextSize::Heading1:
            return 42;
        case HtmlTextSize::Heading2:
            return 34;
        case HtmlTextSize::Heading3:
            return 30;
        case HtmlTextSize::Large:
            return 30;
        case HtmlTextSize::Small:
            return 18;
        case HtmlTextSize::Normal:
        default:
            return 24;
    }
}

int HtmlPaginatorService::charWidth(const HtmlTextStyle &style) const {
    int width = 10;

    switch (style.size) {
        case HtmlTextSize::Heading1:
            width = 20;
            break;
        case HtmlTextSize::Heading2:
            width = 14;
            break;
        case HtmlTextSize::Heading3:
        case HtmlTextSize::Large:
            width = 12;
            break;
        case HtmlTextSize::Small:
            width = 8;
            break;
        case HtmlTextSize::Normal:
        default:
            width = 10;
            break;
    }

    if (style.bold) {
        width += 1;
    }

    return width;
}

int HtmlPaginatorService::parseCssPx(const String &inputValue, int fallbackPx, int parentPx) const {
    String value = inputValue;
    value.trim();
    value.toLowerCase();

    if (value.isEmpty() || value == "auto") {
        return fallbackPx;
    }

    if (value.endsWith("%")) {
        value.remove(value.length() - 1);
        const int pct = value.toInt();
        if (pct > 0) {
            return (parentPx * pct) / 100;
        }

        return fallbackPx;
    }

    return value.toInt() > 0
        ? value.toInt()
        : fallbackPx;
}

bool HtmlPaginatorService::isBlockTag(const String &tagName) const {
    return tagName == "body"
        || tagName == "section"
        || tagName == "article"
        || tagName == "div"
        || tagName == "main"
        || tagName == "header"
        || tagName == "footer"
        || tagName == "table"
        || tagName == "tr"
        || tagName == "ul"
        || tagName == "ol"
        || tagName == "blockquote";
}

bool HtmlPaginatorService::isParagraphTag(const String &tagName) const {
    return tagName == "p";
}

bool HtmlPaginatorService::isHeadingTag(const String &tagName) const {
    return tagName == "h1"
        || tagName == "h2"
        || tagName == "h3"
        || tagName == "h4"
        || tagName == "h5"
        || tagName == "h6";
}

bool HtmlPaginatorService::isIgnoredContentTag(const String &tagName) const {
    return tagName == "script"
        || tagName == "style"
        || tagName == "svg"
        || tagName == "metadata";
}

String HtmlPaginatorService::decodeHtmlEntities(String value) const {
    value.replace("&nbsp;", " ");
    value.replace("&amp;", "&");
    value.replace("&quot;", "\"");
    value.replace("&apos;", "'");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    value.replace("&#39;", "'");
    value.replace("&#34;", "\"");
    return value;
}

String HtmlPaginatorService::resolveRelativePath(
    const String &baseFilePath,
    const String &inputRelativePath
) const {
    String relativePath = inputRelativePath;
    const int hashPos = relativePath.indexOf('#');
    if (hashPos >= 0) {
        relativePath = relativePath.substring(0, hashPos);
    }

    const int queryPos = relativePath.indexOf('?');
    if (queryPos >= 0) {
        relativePath = relativePath.substring(0, queryPos);
    }

    if (relativePath.isEmpty()) {
        return "";
    }

    if (relativePath.startsWith("data:")) {
        return relativePath;
    }

    if (relativePath[0] == '/') {
        return normalizePath(relativePath.substring(1));
    }

    const int slashPos = baseFilePath.lastIndexOf('/');
    const String baseDir = slashPos >= 0
        ? baseFilePath.substring(0, slashPos + 1)
        : "";

    return normalizePath(baseDir + relativePath);
}

String HtmlPaginatorService::normalizePath(const String &path) const {
    String result = "";
    int start = 0;
    const int len = path.length();

    while (start <= len) {
        int slashPos = path.indexOf('/', start);
        if (slashPos < 0) {
            slashPos = len;
        }

        String part = path.substring(start, slashPos);
        part.trim();

        if (!part.isEmpty() && part != ".") {
            if (part == "..") {
                const int lastSlash = result.lastIndexOf('/');
                if (lastSlash >= 0) {
                    result.remove(lastSlash);
                } else {
                    result = "";
                }
            } else {
                if (!result.isEmpty()) {
                    result += "/";
                }

                result += part;
            }
        }

        if (slashPos >= len) {
            break;
        }

        start = slashPos + 1;
    }

    return result;
}

String HtmlPaginatorService::tagNameFromSelector(String selector) const {
    selector.trim();
    selector.toLowerCase();

    const int spacePos = selector.indexOf(' ');
    if (spacePos >= 0) {
        selector = selector.substring(spacePos + 1);
        selector.trim();
    }

    const int classPos = selector.indexOf('.');
    if (classPos >= 0) {
        selector = selector.substring(0, classPos);
    }

    const int idPos = selector.indexOf('#');
    if (idPos >= 0) {
        selector = selector.substring(0, idPos);
    }

    selector.trim();
    return selector;
}
