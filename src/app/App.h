#pragma once

#include "../drivers/ButtonDriver.h"
#include "../drivers/SdCardDriver.h"
#include "../services/WifiService.h"

class App {
public:
    App();

    void begin();
    void update();

private:
    ButtonDriver m_nextButton;
    ButtonDriver m_prevButton;
    SdCardDriver m_sdCard;
    WifiService m_wifiService;

    void handleNextButtonEvent(const ButtonEvent &event);
    void handlePrevButtonEvent(const ButtonEvent &event);
    void handleBothButtonsHold();
};