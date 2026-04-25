#include "App.h"
#include <Arduino.h>
#include "../config/Pins.h"
#include "../config/Constants.h"

App::App()
    : m_nextButton(
          Pins::BUTTON_NEXT,
          true,
          Constants::BUTTON_DEBOUNCE_MS,
          Constants::BUTTON_SHORT_PRESS_MS,
          Constants::BUTTON_LONG_PRESS_MS),
      m_prevButton(
          Pins::BUTTON_PREV,
          true,
          Constants::BUTTON_DEBOUNCE_MS,
          Constants::BUTTON_SHORT_PRESS_MS,
          Constants::BUTTON_LONG_PRESS_MS),
      m_sdCard(
          Pins::SPI_SCK,
          Pins::SPI_MISO,
          Pins::SPI_MOSI,
          Pins::SD_CS),
      m_ledService(Pins::RGB_LED),
      m_wifiService(m_ledService),
      m_display(
          Pins::EPD_PWR,
          Pins::EPD_BUSY,
          Pins::EPD_RST,
          Pins::EPD_DC,
          Pins::EPD_CS,
          Pins::EPD_CLK,
          Pins::EPD_DIN) {
}

void App::begin() {
    Serial.begin(115200);
    delay(1000);

    m_nextButton.begin();
    m_prevButton.begin();
    m_ledService.begin();
    m_wifiService.begin();

    Serial.println();
    Serial.println("App started");
    Serial.println("Button test initialized");

    Serial.println("Initializing display...");

    m_display.begin();

    Serial.println("Initializing SD card...");
    if (m_sdCard.begin()) {
        Serial.println("SD mounted successfully");
        m_sdCard.printCardInfo(Serial);

        Serial.println("Root directory:");
        m_sdCard.listDir("/", 1, Serial);

        Serial.println("Initializing library storage...");

        m_libraryService = new LibraryService(m_sdCard.fs());
        m_epubParserService = new EpubParserService(m_sdCard.fs());
        m_epubReaderService = new EpubReaderService(
            m_sdCard.fs(),
            *m_epubParserService,
            m_display
        );

        if (m_libraryService->begin()) {
            Serial.println("Library storage initialized successfully");

            LibraryData library;
            if (m_libraryService->loadLibrary(library)) {
                Serial.print("Library loaded, books count: ");
                Serial.println(library.books.size());
            } else {
                Serial.println("Failed to load library.json");
            }

            m_wifiService.setLibraryService(m_libraryService);
            openActiveBook();
        } else {
            Serial.println("Failed to initialize library storage");
        }
    } else {
        Serial.println("SD mount failed");
    }
}

void App::update() {
    const ButtonEvent nextEvent = m_nextButton.update();
    const ButtonEvent prevEvent = m_prevButton.update();

    handleNextButtonEvent(nextEvent);
    handlePrevButtonEvent(prevEvent);
    handleBothButtonsHold();

    m_wifiService.update();
    m_ledService.update();
}

void App::handleNextButtonEvent(const ButtonEvent &event) {
    switch (event.type) {
        case ButtonEventType::Click:
            Serial.println("NEXT click");

            if (m_epubReaderService) {
                m_epubReaderService->nextPage();
            }

            break;

        case ButtonEventType::LongPress:
            Serial.println("NEXT long press");
            break;

        case ButtonEventType::None:
        case ButtonEventType::Pressed:
        case ButtonEventType::Released:
        default:
            break;
    }
}

void App::handlePrevButtonEvent(const ButtonEvent &event) {
    switch (event.type) {
        case ButtonEventType::Click:
            Serial.println("PREV click");

            if (m_epubReaderService) {
                m_epubReaderService->prevPage();
            }

            break;

        case ButtonEventType::LongPress:
            Serial.println("PREV long press");
            break;

        case ButtonEventType::None:
        case ButtonEventType::Pressed:
        case ButtonEventType::Released:
        default:
            break;
    }
}

void App::handleBothButtonsHold() {
    static bool bothHoldTriggered = false;

    const bool bothPressed = m_nextButton.isPressed() && m_prevButton.isPressed();
    const bool bothLongPressed = m_nextButton.isLongPressed() && m_prevButton.isLongPressed();

    if (bothPressed && bothLongPressed && !bothHoldTriggered) {
        bothHoldTriggered = true;

        Serial.println("BOTH buttons long press -> WIFI MODE");

        if (!m_wifiService.isEnabled()) {
            m_wifiService.enable();
        } else {
            Serial.println("WIFI already enabled");
        }
    }

    if (!bothPressed) {
        bothHoldTriggered = false;
    }
}

void App::openActiveBook() {
    if (!m_libraryService || !m_epubReaderService) {
        return;
    }

    LibraryData library;

    if (!m_libraryService->loadLibrary(library)) {
        Serial.println("APP: failed to load library");
        m_display.showMessage("Library error", "Failed to load library.json.");
        return;
    }

    if (library.books.empty()) {
        Serial.println("APP: library is empty");
        m_display.showMessage("Library is empty", "Upload an EPUB book using web interface.");
        return;
    }

    const BookItem &book = library.books[0];

    String epubPath = "/books/items/";
    epubPath += book.folder;
    epubPath += "/original.epub";

    Serial.print("APP: opening book: ");
    Serial.println(epubPath);

    m_epubReaderService->openBook(epubPath);
}