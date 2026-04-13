#pragma once

#include <Arduino.h>

class WifiService {
public:
    WifiService();

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

    int m_staAttempts = 0;
    unsigned long m_attemptStartedAt = 0;

    bool m_autoDisableScheduled = false;
    unsigned long m_autoDisableTimeoutMs = 0;
    unsigned long m_autoDisableStartedAt = 0;

    void startAccessPoint();
    void startStaAttempt();
    void handleConnecting();
    void handleAutoDisable();
};