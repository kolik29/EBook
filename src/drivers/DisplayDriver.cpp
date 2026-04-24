#include "DisplayDriver.h"

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

    Serial.println("DISPLAY: initialized");
}

void DisplayDriver::showMessage(const String &title, const String &message) {
    powerOn();

    m_display.setFullWindow();
    m_display.firstPage();

    do {
        m_display.fillScreen(GxEPD_WHITE);
        m_display.setTextColor(GxEPD_BLACK);

        m_display.setFont(&FreeMonoBold9pt7b);
        m_display.setCursor(40, 60);
        m_display.print(title);

        m_display.setFont(&FreeMono9pt7b);
        renderWrappedText(message, 40, 110, 52, 24);
    } while (m_display.nextPage());

    m_display.hibernate();
}

void DisplayDriver::showTextPage(const String &title, const String &text, int page, int totalPages) {
    powerOn();

    m_display.setFullWindow();
    m_display.firstPage();

    do {
        m_display.fillScreen(GxEPD_WHITE);
        m_display.setTextColor(GxEPD_BLACK);

        m_display.setFont(&FreeMonoBold9pt7b);
        m_display.setCursor(30, 40);
        m_display.print(title);

        m_display.drawLine(30, 58, 760, 58, GxEPD_BLACK);

        m_display.setFont(&FreeMono9pt7b);
        renderWrappedText(text, 30, 90, 68, 22);

        m_display.drawLine(30, 445, 760, 445, GxEPD_BLACK);

        m_display.setCursor(30, 470);
        m_display.print("Page ");
        m_display.print(page);
        m_display.print(" / ");
        m_display.print(totalPages);
    } while (m_display.nextPage());

    m_display.hibernate();
}

void DisplayDriver::powerOn() {
    digitalWrite(m_pwrPin, HIGH);
    delay(20);
}

void DisplayDriver::renderWrappedText(const String &text, int x, int y, int maxCharsPerLine, int lineHeight) {
    int cursorY = y;
    int start = 0;

    while (start < text.length()) {
        int end = start + maxCharsPerLine;

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
            m_display.print(line);
            cursorY += lineHeight;
        }

        start = end + 1;

        if (cursorY > 430) {
            break;
        }
    }
}