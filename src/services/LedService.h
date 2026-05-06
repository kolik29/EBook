#pragma once

class LedService {
public:
    LedService(int pin);

    void begin();
    void update();

    void enableAnimatedMode();
    void disable();

private:
    int m_pin;
    bool m_enabled = false;
};
