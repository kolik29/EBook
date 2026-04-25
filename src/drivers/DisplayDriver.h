#pragma once

#include <Arduino.h>
#include <SPI.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Для твоей панели 075BN-T7-D2.
using EpdDisplay = GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT>;

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

private:
    SPIClass m_spi;
    EpdDisplay m_display;

    int m_pwrPin;
    int m_busyPin;
    int m_rstPin;
    int m_dcPin;
    int m_csPin;
    int m_clkPin;
    int m_dinPin;
    int m_partialUpdateCounter = 0;

    void powerOn();
    void renderWrappedText(const String &text, int x, int y, int maxCharsPerLine, int lineHeight);
    void renderPreformattedText(const String &text, int x, int y, int lineHeight, int maxLines);
    void wake();
};