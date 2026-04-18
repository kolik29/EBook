#include "WifiService.h"

#include <WiFi.h>
#include "../config/WifiConfig.h"
#include "../config/Constants.h"

WifiService::WifiService(LedService &ledService)
    : m_ledService(ledService) {
}

void WifiService::begin() {
    WiFi.mode(WIFI_OFF);
    WiFi.setSleep(false);
}

void WifiService::enable() {
    if (m_enabled) {
        return;
    }

    Serial.println("WIFI: enabling");

    m_enabled = true;
    m_apStarted = false;
    m_staAttempts = 0;
    m_attemptStartedAt = 0;
    m_state = State::Starting;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);

    startAccessPoint();
    startStaAttempt();

    if (!m_webServer.begin(
            [this]() { this->disable(); },
            [this]() { this->refreshAutoDisable(); }
        )) {
        Serial.println("WIFI: web server failed to start");
    }
    m_ledService.enableAnimatedMode();
    scheduleAutoDisable(Constants::WIFI_AUTO_DISABLE_MS);
}

void WifiService::disable() {
    if (!m_enabled) {
        return;
    }

    Serial.println("WIFI: disabling");

    m_webServer.stop();

    WiFi.disconnect(true, true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    m_enabled = false;
    m_apStarted = false;
    m_staAttempts = 0;
    m_attemptStartedAt = 0;
    m_state = State::Disabled;

    cancelAutoDisable();
    m_ledService.disable();
}

void WifiService::scheduleAutoDisable(unsigned long timeoutMs) {
    m_autoDisableScheduled = true;
    m_autoDisableTimeoutMs = timeoutMs;
    m_autoDisableStartedAt = millis();

    Serial.print("WIFI: auto-disable scheduled in ");
    Serial.print(timeoutMs);
    Serial.println(" ms");
}

void WifiService::cancelAutoDisable() {
    m_autoDisableScheduled = false;
    m_autoDisableTimeoutMs = 0;
    m_autoDisableStartedAt = 0;
}

void WifiService::refreshAutoDisable() {
    if (!m_enabled || !m_autoDisableScheduled) {
        return;
    }

    m_autoDisableStartedAt = millis();
    Serial.println("WIFI: auto-disable timer refreshed");
}

void WifiService::update() {
    if (!m_enabled) {
        return;
    }

    switch (m_state) {
        case State::Starting:
        case State::Connecting:
            handleConnecting();
            break;

        case State::Connected:
        case State::Failed:
        case State::Disabled:
        default:
            break;
    }

    m_webServer.update();
    handleAutoDisable();
}

bool WifiService::isEnabled() const {
    return m_enabled;
}

bool WifiService::isApStarted() const {
    return m_apStarted;
}

bool WifiService::isStaConnected() const {
    return m_state == State::Connected;
}

bool WifiService::isStaFinished() const {
    return m_state == State::Connected || m_state == State::Failed;
}

int WifiService::getAttemptCount() const {
    return m_staAttempts;
}

void WifiService::startAccessPoint() {
    if (m_apStarted) {
        return;
    }

    const bool ok = WiFi.softAP(
        WifiConfig::AP_SSID,
        WifiConfig::AP_PASSWORD[0] ? WifiConfig::AP_PASSWORD : nullptr
    );

    if (ok) {
        m_apStarted = true;
        Serial.print("WIFI AP started: ");
        Serial.println(WifiConfig::AP_SSID);
        Serial.print("WIFI AP IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("WIFI AP failed to start");
    }
}

void WifiService::startStaAttempt() {
    if (WifiConfig::HOME_SSID[0] == '\0') {
        Serial.println("WIFI STA: HOME_SSID is empty, stop trying");
        m_state = State::Failed;
        return;
    }

    if (m_staAttempts >= WifiConfig::MAX_STA_ATTEMPTS) {
        Serial.println("WIFI STA: max attempts reached");
        m_state = State::Failed;
        return;
    }

    m_staAttempts++;
    m_attemptStartedAt = millis();
    m_state = State::Connecting;

    Serial.print("WIFI STA: attempt ");
    Serial.print(m_staAttempts);
    Serial.print(" to connect to ");
    Serial.println(WifiConfig::HOME_SSID);

    WiFi.disconnect(false, false);
    WiFi.begin(WifiConfig::HOME_SSID, WifiConfig::HOME_PASSWORD);
}

void WifiService::handleConnecting() {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        m_state = State::Connected;

        Serial.println("WIFI STA: connected");
        Serial.print("WIFI STA IP: ");
        Serial.println(WiFi.localIP());
        return;
    }

    const unsigned long now = millis();
    if ((now - m_attemptStartedAt) < WifiConfig::STA_CONNECT_TIMEOUT_MS) {
        return;
    }

    Serial.println("WIFI STA: attempt timeout");

    if (m_staAttempts >= WifiConfig::MAX_STA_ATTEMPTS) {
        Serial.println("WIFI STA: giving up");
        m_state = State::Failed;
        return;
    }

    startStaAttempt();
}

void WifiService::handleAutoDisable() {
    if (!m_autoDisableScheduled) {
        return;
    }

    const unsigned long now = millis();
    if ((now - m_autoDisableStartedAt) >= m_autoDisableTimeoutMs) {
        Serial.println("WIFI: auto-disable timeout reached");
        disable();
    }
}

void WifiService::setLibraryService(LibraryService *libraryService) {
    m_webServer.setLibraryService(libraryService);
}