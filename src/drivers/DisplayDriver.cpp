#include "../utils/DebugLog.h"
#include "DisplayDriver.h"

#include "../config/Constants.h"
#include "../services/BookFontMetrics.h"
#include "../services/BookTextCodec.h"

#include <esp_heap_caps.h>
#include <esp_rom_tjpgd.h>
#include <FS.h>
#include <rom/miniz.h>
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

    struct PngDecodeInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t bitDepth = 0;
        uint8_t colorType = 0;
        uint8_t interlace = 0;
        std::vector<uint8_t> idat;
        const uint8_t *palette = nullptr;
        size_t paletteSize = 0;
        const uint8_t *transparency = nullptr;
        size_t transparencySize = 0;
    };

    uint32_t readBe32(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24)
            | (static_cast<uint32_t>(p[1]) << 16)
            | (static_cast<uint32_t>(p[2]) << 8)
            | static_cast<uint32_t>(p[3]);
    }

    bool pngChunkTypeEquals(const uint8_t *type, const char *name) {
        return type[0] == static_cast<uint8_t>(name[0])
            && type[1] == static_cast<uint8_t>(name[1])
            && type[2] == static_cast<uint8_t>(name[2])
            && type[3] == static_cast<uint8_t>(name[3]);
    }

    int pngBytesPerPixel(uint8_t colorType) {
        switch (colorType) {
            case 0: return 1; // grayscale
            case 2: return 3; // RGB
            case 3: return 1; // palette index
            case 4: return 2; // grayscale + alpha
            case 6: return 4; // RGBA
            default: return 0;
        }
    }

    uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
        const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
        const int pa = abs(p - static_cast<int>(a));
        const int pb = abs(p - static_cast<int>(b));
        const int pc = abs(p - static_cast<int>(c));

        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }

    bool unfilterPngRow(
        uint8_t filter,
        uint8_t *row,
        const uint8_t *prevRow,
        size_t rowBytes,
        int bpp
    ) {
        for (size_t i = 0; i < rowBytes; i++) {
            const uint8_t left = i >= static_cast<size_t>(bpp) ? row[i - bpp] : 0;
            const uint8_t up = prevRow ? prevRow[i] : 0;
            const uint8_t upLeft =
                (prevRow && i >= static_cast<size_t>(bpp)) ? prevRow[i - bpp] : 0;

            switch (filter) {
                case 0:
                    break;
                case 1:
                    row[i] = static_cast<uint8_t>(row[i] + left);
                    break;
                case 2:
                    row[i] = static_cast<uint8_t>(row[i] + up);
                    break;
                case 3:
                    row[i] = static_cast<uint8_t>(
                        row[i] + ((static_cast<int>(left) + static_cast<int>(up)) >> 1)
                    );
                    break;
                case 4:
                    row[i] = static_cast<uint8_t>(row[i] + paethPredictor(left, up, upLeft));
                    break;
                default:
                    return false;
            }
        }

        return true;
    }

    uint8_t alphaBlendWithWhite(uint8_t gray, uint8_t alpha) {
        return static_cast<uint8_t>(
            ((static_cast<uint16_t>(gray) * alpha)
                + (255U * static_cast<uint16_t>(255U - alpha))
                + 127U) / 255U
        );
    }

    uint8_t pngPixelGray(
        const PngDecodeInfo &png,
        const uint8_t *row,
        uint32_t x
    ) {
        switch (png.colorType) {
            case 0: {
                uint8_t gray = row[x];
                if (png.transparencySize >= 2) {
                    const uint16_t transparentGray =
                        (static_cast<uint16_t>(png.transparency[0]) << 8)
                            | png.transparency[1];
                    if (transparentGray == gray) {
                        gray = 255;
                    }
                }
                return gray;
            }
            case 2: {
                const uint8_t *px = row + static_cast<size_t>(x) * 3;
                uint8_t alpha = 255;
                if (png.transparencySize >= 6) {
                    const uint16_t tr =
                        (static_cast<uint16_t>(png.transparency[0]) << 8)
                            | png.transparency[1];
                    const uint16_t tg =
                        (static_cast<uint16_t>(png.transparency[2]) << 8)
                            | png.transparency[3];
                    const uint16_t tb =
                        (static_cast<uint16_t>(png.transparency[4]) << 8)
                            | png.transparency[5];
                    if (tr == px[0] && tg == px[1] && tb == px[2]) {
                        alpha = 0;
                    }
                }

                const uint8_t gray = static_cast<uint8_t>(
                    (static_cast<uint16_t>(px[0]) * 30
                        + static_cast<uint16_t>(px[1]) * 59
                        + static_cast<uint16_t>(px[2]) * 11) / 100
                );
                return alphaBlendWithWhite(gray, alpha);
            }
            case 3: {
                const uint8_t index = row[x];
                const size_t paletteOffset = static_cast<size_t>(index) * 3;
                if (!png.palette || paletteOffset + 2 >= png.paletteSize) {
                    return 255;
                }

                const uint8_t alpha =
                    index < png.transparencySize ? png.transparency[index] : 255;
                const uint8_t gray = static_cast<uint8_t>(
                    (static_cast<uint16_t>(png.palette[paletteOffset]) * 30
                        + static_cast<uint16_t>(png.palette[paletteOffset + 1]) * 59
                        + static_cast<uint16_t>(png.palette[paletteOffset + 2]) * 11) / 100
                );
                return alphaBlendWithWhite(gray, alpha);
            }
            case 4: {
                const uint8_t *px = row + static_cast<size_t>(x) * 2;
                return alphaBlendWithWhite(px[0], px[1]);
            }
            case 6: {
                const uint8_t *px = row + static_cast<size_t>(x) * 4;
                const uint8_t gray = static_cast<uint8_t>(
                    (static_cast<uint16_t>(px[0]) * 30
                        + static_cast<uint16_t>(px[1]) * 59
                        + static_cast<uint16_t>(px[2]) * 11) / 100
                );
                return alphaBlendWithWhite(gray, px[3]);
            }
            default:
                return 255;
        }
    }

    bool parsePng(const uint8_t *data, size_t size, PngDecodeInfo &png) {
        static const uint8_t signature[8] = {
            0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'
        };

        if (!data || size < 33 || memcmp(data, signature, sizeof(signature)) != 0) {
            return false;
        }

        size_t pos = 8;

        try {
            while (pos + 12 <= size) {
                const uint32_t length = readBe32(data + pos);
                const uint8_t *type = data + pos + 4;
                const uint8_t *chunkData = data + pos + 8;

                if (length > size - pos - 12) {
                    return false;
                }

                if (pngChunkTypeEquals(type, "IHDR")) {
                    if (length != 13) {
                        return false;
                    }

                    png.width = readBe32(chunkData);
                    png.height = readBe32(chunkData + 4);
                    png.bitDepth = chunkData[8];
                    png.colorType = chunkData[9];
                    png.interlace = chunkData[12];
                } else if (pngChunkTypeEquals(type, "PLTE")) {
                    png.palette = chunkData;
                    png.paletteSize = length;
                } else if (pngChunkTypeEquals(type, "tRNS")) {
                    png.transparency = chunkData;
                    png.transparencySize = length;
                } else if (pngChunkTypeEquals(type, "IDAT")) {
                    png.idat.insert(png.idat.end(), chunkData, chunkData + length);
                } else if (pngChunkTypeEquals(type, "IEND")) {
                    break;
                }

                pos += static_cast<size_t>(length) + 12;
            }
        } catch (...) {
            return false;
        }

        return png.width > 0
            && png.height > 0
            && png.bitDepth == 8
            && png.interlace == 0
            && pngBytesPerPixel(png.colorType) > 0
            && !png.idat.empty();
    }

    struct PngRenderContext {
        const PngDecodeInfo *png = nullptr;
        JpegDrawContext *draw = nullptr;
        uint8_t *scanline = nullptr;
        uint8_t *currentRow = nullptr;
        uint8_t *prevRow = nullptr;
        size_t scanlineBytes = 0;
        size_t rowBytes = 0;
        size_t scanlineOffset = 0;
        uint32_t sourceY = 0;
        int bpp = 0;
        bool ok = true;
    };

    bool renderPngScanline(PngRenderContext *ctx) {
        if (!ctx || !ctx->png || !ctx->draw
            || !ctx->scanline || !ctx->currentRow || !ctx->prevRow) {
            return false;
        }

        if (ctx->sourceY >= ctx->png->height) {
            return false;
        }

        memcpy(ctx->currentRow, ctx->scanline + 1, ctx->rowBytes);
        if (!unfilterPngRow(
                ctx->scanline[0],
                ctx->currentRow,
                ctx->prevRow,
                ctx->rowBytes,
                ctx->bpp
            )) {
            return false;
        }

        const int targetW = ctx->draw->destWidth;
        const int targetH = ctx->draw->destHeight;
        const int localY0 =
            (static_cast<int>(ctx->sourceY) * targetH)
                / static_cast<int>(ctx->png->height);
        int localY1 =
            (static_cast<int>(ctx->sourceY + 1) * targetH)
                / static_cast<int>(ctx->png->height);
        if (localY1 <= localY0) {
            localY1 = localY0 + 1;
        }

        for (uint32_t sourceX = 0; sourceX < ctx->png->width; sourceX++) {
            const uint8_t gray = pngPixelGray(*ctx->png, ctx->currentRow, sourceX);
            const int localX0 =
                (static_cast<int>(sourceX) * targetW)
                    / static_cast<int>(ctx->png->width);
            int localX1 =
                (static_cast<int>(sourceX + 1) * targetW)
                    / static_cast<int>(ctx->png->width);
            if (localX1 <= localX0) {
                localX1 = localX0 + 1;
            }

            if (ctx->draw->collectGrayscale) {
                for (int yy = localY0; yy < localY1 && yy < targetH; yy++) {
                    for (int xx = localX0; xx < localX1 && xx < targetW; xx++) {
                        storeGraySample(ctx->draw, xx, yy, gray);
                    }
                }
            } else {
                for (int yy = localY0; yy < localY1 && yy < targetH; yy++) {
                    for (int xx = localX0; xx < localX1 && xx < targetW; xx++) {
                        const int drawX = ctx->draw->drawX + xx;
                        const int drawY = ctx->draw->drawY + yy;
                        const uint8_t level = orderedFourGrayLevel(gray, drawX, drawY);
                        drawFourGrayPixel(*ctx->draw->display, drawX, drawY, level);
                    }
                }
            }
        }

        uint8_t *tmp = ctx->prevRow;
        ctx->prevRow = ctx->currentRow;
        ctx->currentRow = tmp;
        ctx->sourceY++;
        ctx->scanlineOffset = 0;

        return true;
    }

    int pngInflateCallback(const void *buffer, int len, void *user) {
        PngRenderContext *ctx = static_cast<PngRenderContext *>(user);
        const uint8_t *input = static_cast<const uint8_t *>(buffer);

        if (!ctx || !input || len < 0 || !ctx->ok) {
            return 0;
        }

        int remaining = len;
        while (remaining > 0) {
            const size_t needed = ctx->scanlineBytes - ctx->scanlineOffset;
            const size_t toCopy =
                static_cast<size_t>(remaining) < needed
                    ? static_cast<size_t>(remaining)
                    : needed;

            memcpy(ctx->scanline + ctx->scanlineOffset, input, toCopy);
            ctx->scanlineOffset += toCopy;
            input += toCopy;
            remaining -= static_cast<int>(toCopy);

            if (ctx->scanlineOffset == ctx->scanlineBytes) {
                if (!renderPngScanline(ctx)) {
                    ctx->ok = false;
                    return 0;
                }
            }
        }

        return 1;
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

    Serial.print("DISPLAY: bw buffer pages = ");
    Serial.println(m_displayBw.pages());
    Serial.print("DISPLAY: bw page height = ");
    Serial.println(m_displayBw.pageHeight());

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
    const unsigned long startedAt = millis();

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

    int displayPass = 0;
    bool hasMorePages = false;

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

        const unsigned long nextPageStartedAt = millis();
        Serial.print("DISPLAY: html bw nextPage start pass=");
        Serial.println(displayPass);
        hasMorePages = m_displayBw.nextPage();
        Serial.print("DISPLAY: html bw nextPage ms pass=");
        Serial.print(displayPass);
        Serial.print(" = ");
        Serial.println(millis() - nextPageStartedAt);
        displayPass++;
    } while (hasMorePages);

    const unsigned long refreshedAt = millis();
    Serial.print("DISPLAY: html bw draw+refresh ms = ");
    Serial.println(refreshedAt - startedAt);

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
    } else if (cached->data && cached->size >= 8
        && cached->data[0] == 0x89
        && cached->data[1] == 'P'
        && cached->data[2] == 'N'
        && cached->data[3] == 'G') {
        rendered = drawPngData(
            cached->data,
            cached->size,
            imageX,
            y,
            imageWidth,
            imageHeight
        );
    } else if (!cached->localFilePath.isEmpty()) {
        String lowerPath = cached->localFilePath;
        lowerPath.toLowerCase();

        if (lowerPath.endsWith(".png")) {
            rendered = drawPngFile(
                cached->localFilePath,
                imageX,
                y,
                imageWidth,
                imageHeight
            );
        } else {
            rendered = drawJpegFile(
                cached->localFilePath,
                imageX,
                y,
                imageWidth,
                imageHeight
            );
        }
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

bool DisplayDriver::drawPngData(
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

    PngDecodeInfo png;
    if (!parsePng(data, size, png)) {
        Serial.println("DISPLAY: png parse failed or unsupported format");
        return false;
    }

    const int bpp = pngBytesPerPixel(png.colorType);
    const size_t rowBytes = static_cast<size_t>(png.width) * bpp;
    const size_t scanlineBytes = rowBytes + 1;

    if (rowBytes == 0 || scanlineBytes > 8192) {
        Serial.println("DISPLAY: png row too large");
        return false;
    }

    const float fitScale = min(
        static_cast<float>(maxWidth) / static_cast<float>(png.width),
        static_cast<float>(maxHeight) / static_cast<float>(png.height)
    );
    const float targetScale = min(fitScale, Constants::DISPLAY_IMAGE_MAX_UPSCALE);

    int targetW = static_cast<int>(png.width * targetScale);
    int targetH = static_cast<int>(png.height * targetScale);

    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;
    if (targetW > maxWidth) targetW = maxWidth;
    if (targetH > maxHeight) targetH = maxHeight;

    JpegDrawContext ctx;
    ctx.display = &m_display;
    ctx.decodedWidth = static_cast<int>(png.width);
    ctx.decodedHeight = static_cast<int>(png.height);
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
        ctx.collectGrayscale = false;
        Serial.println("DISPLAY: png grayscale buffer allocation failed, using direct ordered dither");
    }

    uint8_t *scanline = static_cast<uint8_t *>(malloc(scanlineBytes));
    uint8_t *currentRow = static_cast<uint8_t *>(malloc(rowBytes));
    uint8_t *prevRow = static_cast<uint8_t *>(calloc(rowBytes, 1));

    if (!scanline || !currentRow || !prevRow) {
        free(scanline);
        free(currentRow);
        free(prevRow);
        free(ctx.grayBuffer);
        Serial.println("DISPLAY: png row buffer allocation failed");
        return false;
    }

    Serial.print("DISPLAY: png ");
    Serial.print(static_cast<unsigned long>(png.width));
    Serial.print("x");
    Serial.print(static_cast<unsigned long>(png.height));
    Serial.print(" -> ");
    Serial.print(targetW);
    Serial.print("x");
    Serial.print(targetH);
    Serial.print(" color=");
    Serial.print(static_cast<int>(png.colorType));
    Serial.print(" dither=");
    Serial.println(ctx.collectGrayscale ? "floyd-steinberg" : "ordered-direct");

    PngRenderContext renderContext;
    renderContext.png = &png;
    renderContext.draw = &ctx;
    renderContext.scanline = scanline;
    renderContext.currentRow = currentRow;
    renderContext.prevRow = prevRow;
    renderContext.scanlineBytes = scanlineBytes;
    renderContext.rowBytes = rowBytes;
    renderContext.bpp = bpp;

    size_t inputSize = png.idat.size();
    const int inflateOk = tinfl_decompress_mem_to_callback(
        png.idat.data(),
        &inputSize,
        pngInflateCallback,
        &renderContext,
        TINFL_FLAG_PARSE_ZLIB_HEADER
    );

    const bool ok = inflateOk == 1
        && renderContext.ok
        && renderContext.sourceY == png.height
        && renderContext.scanlineOffset == 0;

    if (ok && ctx.collectGrayscale) {
        drawErrorDiffusedGrayscaleImage(
            m_display,
            ctx.grayBuffer,
            ctx.drawX,
            ctx.drawY,
            ctx.destWidth,
            ctx.destHeight
        );
    }

    free(scanline);
    free(currentRow);
    free(prevRow);
    free(ctx.grayBuffer);

    if (!ok) {
        Serial.println("DISPLAY: png decode failed");
    }

    return ok;
}

bool DisplayDriver::drawPngFile(
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
        Serial.print("DISPLAY: png file open failed: ");
        Serial.println(path);
        return false;
    }

    const size_t size = file.size();
    uint8_t *data = allocateImageBuffer(size);
    if (!data) {
        file.close();
        Serial.println("DISPLAY: png file buffer allocation failed");
        return false;
    }

    const size_t bytesRead = file.read(data, size);
    file.close();

    if (bytesRead != size) {
        free(data);
        Serial.println("DISPLAY: png file read failed");
        return false;
    }

    const bool ok = drawPngData(data, size, x, y, maxWidth, maxHeight);
    free(data);
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
