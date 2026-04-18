#include "WebServerService.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>

static WebServer server(80);

static void listLittleFsRoot() {
    Serial.println("WEB: listing LittleFS root");

    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("WEB: failed to open LittleFS root");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        Serial.print("WEB: file: ");
        Serial.print(file.name());
        Serial.print(" (");
        Serial.print(file.size());
        Serial.println(" bytes)");
        file = root.openNextFile();
    }
}

static String getContentType(const String &path) {
    if (path.endsWith(".html")) return "text/html; charset=utf-8";
    if (path.endsWith(".css")) return "text/css; charset=utf-8";
    if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
    if (path.endsWith(".json")) return "application/json; charset=utf-8";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static bool serveFile(const String &requestPath) {
    String path = requestPath;

    if (path == "/") {
        path = "/index.html";
    }

    if (!LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        Serial.print("WEB: failed to open file: ");
        Serial.println(path);
        return false;
    }

    const String contentType = getContentType(path);
    server.streamFile(file, contentType);
    file.close();

    Serial.print("WEB: served ");
    Serial.println(path);

    return true;
}

WebServerService::WebServerService() {
}

bool WebServerService::begin(
    std::function<void()> onDisableWifi,
    std::function<void()> onActivity
) {
    if (m_running) {
        return true;
    }

    m_onDisableWifi = onDisableWifi;
    m_onActivity = onActivity;
    m_disableWifiRequested = false;

    if (!LittleFS.begin(false)) {
        Serial.println("WEB: LittleFS mount failed");
        return false;
    }

    listLittleFsRoot();

    server.on("/disable-wifi", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"wifi disable scheduled"})");
        m_disableWifiRequested = true;

        Serial.println("WEB: disable-wifi requested");
    });

    server.onNotFound([this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        const String uri = server.uri();

        if (serveFile(uri)) {
            return;
        }

        if (LittleFS.exists("/index.html")) {
            File file = LittleFS.open("/index.html", "r");
            server.streamFile(file, "text/html; charset=utf-8");
            file.close();

            Serial.print("WEB: fallback -> /index.html for ");
            Serial.println(uri);
            return;
        }

        server.send(404, "text/plain", "Not found");
    });

    server.begin();
    m_running = true;

    Serial.println("WEB: server started");
    return true;
}

void WebServerService::update() {
    if (!m_running) {
        return;
    }

    server.handleClient();

    if (m_disableWifiRequested) {
        m_disableWifiRequested = false;

        if (m_onDisableWifi) {
            m_onDisableWifi();
        }
    }
}

void WebServerService::stop() {
    if (!m_running) {
        return;
    }

    server.stop();
    m_running = false;
    m_disableWifiRequested = false;

    Serial.println("WEB: server stopped");
}

bool WebServerService::isRunning() const {
    return m_running;
}