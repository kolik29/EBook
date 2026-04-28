#include "BookFontMetrics.h"

#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifItalic18pt7b.h>
#include <Fonts/FreeSerifBoldItalic18pt7b.h>
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
            if (style.bold && style.italic) return &FreeSerifBoldItalic18pt7b;
            if (style.bold) return &FreeSerifBold18pt7b;
            if (style.italic) return &FreeSerifItalic18pt7b;
            return &FreeSerif18pt7b;
        }

        if (style.size == HtmlTextSize::Heading2
            || style.size == HtmlTextSize::Heading3
            || style.size == HtmlTextSize::Large) {
            if (style.bold && style.italic) return &FreeSerifBoldItalic12pt7b;
            if (style.bold) return &FreeSerifBold12pt7b;
            if (style.italic) return &FreeSerifItalic12pt7b;
            return &FreeSerif12pt7b;
        }

        if (style.bold && style.italic) return &FreeSerifBoldItalic9pt7b;
        if (style.bold) return &FreeSerifBold9pt7b;
        if (style.italic) return &FreeSerifItalic9pt7b;
        return &FreeSerif9pt7b;
    }

    int textWidth(const String &text, const HtmlTextStyle &style) {
        const GFXfont *font = fontForStyle(style);
        const int fallbackPx = baseFallbackCharWidth(style);
        int width = 0;

        for (int i = 0; i < text.length(); i++) {
            const uint8_t c = static_cast<uint8_t>(text[i]);

            if (c == 0xC2 && i + 1 < text.length()
                && static_cast<uint8_t>(text[i + 1]) == 0xA0) {
                width += glyphAdvance(font, ' ', fallbackPx);
                i++;
                continue;
            }

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
