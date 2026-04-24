#pragma once

#include "../drivers/ButtonDriver.h"
#include "../drivers/SdCardDriver.h"
#include "../services/WifiService.h"
#include "../services/LedService.h"
#include "../services/LibraryService.h"
#include "../drivers/DisplayDriver.h"

class App {
public:
    App();

    void begin();
    void update();

private:
    ButtonDriver m_nextButton;
    ButtonDriver m_prevButton;
    SdCardDriver m_sdCard;
    LedService m_ledService;
    WifiService m_wifiService;
    DisplayDriver m_display;
    LibraryService *m_libraryService = nullptr;

    void handleNextButtonEvent(const ButtonEvent &event);
    void handlePrevButtonEvent(const ButtonEvent &event);
    void handleBothButtonsHold();
};