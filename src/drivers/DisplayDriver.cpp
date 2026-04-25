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
    wake();

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

    const bool canUsePartial = m_display.epd2.hasPartialUpdate;
    const bool needFullRefresh = m_partialUpdateCounter >= 15;

    if (canUsePartial && !needFullRefresh) {
        Serial.println("DISPLAY: partial update");

        m_display.setPartialWindow(
            0,
            0,
            m_display.width(),
            m_display.height()
        );

        m_partialUpdateCounter++;
    } else {
        Serial.println("DISPLAY: full update");

        m_display.setFullWindow();
        m_partialUpdateCounter = 0;
    }

    m_display.firstPage();

    const int screenW = m_display.width();
    const int screenH = m_display.height();

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
        m_display.fillScreen(GxEPD_WHITE);
        m_display.setTextColor(GxEPD_BLACK);

        // Заголовок
        m_display.setFont(&FreeMonoBold9pt7b);
        m_display.setCursor(marginLeft, titleY);
        m_display.print(title);

        m_display.drawLine(
            marginLeft,
            titleLineY,
            screenW - marginRight,
            titleLineY,
            GxEPD_BLACK
        );

        // Основной текст
        m_display.setFont(&FreeMono9pt7b);
        renderPreformattedText(text, textX, textY, lineHeight, maxLines);

        // Подвал
        m_display.drawLine(
            marginLeft,
            footerLineY,
            screenW - marginRight,
            footerLineY,
            GxEPD_BLACK
        );

        m_display.setCursor(marginLeft, footerTextY);
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
            m_display.print(line);
        }

        cursorY += lineHeight;
        lineCount++;

        if (end >= text.length()) {
            break;
        }

        start = end + 1;
    }
}

void DisplayDriver::wake() {
    powerOn();

    m_display.init(115200, true, 2, false);
    m_display.setRotation(1);
}