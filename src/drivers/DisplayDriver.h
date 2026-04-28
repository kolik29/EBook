#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <vector>

#include <GxEPD2_4G_4G.h>
#include <GxEPD2_4G_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoOblique12pt7b.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoOblique18pt7b.h>
#include <Fonts/FreeMonoBoldOblique18pt7b.h>

#include "../models/HtmlRenderModels.h"

// Для твоей панели 075BN-T7-D2.
using EpdDisplay = GxEPD2_4G_4G<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT / 2>;
using EpdDisplayBw = GxEPD2_4G_BW_R<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT / 2>;

class DisplayDriver {
public:
    DisplayDriver(
        int pwrPin,
        int busyPin,
        int rstPin,
        int dcPin,
        int csPin,
        int clkPin,
        int dinPin
    );

    void begin();

    void showMessage(const String &title, const String &message);
    void showTextPage(const String &title, const String &text, int page, int totalPages);
    void showHtmlPage(
        const String &title,
        const HtmlRenderPage &htmlPage,
        int page,
        int totalPages,
        HtmlImageLoader imageLoader
    );

private:
    struct CachedHtmlImage {
        String path;
        String localFilePath;
        uint8_t *data = nullptr;
        size_t size = 0;
        bool loaded = false;
    };

    SPIClass m_spi;
    EpdDisplay m_display;
    EpdDisplayBw m_displayBw;

    int m_pwrPin;
    int m_busyPin;
    int m_rstPin;
    int m_dcPin;
    int m_csPin;
    int m_clkPin;
    int m_dinPin;
    int m_partialUpdateCounter = 0;
    bool m_forceNextPageFullRefresh = true;

    void powerOn();
    void renderWrappedText(const String &text, int x, int y, int maxCharsPerLine, int lineHeight);
    void renderPreformattedText(const String &text, int x, int y, int lineHeight, int maxLines);
    void renderPreformattedTextBw(const String &text, int x, int y, int lineHeight, int maxLines);
    bool pageHasImages(const HtmlRenderPage &htmlPage) const;
    void preloadHtmlImages(
        const HtmlRenderPage &htmlPage,
        HtmlImageLoader imageLoader,
        std::vector<CachedHtmlImage> &imageCache
    );
    const CachedHtmlImage *findCachedHtmlImage(
        const std::vector<CachedHtmlImage> &imageCache,
        const String &path
    ) const;
    void freeHtmlImageCache(std::vector<CachedHtmlImage> &imageCache);
    void showHtmlPageBw(const String &title, const HtmlRenderPage &htmlPage, int page, int totalPages);
    void renderHtmlElement(
        const HtmlRenderElement &element,
        int x,
        int &cursorY,
        int maxWidth,
        const std::vector<CachedHtmlImage> &imageCache
    );
    void renderHtmlElementBw(const HtmlRenderElement &element, int x, int &cursorY, int maxWidth);
    void renderHtmlTextLine(const HtmlRenderElement &element, int x, int y, int maxWidth);
    void renderHtmlTextLineBw(const HtmlRenderElement &element, int x, int y, int maxWidth);
    void renderHtmlImage(
        const HtmlRenderElement &element,
        int x,
        int y,
        int maxWidth,
        const std::vector<CachedHtmlImage> &imageCache
    );
    void renderImagePlaceholder(const HtmlRenderElement &element, int x, int y, int width, int height, const String &message);
    bool drawJpegData(const uint8_t *data, size_t size, int x, int y, int maxWidth, int maxHeight);
    bool drawJpegFile(const String &path, int x, int y, int maxWidth, int maxHeight);
    const GFXfont *fontForStyle(const HtmlTextStyle &style) const;
    int baselineOffsetForStyle(const HtmlTextStyle &style) const;
    void wake();
};
