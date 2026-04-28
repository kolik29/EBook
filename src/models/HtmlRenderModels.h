#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

enum class HtmlTextSize {
    Small,
    Normal,
    Large,
    Heading1,
    Heading2,
    Heading3
};

enum class HtmlTextAlign {
    Left,
    Center,
    Right
};

struct HtmlTextStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool hidden = false;
    HtmlTextSize size = HtmlTextSize::Normal;
    HtmlTextAlign align = HtmlTextAlign::Left;
    int indentPx = 0;
};

struct HtmlTextRun {
    String text;
    HtmlTextStyle style;
    int widthPx = 0;
};

enum class HtmlElementType {
    TextLine,
    Image,
    Rule,
    Spacer
};

struct HtmlRenderElement {
    HtmlElementType type = HtmlElementType::Spacer;
    HtmlTextStyle style;
    std::vector<HtmlTextRun> runs;
    String imagePath;
    String altText;
    int widthPx = 0;
    int heightPx = 0;
    int contentWidthPx = 0;
};

struct HtmlRenderPage {
    std::vector<HtmlRenderElement> elements;
};

using HtmlImageLoader = std::function<bool(const String &path, uint8_t *&outData, size_t &outSize)>;
