#include "BookFontMetrics.h"

#include "../fonts/BookSerifCp1251.h"
#include "BookTextCodec.h"

#include <pgmspace.h>

namespace {
    constexpr int BOOK_SPACE_EXTRA_PX = 2;

    int baseFallbackCharWidth(const HtmlTextStyle &style) {
        switch (style.size) {
            case HtmlTextSize::Heading1:
                return 18;
            case HtmlTextSize::Heading2:
                return 13;
            case HtmlTextSize::Heading3:
            case HtmlTextSize::Large:
                return 12;
            case HtmlTextSize::Small:
                return 7;
            case HtmlTextSize::Normal:
            default:
                return 8;
        }
    }

    int glyphAdvance(const GFXfont *font, uint8_t c, int fallbackPx) {
        if (!font) {
            return fallbackPx;
        }

        if (c == '\t') {
            c = ' ';
        }

        const uint16_t first = pgm_read_word(&font->first);
        const uint16_t last = pgm_read_word(&font->last);

        if (c < first || c > last) {
            return fallbackPx;
        }

        const GFXglyph *glyphs =
            reinterpret_cast<const GFXglyph *>(pgm_read_ptr(&font->glyph));
        const GFXglyph *glyph = glyphs + (c - first);
        int advance = pgm_read_byte(&glyph->xAdvance);

        if (c == ' ') {
            advance += BOOK_SPACE_EXTRA_PX;
        }

        return advance > 0 ? advance : fallbackPx;
    }
}

namespace BookFontMetrics {
    const GFXfont *fontForStyle(const HtmlTextStyle &style) {
        if (style.size == HtmlTextSize::Heading1) {
            if (style.bold && style.italic) return &BookSerifCp1251BoldItalic18pt;
            if (style.bold) return &BookSerifCp1251Bold18pt;
            if (style.italic) return &BookSerifCp1251Italic18pt;
            return &BookSerifCp1251Regular18pt;
        }

        if (style.size == HtmlTextSize::Heading2
            || style.size == HtmlTextSize::Heading3
            || style.size == HtmlTextSize::Large) {
            if (style.bold && style.italic) return &BookSerifCp1251BoldItalic12pt;
            if (style.bold) return &BookSerifCp1251Bold12pt;
            if (style.italic) return &BookSerifCp1251Italic12pt;
            return &BookSerifCp1251Regular12pt;
        }

        if (style.bold && style.italic) return &BookSerifCp1251BoldItalic9pt;
        if (style.bold) return &BookSerifCp1251Bold9pt;
        if (style.italic) return &BookSerifCp1251Italic9pt;
        return &BookSerifCp1251Regular9pt;
    }

    int textWidth(const String &text, const HtmlTextStyle &style) {
        const GFXfont *font = fontForStyle(style);
        const int fallbackPx = baseFallbackCharWidth(style);
        int width = 0;

        int index = 0;

        while (index < text.length()) {
            uint32_t codepoint = 0;
            const bool validUtf8 = BookTextCodec::readUtf8Codepoint(text, index, codepoint);

            if (codepoint == '\n' || codepoint == '\r') {
                continue;
            }

            const uint8_t c = BookTextCodec::glyphCodeForCodepoint(codepoint, validUtf8);
            width += glyphAdvance(font, c, fallbackPx);
        }

        return width;
    }

    int spaceWidth(const HtmlTextStyle &style) {
        return glyphAdvance(fontForStyle(style), ' ', baseFallbackCharWidth(style));
    }

    int fallbackCharWidth(const HtmlTextStyle &style) {
        return baseFallbackCharWidth(style);
    }
}
