#include "LedService.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

namespace {
    constexpr int kLedCount = 1;
    constexpr unsigned long kStepMs = 40;
    constexpr unsigned long kBlendMs = 900;

    Adafruit_NeoPixel strip(kLedCount, -1, NEO_GRB + NEO_KHZ800);

    struct RgbColor {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    // Палитра из 6 цветов
    constexpr RgbColor kPalette[] = {
        {228, 3, 3},
        {255, 140, 0},
        {255, 237, 0},
        {0, 128, 38},
        {0, 77, 255},
        {117, 7, 135}
    };

    constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

    unsigned long gLastStepAt = 0;
    unsigned long gSegmentStartedAt = 0;
    size_t gCurrentIndex = 0;

    uint8_t lerp8(uint8_t a, uint8_t b, float t) {
        return static_cast<uint8_t>(a + (b - a) * t);
    }

    uint32_t blendColor(const RgbColor &from, const RgbColor &to, float t) {
        uint8_t r = lerp8(from.r, to.r, t);
        uint8_t g = lerp8(from.g, to.g, t);
        uint8_t b = lerp8(from.b, to.b, t);
        return strip.Color(r, g, b);
    }
}

LedService::LedService(int pin)
    : m_pin(pin) {
}

void LedService::begin() {
    strip.setPin(m_pin);
    strip.begin();
    strip.clear();
    strip.show();
}

void LedService::enableAnimatedMode() {
    if (m_enabled) {
        return;
    }

    m_enabled = true;
    gCurrentIndex = 0;
    gSegmentStartedAt = millis();
    gLastStepAt = 0;
}

void LedService::disable() {
    m_enabled = false;
    strip.clear();
    strip.show();
}

bool LedService::isEnabled() const {
    return m_enabled;
}

void LedService::update() {
    if (!m_enabled) {
        return;
    }

    const unsigned long now = millis();
    if ((now - gLastStepAt) < kStepMs) {
        return;
    }

    gLastStepAt = now;

    const size_t nextIndex = (gCurrentIndex + 1) % kPaletteSize;
    float t = static_cast<float>(now - gSegmentStartedAt) / static_cast<float>(kBlendMs);

    if (t >= 1.0f) {
        gCurrentIndex = nextIndex;
        gSegmentStartedAt = now;
        t = 0.0f;
    }

    uint32_t color = blendColor(kPalette[gCurrentIndex], kPalette[nextIndex], t);
    strip.setPixelColor(0, color);
    strip.show();
}