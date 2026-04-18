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

static bool isDigitsOnly(const String &value) {
    if (value.isEmpty()) {
        return false;
    }

    for (size_t i = 0; i < value.length(); i++) {
        if (!isDigit(value[i])) {
            return false;
        }
    }

    return true;
}

static bool extractBookId(const String &uri, int &bookId) {
    // /books/123
    if (!uri.startsWith("/books/")) {
        return false;
    }

    String tail = uri.substring(String("/books/").length());
    if (!isDigitsOnly(tail)) {
        return false;
    }

    bookId = tail.toInt();
    return true;
}

static bool extractBookPaginationId(const String &uri, int &bookId) {
    // /books/123/pagination
    const String suffix = "/pagination";

    if (!uri.startsWith("/books/") || !uri.endsWith(suffix)) {
        return false;
    }

    String middle = uri.substring(String("/books/").length(), uri.length() - suffix.length());
    if (!isDigitsOnly(middle)) {
        return false;
    }

    bookId = middle.toInt();
    return true;
}

static void sendBooksMock() {
    const char *json =
        R"([
            {
                "id": 1,
                "title": "Harry Potter 1",
                "author": "JK Rowling",
                "img": "https://images.booksense.com/images/403/353/9780590353403.jpg",
                "active": false,
                "page": {
                    "total": 400,
                    "current": 1
                }
            },
            {
                "id": 2,
                "title": "Harry Potter 2",
                "author": "JK Rowling",
                "img": "https://images.booksense.com/images/403/353/9780590353403.jpg",
                "active": false,
                "page": {
                    "total": 400,
                    "current": 1
                }
            },
            {
                "id": 6,
                "title": "Harry Potter 6",
                "author": "JK Rowling",
                "img": "https://images.booksense.com/images/403/353/9780590353403.jpg",
                "active": true,
                "page": {
                    "total": 400,
                    "current": 123
                }
            }
        ])";

    server.send(200, "application/json; charset=utf-8", json);
}

static void sendBookPaginationMock(int bookId) {
    String json = "{";
    json += "\"id\":" + String(bookId) + ",";
    json += "\"total\":400,";
    json += "\"current\":123";
    json += "}";

    server.send(200, "application/json; charset=utf-8", json);
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

    server.on("/rotate-display", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"display rotation"})");
        Serial.println("WEB: rotate-display requested");
    });

    server.on("/refresh-display", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"display refresh"})");
        Serial.println("WEB: refresh-display requested");
    });

    server.on("/books", HTTP_GET, [this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        Serial.println("WEB: get books requested");
        sendBooksMock();
    });

    server.on(
        "/books/upload",
        HTTP_POST,
        [this]() {
            if (m_onActivity) {
                m_onActivity();
            }

            server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"upload accepted"})");
            Serial.println("WEB: upload book completed");
        },
        []() {
            HTTPUpload &upload = server.upload();

            if (upload.status == UPLOAD_FILE_START) {
                Serial.print("WEB: upload started: ");
                Serial.println(upload.filename);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                Serial.print("WEB: upload chunk size: ");
                Serial.println(upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                Serial.print("WEB: upload finished, total size: ");
                Serial.println(upload.totalSize);
            }
        }
    );

    server.onNotFound([this]() {
        if (m_onActivity) {
            m_onActivity();
        }

        const String uri = server.uri();
        const HTTPMethod method = server.method();

        // DELETE /books/:id
        if (method == HTTP_DELETE) {
            int bookId = 0;
            if (extractBookId(uri, bookId)) {
                Serial.print("WEB: delete book requested, id=");
                Serial.println(bookId);

                server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"book deleted"})");
                return;
            }
        }

        // PATCH /books/:id
        if (method == HTTP_PATCH) {
            int bookId = 0;
            if (extractBookId(uri, bookId)) {
                String body = server.arg("plain");

                Serial.print("WEB: update current page requested, id=");
                Serial.println(bookId);
                Serial.print("WEB: patch body: ");
                Serial.println(body);

                server.send(200, "application/json; charset=utf-8", R"({"ok":true,"message":"book updated"})");
                return;
            }
        }

        // GET /books/:id/pagination
        if (method == HTTP_GET) {
            int bookId = 0;
            if (extractBookPaginationId(uri, bookId)) {
                Serial.print("WEB: get pagination requested, id=");
                Serial.println(bookId);

                sendBookPaginationMock(bookId);
                return;
            }
        }

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