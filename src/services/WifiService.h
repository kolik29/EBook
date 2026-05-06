#pragma once

#include <Arduino.h>
#include <functional>
#include "WebServerService.h"
#include "LedService.h"
#include "LibraryService.h"

class WifiService {
public:
    WifiService(LedService &ledService);

    void begin();
    void update();

    void enable();
    void disable();

    void scheduleAutoDisable(unsigned long timeoutMs);
    void cancelAutoDisable();
    void refreshAutoDisable();

    bool isEnabled() const;
    bool isApStarted() const;
    bool isStaConnected() const;
    bool isStaFinished() const;
    int getAttemptCount() const;

    void setLibraryService(LibraryService *libraryService);
    void setOnOpenBookPage(std::function<void(const String &, uint32_t)> cb);

private:
    enum class State {
        Disabled,
        Starting,
        Connecting,
        Connected,
        Failed
    };

    State m_state = State::Disabled;

    bool m_enabled = false;
    bool m_apStarted = false;
    bool m_otaStarted = false;
    bool m_otaInProgress = false;
    int m_otaLastProgressPercent = -1;

    int m_staAttempts = 0;
    unsigned long m_attemptStartedAt = 0;

    bool m_autoDisableScheduled = false;
    unsigned long m_autoDisableTimeoutMs = 0;
    unsigned long m_autoDisableStartedAt = 0;

    WebServerService m_webServer;
    LedService &m_ledService;

    void startOta();
    void stopOta();
    void handleOta();
    void startAccessPoint();
    void startStaAttempt();
    void handleConnecting();
    void handleAutoDisable();
};
