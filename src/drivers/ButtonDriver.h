#pragma once

#include <Arduino.h>

enum class ButtonEventType {
    None,
    Pressed,
    Released,
    Click,
    LongPress
};

struct ButtonEvent {
    ButtonEventType type = ButtonEventType::None;
};

class ButtonDriver {
public:
    ButtonDriver(
        int pin,
        bool activeHigh,
        unsigned long debounceMs,
        unsigned long shortPressMs,
        unsigned long longPressMs
    );

    void begin();
    ButtonEvent update();

    bool isPressed() const;
    bool isLongPressed() const;

private:
    bool readPressedRaw() const;

    int m_pin;
    bool m_activeHigh;
    unsigned long m_debounceMs;
    unsigned long m_shortPressMs;
    unsigned long m_longPressMs;

    bool m_stablePressed = false;
    bool m_lastRawPressed = false;

    unsigned long m_lastDebounceTime = 0;
    unsigned long m_pressStartTime = 0;

    bool m_longPressFired = false;
    bool m_ignoreUntilRelease = false;
};