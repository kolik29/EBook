#include "WifiService.h"

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../config/WifiConfig.h"
#include "../config/Constants.h"
#include "../utils/DebugLog.h"

namespace {
    void logWifiHeap(const char *label) {
        Serial.print("WIFI heap ");
        Serial.print(label);
        Serial.print(": free=");
        Serial.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
        Serial.print(" largest=");
        Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

WifiService::WifiService(LedService &ledService)
    : m_ledService(ledService) {
}

void WifiService::begin() {
    WiFi.mode(WIFI_OFF);
    WiFi.setHostname(WifiConfig::OTA_HOSTNAME);
    WiFi.setSleep(false);
}

void WifiService::enable() {
    if (m_enabled) {
        return;
    }

    Serial.println("WIFI: enabling");
    logWifiHeap("before enable");

    m_enabled = true;
    m_apStarted = false;
    m_staAttempts = 0;
    m_attemptStartedAt = 0;
    m_state = State::Starting;

    WiFi.setHostname(WifiConfig::OTA_HOSTNAME);
    Serial.print("WIFI STA hostname: ");
    Serial.println(WifiConfig::OTA_HOSTNAME);

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    logWifiHeap("after mode");

    startAccessPoint();
    startStaAttempt();
    startOta();

    if (!m_webServer.begin(
            [this]() { this->disable(); },
            [this]() { this->refreshAutoDisable(); }
        )) {
        Serial.println("WIFI: web server failed to start");
    }
    logWifiHeap("after services");

    m_ledService.enableAnimatedMode();
    scheduleAutoDisable(Constants::WIFI_AUTO_DISABLE_MS);
}

void WifiService::disable() {
    if (!m_enabled) {
        return;
    }

    if (m_otaInProgress) {
        Serial.println("WIFI: disable ignored during OTA update");
        return;
    }

    Serial.println("WIFI: disabling");

    stopOta();
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

    handleOta();

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

void WifiService::startOta() {
    if (m_otaStarted) {
        return;
    }

    ArduinoOTA.setHostname(WifiConfig::OTA_HOSTNAME);
    ArduinoOTA.setPort(WifiConfig::OTA_PORT);
    ArduinoOTA.setMdnsEnabled(false);

    if (WifiConfig::OTA_PASSWORD[0] != '\0') {
        ArduinoOTA.setPassword(WifiConfig::OTA_PASSWORD);
    }

    ArduinoOTA.onStart([this]() {
        m_otaInProgress = true;
        m_otaLastProgressPercent = -1;
        cancelAutoDisable();

        Serial.println("OTA: update started");

        if (ArduinoOTA.getCommand() == U_FLASH) {
            Serial.println("OTA: target = firmware");
        } else {
            Serial.println("OTA: target = filesystem");
        }
    });

    ArduinoOTA.onEnd([this]() {
        m_otaInProgress = false;
        Serial.println();
        Serial.println("OTA: update finished");
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        const int percent = total > 0
            ? static_cast<int>((progress * 100U) / total)
            : 0;

        if (percent != m_otaLastProgressPercent && (percent % 5 == 0 || percent == 100)) {
            m_otaLastProgressPercent = percent;
            Serial.print("OTA: progress ");
            Serial.print(percent);
            Serial.println("%");
        }
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        m_otaInProgress = false;

        Serial.print("OTA: error ");
        Serial.println(static_cast<int>(error));

        switch (error) {
            case OTA_AUTH_ERROR:
                Serial.println("OTA: auth failed");
                break;
            case OTA_BEGIN_ERROR:
                Serial.println("OTA: begin failed");
                break;
            case OTA_CONNECT_ERROR:
                Serial.println("OTA: connect failed");
                break;
            case OTA_RECEIVE_ERROR:
                Serial.println("OTA: receive failed");
                break;
            case OTA_END_ERROR:
                Serial.println("OTA: end failed");
                break;
            default:
                break;
        }

        scheduleAutoDisable(Constants::WIFI_AUTO_DISABLE_MS);
    });

    ArduinoOTA.begin();

    m_otaStarted = true;

    Serial.print("OTA: ready, hostname=");
    Serial.print(WifiConfig::OTA_HOSTNAME);
    Serial.print(", port=");
    Serial.print(WifiConfig::OTA_PORT);
    Serial.println(", mdns=off");
}

void WifiService::stopOta() {
    if (!m_otaStarted) {
        return;
    }

    ArduinoOTA.end();
    m_otaStarted = false;
    m_otaInProgress = false;
    m_otaLastProgressPercent = -1;

    Serial.println("OTA: stopped");
}

void WifiService::handleOta() {
    if (!m_otaStarted) {
        return;
    }

    ArduinoOTA.handle();
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
        Serial.print("WIFI STA MAC: ");
        Serial.println(WiFi.macAddress());
        Serial.print("WIFI STA hostname: ");
        Serial.println(WiFi.getHostname());
        logWifiHeap("after STA connected");
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

void WifiService::setOnOpenBookPage(std::function<void(const String &, uint32_t)> cb) {
    m_webServer.setOnOpenBookPage(cb);
}
