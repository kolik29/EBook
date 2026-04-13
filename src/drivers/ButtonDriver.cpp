#include "ButtonDriver.h"

ButtonDriver::ButtonDriver(
    int pin,
    bool activeHigh,
    unsigned long debounceMs,
    unsigned long shortPressMs,
    unsigned long longPressMs
)
    : m_pin(pin),
      m_activeHigh(activeHigh),
      m_debounceMs(debounceMs),
      m_shortPressMs(shortPressMs),
      m_longPressMs(longPressMs) {
}

bool ButtonDriver::readPressedRaw() const {
    const int level = digitalRead(m_pin);
    return m_activeHigh ? (level == HIGH) : (level == LOW);
}

void ButtonDriver::begin() {
    pinMode(m_pin, INPUT);

    const bool initialPressed = readPressedRaw();

    m_lastRawPressed = initialPressed;
    m_stablePressed = initialPressed;
    m_lastDebounceTime = millis();
    m_pressStartTime = millis();
    m_longPressFired = false;
    m_ignoreUntilRelease = initialPressed;
}

ButtonEvent ButtonDriver::update() {
    ButtonEvent event;
    const unsigned long now = millis();
    const bool currentRawPressed = readPressedRaw();

    if (currentRawPressed != m_lastRawPressed) {
        m_lastDebounceTime = now;
        m_lastRawPressed = currentRawPressed;
    }

    if ((now - m_lastDebounceTime) < m_debounceMs) {
        return event;
    }

    if (currentRawPressed != m_stablePressed) {
        m_stablePressed = currentRawPressed;

        if (m_ignoreUntilRelease) {
            if (!m_stablePressed) {
                m_ignoreUntilRelease = false;
            }
            return event;
        }

        if (m_stablePressed) {
            m_pressStartTime = now;
            m_longPressFired = false;
            event.type = ButtonEventType::Pressed;
            return event;
        }

        const unsigned long pressDuration = now - m_pressStartTime;

        if (m_longPressFired) {
            event.type = ButtonEventType::Released;
        } else if (pressDuration >= m_shortPressMs) {
            event.type = ButtonEventType::Click;
        } else {
            event.type = ButtonEventType::None;
        }

        return event;
    }

    if (m_ignoreUntilRelease) {
        return event;
    }

    if (m_stablePressed && !m_longPressFired) {
        if ((now - m_pressStartTime) >= m_longPressMs) {
            m_longPressFired = true;
            event.type = ButtonEventType::LongPress;
            return event;
        }
    }

    return event;
}

bool ButtonDriver::isPressed() const {
    return m_stablePressed && !m_ignoreUntilRelease;
}

bool ButtonDriver::isLongPressed() const {
    return m_stablePressed && m_longPressFired && !m_ignoreUntilRelease;
}