#pragma once

#include <Arduino.h>

class WifiService {
public:
    WifiService();

    void begin();
    void update();

    void enable();
    void disable();

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

    void startAccessPoint();
    void startStaAttempt();
    void handleConnecting();
};