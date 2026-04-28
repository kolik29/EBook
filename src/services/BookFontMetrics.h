#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>

#include "../models/HtmlRenderModels.h"

namespace BookFontMetrics {
    const GFXfont *fontForStyle(const HtmlTextStyle &style);
    int textWidth(const String &text, const HtmlTextStyle &style);
    int spaceWidth(const HtmlTextStyle &style);
    int fallbackCharWidth(const HtmlTextStyle &style);
}
