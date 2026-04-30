#include "DisplayDriver.h"

#include "../config/Constants.h"
#include "../services/BookFontMetrics.h"
#include "../services/BookTextCodec.h"

#include <esp_heap_caps.h>
#include <esp_rom_tjpgd.h>
#include <FS.h>
#include <SD.h>
#include <stdlib.h>
#include <string.h>

namespace {
    template <typename DisplayT>
    void printBookText(DisplayT &display, const String &text) {
        display.print(BookTextCodec::encodeUtf8ToCp1251(text));
    }

    HtmlTextStyle textStyle(HtmlTextSize size, bool bold = false, bool italic = false) {
        HtmlTextStyle style;
        style.size = size;
        style.bold = bold;
        style.italic = italic;
        return style;
    }

    struct JpegDrawContext {
        const uint8_t *data = nullptr;
        File *file = nullptr;
        size_t size = 0;
        size_t offset = 0;
        EpdDisplay *display = nullptr;
        int drawX = 0;
        int drawY = 0;
        int destWidth = 0;
        int destHeight = 0;
        int decodedWidth = 0;
        int decodedHeight = 0;
        uint8_t *grayBuffer = nullptr;
        bool collectGrayscale = false;
    };

    constexpr uint8_t kBayer8[8][8] = {
        {0, 48, 12, 60, 3, 51, 15, 63},
        {32, 16, 44, 28, 35, 19, 47, 31},
        {8, 56, 4, 52, 11, 59, 7, 55},
        {40, 24, 36, 20, 43, 27, 39, 23},
        {2, 50, 14, 62, 1, 49, 13, 61},
        {34, 18, 46, 30, 33, 17, 45, 29},
        {10, 58, 6, 54, 9, 57, 5, 53},
        {42, 26, 38, 22, 41, 25, 37, 21}
    };

    int clampInt(int value, int low, int high) {
        if (value < low) return low;
        if (value > high) return high;
        return value;
    }

    uint8_t *allocateImageBuffer(size_t size) {
        void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) {
            ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        }
        return static_cast<uint8_t *>(ptr);
    }

    uint8_t normalizeImageGray(uint8_t gray) {
        const int range = Constants::DISPLAY_IMAGE_WHITE_POINT
            - Constants::DISPLAY_IMAGE_BLACK_POINT;
        int value = gray;

        if (range > 0) {
            value = ((value - Constants::DISPLAY_IMAGE_BLACK_POINT) * 255) / range;
        }

        value = clampInt(value, 0, 255);
        value = 128 + ((value - 128) * Constants::DISPLAY_IMAGE_CONTRAST_PERCENT) / 100;
        value += Constants::DISPLAY_IMAGE_BRIGHTNESS_OFFSET;

        return static_cast<uint8_t>(clampInt(value, 0, 255));
    }

    void addDitherError(int16_t *errors, int index, int error) {
        const int value = static_cast<int>(errors[index]) + error;
        errors[index] = static_cast<int16_t>(clampInt(value, -4096, 4096));
    }

    uint8_t fourGrayLevelFromGray(int gray) {
        return static_cast<uint8_t>(clampInt((gray * 3 + 127) / 255, 0, 3));
    }

    uint8_t orderedFourGrayLevel(uint8_t gray, int x, int y) {
        const int normalized = normalizeImageGray(gray);
        const int offset =
            (static_cast<int>(kBayer8[y & 7][x & 7]) - 32)
                * Constants::DISPLAY_IMAGE_DITHER_STRENGTH;
        return fourGrayLevelFromGray(clampInt(normalized + offset, 0, 255));
    }

    void drawFourGrayPixel(EpdDisplay &display, int x, int y, uint8_t level) {
        if (level >= 3) {
            return;
        }

        display.drawGreyPixel(x, y, static_cast<uint8_t>(level << 6));
    }

    void storeGraySample(JpegDrawContext *ctx, int x, int y, uint8_t gray) {
        if (!ctx || !ctx->grayBuffer) {
            return;
        }
        if (x < 0 || y < 0 || x >= ctx->destWidth || y >= ctx->destHeight) {
            return;
        }

        const size_t idx = static_cast<size_t>(y) * ctx->destWidth + x;
        const uint8_t current = ctx->grayBuffer[idx];

        if (current == 0xFF) {
            ctx->grayBuffer[idx] = gray;
        } else if (gray < current) {
            ctx->grayBuffer[idx] = gray;
        } else {
            ctx->grayBuffer[idx] =
                static_cast<uint8_t>((static_cast<uint16_t>(current) * 3 + gray) / 4);
        }
    }

    void drawOrderedDitheredGrayscaleImage(
        EpdDisplay &display,
        const uint8_t *grayBuffer,
        int x,
        int y,
        int width,
        int height
    ) {
        if (!grayBuffer || width <= 0 || height <= 0) {
            return;
        }

        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                const size_t idx = static_cast<size_t>(row) * width + col;
                const uint8_t level = orderedFourGrayLevel(
                    grayBuffer[idx],
                    x + col,
                    y + row
                );

                drawFourGrayPixel(display, x + col, y + row, level);
            }
        }
    }

    void drawErrorDiffusedGrayscaleImage(
        EpdDisplay &display,
        const uint8_t *grayBuffer,
        int x,
        int y,
        int width,
        int height
    ) {
        if (!Constants::DISPLAY_IMAGE_USE_ERROR_DIFFUSION) {
            drawOrderedDitheredGrayscaleImage(display, grayBuffer, x, y, width, height);
            return;
        }

        if (!grayBuffer || width <= 0 || height <= 0) {
            return;
        }

        // The panel has 4 hardware gray levels; diffusion creates smoother perceived tones.
        const size_t errorBytes = static_cast<size_t>(width + 2) * sizeof(int16_t);
        int16_t *currentErrors = static_cast<int16_t *>(heap_caps_malloc(errorBytes, MALLOC_CAP_8BIT));
        int16_t *nextErrors = static_cast<int16_t *>(heap_caps_malloc(errorBytes, MALLOC_CAP_8BIT));

        if (!currentErrors || !nextErrors) {
            free(currentErrors);
            free(nextErrors);
            Serial.println("DISPLAY: image dither error buffer allocation failed, using ordered dither");
            drawOrderedDitheredGrayscaleImage(display, grayBuffer, x, y, width, height);
            return;
        }

        memset(currentErrors, 0, errorBytes);
        memset(nextErrors, 0, errorBytes);

        for (int row = 0; row < height; row++) {
            const bool leftToRight = (row & 1) == 0;

            for (int step = 0; step < width; step++) {
                const int col = leftToRight ? step : width - 1 - step;
                const size_t idx = static_cast<size_t>(row) * width + col;
                const int original = normalizeImageGray(grayBuffer[idx]);
                const int adjusted = clampInt(original + currentErrors[col + 1] / 16, 0, 255);
                const uint8_t level = fourGrayLevelFromGray(adjusted);
                const int rendered = (static_cast<int>(level) * 255) / 3;

                drawFourGrayPixel(display, x + col, y + row, level);

                const int error = adjusted - rendered;

                if (leftToRight) {
                    addDitherError(currentErrors, col + 2, error * 7);
                    addDitherError(nextErrors, col, error * 3);
                    addDitherError(nextErrors, col + 1, error * 5);
                    addDitherError(nextErrors, col + 2, error);
                } else {
                    addDitherError(currentErrors, col, error * 7);
                    addDitherError(nextErrors, col + 2, error * 3);
                    addDitherError(nextErrors, col + 1, error * 5);
                    addDitherError(nextErrors, col, error);
                }
            }

            int16_t *tmp = currentErrors;
            currentErrors = nextErrors;
            nextErrors = tmp;
            memset(nextErrors, 0, errorBytes);
        }

        free(currentErrors);
        free(nextErrors);
    }

    uint32_t jpegInput(esp_rom_tjpgd_dec_t *dec, uint8_t *buffer, uint32_t ndata) {
        JpegDrawContext *ctx = static_cast<JpegDrawContext *>(dec->device);
        if (!ctx || ctx->offset >= ctx->size) {
            return 0;
        }

        const uint32_t available = static_cast<uint32_t>(ctx->size - ctx->offset);
        const uint32_t toRead = ndata < available ? ndata : available;

        if (ctx->file) {
            size_t bytesRead = 0;

            if (buffer) {
                bytesRead = ctx->file->read(buffer, toRead);
            } else {
                const uint32_t nextPos = static_cast<uint32_t>(ctx->file->position() + toRead);
                bytesRead = ctx->file->seek(nextPos, SeekSet) ? toRead : 0;
            }

            ctx->offset += bytesRead;
            return static_cast<uint32_t>(bytesRead);
        }

        if (ctx->data && buffer) {
            memcpy(buffer, ctx->data + ctx->offset, toRead);
        }

        ctx->offset += toRead;
        return toRead;
    }

    uint32_t jpegOutput(esp_rom_tjpgd_dec_t *dec, void *bitmap, esp_rom_tjpgd_rect_t *rect) {
        JpegDrawContext *ctx = static_cast<JpegDrawContext *>(dec->device);
        if (!ctx || !ctx->display || !bitmap || !rect) {
            return 0;
        }

        if (ctx->destWidth <= 0 || ctx->destHeight <= 0
            || ctx->decodedWidth <= 0 || ctx->decodedHeight <= 0) {
            return 0;
        }

        const uint8_t *rgb = static_cast<const uint8_t *>(bitmap);
        const int blockW = rect->right - rect->left + 1;
        const int blockH = rect->bottom - rect->top + 1;

        for (int row = 0; row < blockH; row++) {
            for (int col = 0; col < blockW; col++) {
                const int sourceX = rect->left + col;
                const int sourceY = rect->top + row;
                const int x0 = ctx->drawX + (sourceX * ctx->destWidth) / ctx->decodedWidth;
                const int y0 = ctx->drawY + (sourceY * ctx->destHeight) / ctx->decodedHeight;
                int x1 = ctx->drawX + ((sourceX + 1) * ctx->destWidth) / ctx->decodedWidth;
                int y1 = ctx->drawY + ((sourceY + 1) * ctx->destHeight) / ctx->decodedHeight;

                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;

                if (x0 < ctx->drawX || x0 >= ctx->drawX + ctx->destWidth
                    || y0 < ctx->drawY || y0 >= ctx->drawY + ctx->destHeight) {
                    continue;
                }

                const size_t idx = (row * blockW + col) * 3;
                const uint8_t r = rgb[idx];
                const uint8_t g = rgb[idx + 1];
                const uint8_t b = rgb[idx + 2];
                const uint8_t gray = static_cast<uint8_t>((r * 30 + g * 59 + b * 11) / 100);

                if (ctx->collectGrayscale) {
                    const int localX0 = (sourceX * ctx->destWidth) / ctx->decodedWidth;
                    const int localY0 = (sourceY * ctx->destHeight) / ctx->decodedHeight;
                    int localX1 = ((sourceX + 1) * ctx->destWidth) / ctx->decodedWidth;
                    int localY1 = ((sourceY + 1) * ctx->destHeight) / ctx->decodedHeight;

                    if (localX1 <= localX0) localX1 = localX0 + 1;
                    if (localY1 <= localY0) localY1 = localY0 + 1;

                    for (int yy = localY0; yy < localY1 && yy < ctx->destHeight; yy++) {
                        for (int xx = localX0; xx < localX1 && xx < ctx->destWidth; xx++) {
                            storeGraySample(ctx, xx, yy, gray);
                        }
                    }

                    continue;
                }

                const uint8_t level = orderedFourGrayLevel(gray, x0, y0);

                if (level < 3) {
                    for (int yy = y0; yy < y1 && yy < ctx->drawY + ctx->destHeight; yy++) {
                        for (int xx = x0; xx < x1 && xx < ctx->drawX + ctx->destWidth; xx++) {
                            drawFourGrayPixel(*ctx->display, xx, yy, level);
                        }
                    }
                }
            }
        }

        return 1;
    }

    bool drawJpegContext(
        JpegDrawContext &ctx,
        int x,
        int y,
        int maxWidth,
        int maxHeight
    ) {
        if (!ctx.display || ctx.size == 0 || maxWidth <= 0 || maxHeight <= 0) {
            return false;
        }

        esp_rom_tjpgd_dec_t decoder;
        ctx.offset = 0;

        if (ctx.file) {
            ctx.file->seek(0, SeekSet);
        }

        void *work = malloc(4096);
        if (!work) {
            Serial.println("DISPLAY: jpeg work allocation failed");
            return false;
        }

        esp_rom_tjpgd_result_t result = esp_rom_tjpgd_prepare(
            &decoder,
            jpegInput,
            work,
            4096,
            &ctx
        );

        if (result != JDR_OK) {
            Serial.print("DISPLAY: jpeg prepare failed: ");
            Serial.println(static_cast<int>(result));
            free(work);
            return false;
        }

        const float fitScale = min(
            static_cast<float>(maxWidth) / static_cast<float>(decoder.width),
            static_cast<float>(maxHeight) / static_cast<float>(decoder.height)
        );
        const float targetScale = min(fitScale, Constants::DISPLAY_IMAGE_MAX_UPSCALE);

        int targetW = static_cast<int>(decoder.width * targetScale);
        int targetH = static_cast<int>(decoder.height * targetScale);

        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;
        if (targetW > maxWidth) targetW = maxWidth;
        if (targetH > maxHeight) targetH = maxHeight;

        uint8_t scale = 0;
        while (scale < 3) {
            const int candidateW = static_cast<int>(decoder.width >> (scale + 1));
            const int candidateH = static_cast<int>(decoder.height >> (scale + 1));

            if (candidateW < targetW || candidateH < targetH) {
                break;
            }

            scale++;
        }

        const int scaleDivisor = 1 << scale;
        ctx.decodedWidth = static_cast<int>((decoder.width + scaleDivisor - 1) >> scale);
        ctx.decodedHeight = static_cast<int>((decoder.height + scaleDivisor - 1) >> scale);
        ctx.destWidth = targetW;
        ctx.destHeight = targetH;
        ctx.drawX = x + (maxWidth - targetW) / 2;
        ctx.drawY = y + (maxHeight - targetH) / 2;

        const size_t pixelCount = static_cast<size_t>(targetW) * targetH;
        ctx.grayBuffer = allocateImageBuffer(pixelCount);

        if (ctx.grayBuffer) {
            memset(ctx.grayBuffer, 0xFF, pixelCount);
            ctx.collectGrayscale = true;
        } else {
            free(ctx.grayBuffer);
            ctx.grayBuffer = nullptr;
            ctx.collectGrayscale = false;
            Serial.println("DISPLAY: image grayscale buffer allocation failed, using direct ordered dither");
        }

        Serial.print("DISPLAY: jpeg ");
        Serial.print(static_cast<int>(decoder.width));
        Serial.print("x");
        Serial.print(static_cast<int>(decoder.height));
        Serial.print(" -> ");
        Serial.print(targetW);
        Serial.print("x");
        Serial.print(targetH);
        Serial.print(" scale=1/");
        Serial.print(scaleDivisor);
        Serial.print(" dither=");
        Serial.println(ctx.collectGrayscale ? "floyd-steinberg" : "ordered-direct");

        result = esp_rom_tjpgd_decomp(&decoder, jpegOutput, scale);
        free(work);

        if (result != JDR_OK) {
            Serial.print("DISPLAY: jpeg decode failed: ");
            Serial.println(static_cast<int>(result));
            free(ctx.grayBuffer);
            return false;
        }

        if (ctx.collectGrayscale) {
            drawErrorDiffusedGrayscaleImage(
                *ctx.display,
                ctx.grayBuffer,
                ctx.drawX,
                ctx.drawY,
                ctx.destWidth,
                ctx.destHeight
            );
        }

        free(ctx.grayBuffer);

        return true;
    }
}

DisplayDriver::DisplayDriver(
    int pwrPin,
    int busyPin,
    int rstPin,
    int dcPin,
    int csPin,
    int clkPin,
    int dinPin
)
    : m_spi(HSPI),
      m_display(GxEPD2_750_T7(csPin, dcPin, rstPin, busyPin)),
      m_displayBw(m_display.epd2),
      m_pwrPin(pwrPin),
      m_busyPin(busyPin),
      m_rstPin(rstPin),
      m_dcPin(dcPin),
      m_csPin(csPin),
      m_clkPin(clkPin),
      m_dinPin(dinPin) {
}

void DisplayDriver::begin() {
    Serial.println("DISPLAY: begin");

    pinMode(m_pwrPin, OUTPUT);
    powerOn();

    m_spi.begin(m_clkPin, -1, m_dinPin, m_csPin);

    m_display.epd2.selectSPI(
        m_spi,
        SPISettings(4000000, MSBFIRST, SPI_MODE0)
    );

    m_display.init(115200, true, 2, false);
    m_display.setRotation(1);
    m_display.setTextWrap(false);
    m_displayBw.setRotation(1);
    m_displayBw.setTextWrap(false);

    Serial.println("DISPLAY: initialized");
}

void DisplayDriver::showMessage(const String &title, const String &message) {
    wake();
    m_display.setTextWrap(false);

    m_display.setFullWindow();
    m_display.firstPage();

    do {
        m_display.fillScreen(GxEPD_WHITE);
        m_display.setTextColor(GxEPD_BLACK);

        m_display.setFont(BookFontMetrics::fontForStyle(textStyle(HtmlTextSize::Normal, true)));
        m_display.setCursor(40, 60);
        printBookText(m_display, title);

        m_display.setFont(BookFontMetrics::fontForStyle(textStyle(HtmlTextSize::Normal)));
        renderWrappedText(message, 40, 110, 52, 24);
    } while (m_display.nextPage());

    m_display.hibernate();
    m_forceNextPageFullRefresh = true;
}

void DisplayDriver::showTextPage(const String &title, const String &text, int page, int totalPages) {
    powerOn();
    m_displayBw.setTextWrap(false);

    const bool canUsePartial =
        Constants::DISPLAY_USE_PARTIAL_PAGE_UPDATES
        && m_displayBw.epd2.hasPartialUpdate
        && !m_forceNextPageFullRefresh;
    const bool needFullRefresh =
        !canUsePartial
        || m_partialUpdateCounter >= Constants::DISPLAY_FULL_REFRESH_AFTER_PARTIALS;
    const bool usePartialRefresh = canUsePartial && !needFullRefresh;

    if (usePartialRefresh) {
        Serial.println("DISPLAY: partial update");

        m_displayBw.setPartialWindow(
            0,
            0,
            m_displayBw.width(),
            m_displayBw.height()
        );

        m_partialUpdateCounter++;
    } else {
        Serial.println("DISPLAY: full update");

        m_displayBw.setFullWindow();
        m_partialUpdateCounter = 0;
    }

    m_displayBw.firstPage();

    const int screenW = m_displayBw.width();
    const int screenH = m_displayBw.height();

    const int marginLeft = 14;
    const int marginRight = 14;

    const int titleY = 34;
    const int titleLineY = 48;

    const int textX = 14;
    const int textY = 72;
    const int lineHeight = 24;
    const int maxLines = 29;

    const int footerLineY = screenH - 30;
    const int footerTextY = screenH - 7;

    do {
        m_displayBw.fillScreen(GxEPD_WHITE);
        m_displayBw.setTextColor(GxEPD_BLACK);

        // Заголовок
        const HtmlTextStyle titleStyle = textStyle(HtmlTextSize::Normal, true);
        const HtmlTextStyle bodyStyle = textStyle(HtmlTextSize::Normal);

        m_displayBw.setFont(BookFontMetrics::fontForStyle(titleStyle));
        m_displayBw.setCursor(marginLeft, titleY);
        printBookText(m_displayBw, title);

        m_displayBw.drawLine(
            marginLeft,
            titleLineY,
            screenW - marginRight,
            titleLineY,
            GxEPD_BLACK
        );

        // Основной текст
        m_displayBw.setFont(BookFontMetrics::fontForStyle(bodyStyle));
        renderPreformattedTextBw(text, textX, textY, lineHeight, maxLines);

        // Подвал
        m_displayBw.drawLine(
            marginLeft,
            footerLineY,
            screenW - marginRight,
            footerLineY,
            GxEPD_BLACK
        );

        m_displayBw.setFont(BookFontMetrics::fontForStyle(bodyStyle));
        m_displayBw.setCursor(marginLeft, footerTextY);
        m_displayBw.print("Page ");
        m_displayBw.print(page);
        m_displayBw.print(" / ");
        m_displayBw.print(totalPages);
    } while (m_displayBw.nextPage());

    m_forceNextPageFullRefresh = false;
    m_displayBw.powerOff();
}

void DisplayDriver::showHtmlPage(
    const String &title,
    const HtmlRenderPage &htmlPage,
    int page,
    int totalPages,
    HtmlImageLoader imageLoader
) {
    if (!pageHasImages(htmlPage)) {
        showHtmlPageBw(title, htmlPage, page, totalPages);
        return;
    }

    std::vector<CachedHtmlImage> imageCache;
    preloadHtmlImages(htmlPage, imageLoader, imageCache);

    powerOn();
    m_display.setTextWrap(false);

    Serial.println("DISPLAY: html 4-gray full update");

    m_display.setFullWindow();
    m_partialUpdateCounter = 0;

    m_display.firstPage();

    const int screenW = m_display.width();
    const int screenH = m_display.height();

    const int marginLeft = 14;
    const int marginRight = 14;

    const int titleY = 34;
    const int titleLineY = 48;

    const int contentX = 14;
    const int contentY = 66;
    const int contentWidth = screenW - marginLeft - marginRight;

    const int footerLineY = screenH - 30;
    const int footerTextY = screenH - 7;

    do {
        m_display.fillScreen(GxEPD_WHITE);
        m_display.setTextColor(GxEPD_BLACK);

        const HtmlTextStyle titleStyle = textStyle(HtmlTextSize::Normal, true);
        const HtmlTextStyle bodyStyle = textStyle(HtmlTextSize::Normal);

        m_display.setFont(BookFontMetrics::fontForStyle(titleStyle));
        m_display.setCursor(marginLeft, titleY);
        printBookText(m_display, title);

        m_display.drawLine(
            marginLeft,
            titleLineY,
            screenW - marginRight,
            titleLineY,
            GxEPD_BLACK
        );

        int cursorY = contentY;

        for (const HtmlRenderElement &element : htmlPage.elements) {
            if (cursorY >= footerLineY - 4) {
                break;
            }

            renderHtmlElement(element, contentX, cursorY, contentWidth, imageCache);
        }

        m_display.drawLine(
            marginLeft,
            footerLineY,
            screenW - marginRight,
            footerLineY,
            GxEPD_BLACK
        );

        m_display.setFont(BookFontMetrics::fontForStyle(bodyStyle));
        m_display.setCursor(marginLeft, footerTextY);
        m_display.print("Page ");
        m_display.print(page);
        m_display.print(" / ");
        m_display.print(totalPages);
    } while (m_display.nextPage());

    freeHtmlImageCache(imageCache);

    // Image-heavy 4-gray pages can leave visible ghosting if the next text page
    // is drawn with a partial update. Force one full refresh after the image page.
    m_forceNextPageFullRefresh = true;
    m_display.powerOff();
}

void DisplayDriver::powerOn() {
    digitalWrite(m_pwrPin, HIGH);
    delay(20);
}

void DisplayDriver::renderWrappedText(const String &text, int x, int y, int maxCharsPerLine, int lineHeight) {
    int cursorY = y;
    int start = 0;

    while (start < text.length()) {
        const String remaining = text.substring(start);
        String prefix = BookTextCodec::utf8PrefixByCodepoints(
            remaining,
            maxCharsPerLine
        );

        if (prefix.isEmpty()) {
            prefix = text.substring(start, start + 1);
        }

        int end = start + prefix.length();

        if (end >= text.length()) {
            end = text.length();
        } else {
            int spaceIndex = text.lastIndexOf(' ', end);

            if (spaceIndex > start) {
                end = spaceIndex;
            }
        }

        String line = text.substring(start, end);
        line.trim();

        if (line.length() > 0) {
            m_display.setCursor(x, cursorY);
            printBookText(m_display, line);
            cursorY += lineHeight;
        }

        start = end;
        while (start < text.length() && text[start] == ' ') {
            start++;
        }

        if (cursorY > 430) {
            break;
        }
    }
}

void DisplayDriver::renderPreformattedText(
    const String &text,
    int x,
    int y,
    int lineHeight,
    int maxLines
) {
    int cursorY = y;
    int lineCount = 0;
    int start = 0;

    while (start <= text.length() && lineCount < maxLines) {
        int end = text.indexOf('\n', start);

        if (end < 0) {
            end = text.length();
        }

        String line = text.substring(start, end);
        line.trim();

        if (!line.isEmpty()) {
            m_display.setCursor(x, cursorY);
            printBookText(m_display, line);
        }

        cursorY += lineHeight;
        lineCount++;

        if (end >= text.length()) {
            break;
        }

        start = end + 1;
    }
}

void DisplayDriver::renderPreformattedTextBw(
    const String &text,
    int x,
    int y,
    int lineHeight,
    int maxLines
) {
    int cursorY = y;
    int lineCount = 0;
    int start = 0;

    while (start <= text.length() && lineCount < maxLines) {
        int end = text.indexOf('\n', start);

        if (end < 0) {
            end = text.length();
        }

        String line = text.substring(start, end);
        line.trim();

        if (!line.isEmpty()) {
            m_displayBw.setCursor(x, cursorY);
            printBookText(m_displayBw, line);
        }

        cursorY += lineHeight;
        lineCount++;

        if (end >= text.length()) {
            break;
        }

        start = end + 1;
    }
}

bool DisplayDriver::pageHasImages(const HtmlRenderPage &htmlPage) const {
    for (const HtmlRenderElement &element : htmlPage.elements) {
        if (element.type == HtmlElementType::Image) {
            return true;
        }
    }

    return false;
}

void DisplayDriver::preloadHtmlImages(
    const HtmlRenderPage &htmlPage,
    HtmlImageLoader imageLoader,
    std::vector<CachedHtmlImage> &imageCache
) {
    imageCache.clear();

    if (!imageLoader) {
        return;
    }

    for (const HtmlRenderElement &element : htmlPage.elements) {
        if (element.type != HtmlElementType::Image || element.imagePath.isEmpty()) {
            continue;
        }

        if (findCachedHtmlImage(imageCache, element.imagePath)) {
            continue;
        }

        CachedHtmlImage cached;
        cached.path = element.imagePath;
        cached.loaded = imageLoader(
            cached.path,
            cached.data,
            cached.size,
            cached.localFilePath
        );

        if (!cached.loaded) {
            cached.data = nullptr;
            cached.size = 0;
            cached.localFilePath = "";
        }

        imageCache.push_back(cached);
    }
}

const DisplayDriver::CachedHtmlImage *DisplayDriver::findCachedHtmlImage(
    const std::vector<CachedHtmlImage> &imageCache,
    const String &path
) const {
    for (const CachedHtmlImage &cached : imageCache) {
        if (cached.path == path) {
            return &cached;
        }
    }

    return nullptr;
}

void DisplayDriver::freeHtmlImageCache(std::vector<CachedHtmlImage> &imageCache) {
    for (CachedHtmlImage &cached : imageCache) {
        free(cached.data);
        cached.data = nullptr;
        cached.size = 0;
        cached.loaded = false;
    }

    imageCache.clear();
}

void DisplayDriver::showHtmlPageBw(
    const String &title,
    const HtmlRenderPage &htmlPage,
    int page,
    int totalPages
) {
    powerOn();
    m_displayBw.setTextWrap(false);

    const bool canUsePartial =
        Constants::DISPLAY_USE_PARTIAL_PAGE_UPDATES
        && m_displayBw.epd2.hasPartialUpdate
        && !m_forceNextPageFullRefresh;
    const bool needFullRefresh =
        !canUsePartial
        || m_partialUpdateCounter >= Constants::DISPLAY_FULL_REFRESH_AFTER_PARTIALS;
    const bool usePartialRefresh = canUsePartial && !needFullRefresh;

    if (usePartialRefresh) {
        Serial.println("DISPLAY: html bw partial update");

        m_displayBw.setPartialWindow(
            0,
            0,
            m_displayBw.width(),
            m_displayBw.height()
        );

        m_partialUpdateCounter++;
    } else {
        Serial.println("DISPLAY: html bw full update");

        m_displayBw.setFullWindow();
        m_partialUpdateCounter = 0;
    }

    m_displayBw.firstPage();

    const int screenW = m_displayBw.width();
    const int screenH = m_displayBw.height();

    const int marginLeft = 14;
    const int marginRight = 14;

    const int titleY = 34;
    const int titleLineY = 48;

    const int contentX = 14;
    const int contentY = 66;
    const int contentWidth = screenW - marginLeft - marginRight;

    const int footerLineY = screenH - 30;
    const int footerTextY = screenH - 7;

    do {
        m_displayBw.fillScreen(GxEPD_WHITE);
        m_displayBw.setTextColor(GxEPD_BLACK);

        const HtmlTextStyle titleStyle = textStyle(HtmlTextSize::Normal, true);
        const HtmlTextStyle bodyStyle = textStyle(HtmlTextSize::Normal);

        m_displayBw.setFont(BookFontMetrics::fontForStyle(titleStyle));
        m_displayBw.setCursor(marginLeft, titleY);
        printBookText(m_displayBw, title);

        m_displayBw.drawLine(
            marginLeft,
            titleLineY,
            screenW - marginRight,
            titleLineY,
            GxEPD_BLACK
        );

        int cursorY = contentY;

        for (const HtmlRenderElement &element : htmlPage.elements) {
            if (cursorY >= footerLineY - 4) {
                break;
            }

            renderHtmlElementBw(element, contentX, cursorY, contentWidth);
        }

        m_displayBw.drawLine(
            marginLeft,
            footerLineY,
            screenW - marginRight,
            footerLineY,
            GxEPD_BLACK
        );

        m_displayBw.setFont(BookFontMetrics::fontForStyle(bodyStyle));
        m_displayBw.setCursor(marginLeft, footerTextY);
        m_displayBw.print("Page ");
        m_displayBw.print(page);
        m_displayBw.print(" / ");
        m_displayBw.print(totalPages);
    } while (m_displayBw.nextPage());

    m_forceNextPageFullRefresh = false;
    m_displayBw.powerOff();
}

void DisplayDriver::renderHtmlElement(
    const HtmlRenderElement &element,
    int x,
    int &cursorY,
    int maxWidth,
    const std::vector<CachedHtmlImage> &imageCache
) {
    switch (element.type) {
        case HtmlElementType::TextLine:
            renderHtmlTextLine(element, x, cursorY, maxWidth);
            cursorY += element.heightPx;
            break;

        case HtmlElementType::Image:
            renderHtmlImage(element, x, cursorY, maxWidth, imageCache);
            cursorY += element.heightPx;
            break;

        case HtmlElementType::Rule:
            m_display.drawLine(x, cursorY + 8, x + maxWidth, cursorY + 8, GxEPD_BLACK);
            cursorY += element.heightPx;
            break;

        case HtmlElementType::Spacer:
        default:
            cursorY += element.heightPx;
            break;
    }
}

void DisplayDriver::renderHtmlElementBw(
    const HtmlRenderElement &element,
    int x,
    int &cursorY,
    int maxWidth
) {
    switch (element.type) {
        case HtmlElementType::TextLine:
            renderHtmlTextLineBw(element, x, cursorY, maxWidth);
            cursorY += element.heightPx;
            break;

        case HtmlElementType::Rule:
            m_displayBw.drawLine(x, cursorY + 8, x + maxWidth, cursorY + 8, GxEPD_BLACK);
            cursorY += element.heightPx;
            break;

        case HtmlElementType::Image:
        case HtmlElementType::Spacer:
        default:
            cursorY += element.heightPx;
            break;
    }
}

void DisplayDriver::renderHtmlTextLine(
    const HtmlRenderElement &element,
    int x,
    int y,
    int maxWidth
) {
    const int availableWidth = maxWidth - element.style.indentPx;
    int lineX = x + element.style.indentPx;

    if (element.style.align == HtmlTextAlign::Center && element.widthPx < availableWidth) {
        lineX += (availableWidth - element.widthPx) / 2;
    } else if (element.style.align == HtmlTextAlign::Right && element.widthPx < availableWidth) {
        lineX += availableWidth - element.widthPx;
    }

    const int baselineY = y + baselineOffsetForStyle(element.style);
    int cursorX = lineX;
    int justifyExtraPx = 0;
    int justifySpaces = 0;

    if (element.justify
        && element.style.align == HtmlTextAlign::Left
        && element.widthPx < availableWidth) {
        for (const HtmlTextRun &run : element.runs) {
            if (run.text == " ") {
                justifySpaces++;
            }
        }

        if (justifySpaces > 0) {
            justifyExtraPx = availableWidth - element.widthPx;
        }
    }

    for (const HtmlTextRun &run : element.runs) {
        if (run.text.isEmpty()) {
            continue;
        }

        int extraAdvance = 0;
        if (justifyExtraPx > 0 && justifySpaces > 0 && run.text == " ") {
            extraAdvance = justifyExtraPx / justifySpaces;
            justifyExtraPx -= extraAdvance;
            justifySpaces--;
        }

        m_display.setFont(fontForStyle(run.style));
        m_display.setCursor(cursorX, baselineY);
        printBookText(m_display, run.text);

        if (run.style.underline) {
            m_display.drawLine(
                cursorX,
                baselineY + 3,
                cursorX + run.widthPx + extraAdvance,
                baselineY + 3,
                GxEPD_BLACK
            );
        }

        cursorX += run.widthPx + extraAdvance;
    }
}

void DisplayDriver::renderHtmlTextLineBw(
    const HtmlRenderElement &element,
    int x,
    int y,
    int maxWidth
) {
    const int availableWidth = maxWidth - element.style.indentPx;
    int lineX = x + element.style.indentPx;

    if (element.style.align == HtmlTextAlign::Center && element.widthPx < availableWidth) {
        lineX += (availableWidth - element.widthPx) / 2;
    } else if (element.style.align == HtmlTextAlign::Right && element.widthPx < availableWidth) {
        lineX += availableWidth - element.widthPx;
    }

    const int baselineY = y + baselineOffsetForStyle(element.style);
    int cursorX = lineX;
    int justifyExtraPx = 0;
    int justifySpaces = 0;

    if (element.justify
        && element.style.align == HtmlTextAlign::Left
        && element.widthPx < availableWidth) {
        for (const HtmlTextRun &run : element.runs) {
            if (run.text == " ") {
                justifySpaces++;
            }
        }

        if (justifySpaces > 0) {
            justifyExtraPx = availableWidth - element.widthPx;
        }
    }

    for (const HtmlTextRun &run : element.runs) {
        if (run.text.isEmpty()) {
            continue;
        }

        int extraAdvance = 0;
        if (justifyExtraPx > 0 && justifySpaces > 0 && run.text == " ") {
            extraAdvance = justifyExtraPx / justifySpaces;
            justifyExtraPx -= extraAdvance;
            justifySpaces--;
        }

        m_displayBw.setFont(fontForStyle(run.style));
        m_displayBw.setCursor(cursorX, baselineY);
        printBookText(m_displayBw, run.text);

        if (run.style.underline) {
            m_displayBw.drawLine(
                cursorX,
                baselineY + 3,
                cursorX + run.widthPx + extraAdvance,
                baselineY + 3,
                GxEPD_BLACK
            );
        }

        cursorX += run.widthPx + extraAdvance;
    }
}

void DisplayDriver::renderHtmlImage(
    const HtmlRenderElement &element,
    int x,
    int y,
    int maxWidth,
    const std::vector<CachedHtmlImage> &imageCache
) {
    const int imageHeight = element.heightPx > 10
        ? element.heightPx - 10
        : element.heightPx;
    const int availableWidth = maxWidth - element.style.indentPx;

    int imageWidth = element.widthPx > 0 ? element.widthPx : availableWidth;
    if (imageWidth > availableWidth) {
        imageWidth = availableWidth;
    }

    int imageX = x + element.style.indentPx;
    if (element.style.align == HtmlTextAlign::Center && imageWidth < availableWidth) {
        imageX += (availableWidth - imageWidth) / 2;
    } else if (element.style.align == HtmlTextAlign::Right && imageWidth < availableWidth) {
        imageX += availableWidth - imageWidth;
    }

    const CachedHtmlImage *cached = findCachedHtmlImage(imageCache, element.imagePath);

    if (!cached || !cached->loaded) {
        renderImagePlaceholder(element, imageX, y, imageWidth, imageHeight, "image not found");
        return;
    }

    bool rendered = false;

    if (cached->data && cached->size >= 2 && cached->data[0] == 0xFF && cached->data[1] == 0xD8) {
        rendered = drawJpegData(
            cached->data,
            cached->size,
            imageX,
            y,
            imageWidth,
            imageHeight
        );
    } else if (!cached->localFilePath.isEmpty()) {
        rendered = drawJpegFile(
            cached->localFilePath,
            imageX,
            y,
            imageWidth,
            imageHeight
        );
    }

    if (!rendered) {
        renderImagePlaceholder(element, imageX, y, imageWidth, imageHeight, "unsupported image");
    }
}

void DisplayDriver::renderImagePlaceholder(
    const HtmlRenderElement &element,
    int x,
    int y,
    int width,
    int height,
    const String &message
) {
    m_display.drawRect(x, y, width, height, GxEPD_BLACK);
    m_display.drawLine(x, y, x + width, y + height, GxEPD_BLACK);
    m_display.drawLine(x + width, y, x, y + height, GxEPD_BLACK);

    m_display.setFont(BookFontMetrics::fontForStyle(textStyle(HtmlTextSize::Normal)));
    m_display.setCursor(x + 10, y + 24);
    printBookText(m_display, message);

    if (!element.altText.isEmpty() && height > 52) {
        m_display.setCursor(x + 10, y + 48);
        printBookText(m_display, BookTextCodec::utf8PrefixByCodepoints(element.altText, 52));
    }
}

bool DisplayDriver::drawJpegData(
    const uint8_t *data,
    size_t size,
    int x,
    int y,
    int maxWidth,
    int maxHeight
) {
    if (!data || size == 0 || maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    JpegDrawContext ctx;
    ctx.data = data;
    ctx.size = size;
    ctx.display = &m_display;

    return drawJpegContext(ctx, x, y, maxWidth, maxHeight);
}

bool DisplayDriver::drawJpegFile(
    const String &path,
    int x,
    int y,
    int maxWidth,
    int maxHeight
) {
    if (path.isEmpty() || maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    File file = SD.open(path, "r");
    if (!file || file.isDirectory()) {
        Serial.print("DISPLAY: jpeg file open failed: ");
        Serial.println(path);
        return false;
    }

    JpegDrawContext ctx;
    ctx.file = &file;
    ctx.size = file.size();
    ctx.display = &m_display;

    const bool ok = drawJpegContext(ctx, x, y, maxWidth, maxHeight);
    file.close();

    return ok;
}

const GFXfont *DisplayDriver::fontForStyle(const HtmlTextStyle &style) const {
    return BookFontMetrics::fontForStyle(style);
}

int DisplayDriver::baselineOffsetForStyle(const HtmlTextStyle &style) const {
    switch (style.size) {
        case HtmlTextSize::Heading1:
            return 34;
        case HtmlTextSize::Heading2:
            return 27;
        case HtmlTextSize::Heading3:
        case HtmlTextSize::Large:
            return 24;
        case HtmlTextSize::Small:
            return 14;
        case HtmlTextSize::Normal:
        default:
            return 18;
    }
}

void DisplayDriver::wake() {
    powerOn();

    m_display.init(115200, true, 2, false);
    m_display.setRotation(1);
    m_display.setTextWrap(false);
    m_displayBw.setRotation(1);
    m_displayBw.setTextWrap(false);
}
