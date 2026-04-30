#pragma once

namespace Constants {
    constexpr unsigned long BUTTON_DEBOUNCE_MS = 30;
    constexpr unsigned long BUTTON_SHORT_PRESS_MS = 200;
    constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;

    constexpr unsigned long WIFI_AUTO_DISABLE_MS = 15UL * 60UL * 1000UL;

    // Page layout — shared between EpubReaderService and upload-time page builder
    constexpr int PAGE_MAX_CHARS_PER_LINE = 40;
    constexpr int PAGE_MAX_LINES_PER_PAGE = 30;
    constexpr int HTML_PAGE_CONTENT_WIDTH_PX = 452;
    constexpr int HTML_PAGE_CONTENT_HEIGHT_PX = 696;
    constexpr int HTML_DEFAULT_IMAGE_HEIGHT_PX = 260;
    constexpr int PAGE_INDEX_VERSION = 8;

    // Partial page updates are fast; a periodic full refresh keeps the panel
    // conditioned without flashing on every page turn.
    constexpr bool DISPLAY_USE_PARTIAL_PAGE_UPDATES = true;
    constexpr int DISPLAY_FULL_REFRESH_AFTER_PARTIALS = 8;
    constexpr float DISPLAY_IMAGE_MAX_UPSCALE = 2.0f;
    constexpr bool DISPLAY_IMAGE_USE_ERROR_DIFFUSION = true;
    constexpr int DISPLAY_IMAGE_BLACK_POINT = 18;
    constexpr int DISPLAY_IMAGE_WHITE_POINT = 255;
    constexpr int DISPLAY_IMAGE_CONTRAST_PERCENT = 128;
    constexpr int DISPLAY_IMAGE_BRIGHTNESS_OFFSET = -18;
    constexpr int DISPLAY_IMAGE_DITHER_STRENGTH = 2;
}
