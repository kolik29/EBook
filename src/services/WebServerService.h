#pragma once

#include <functional>
#include "LibraryService.h"

class WebServerService {
public:
    WebServerService();

    bool begin(
        std::function<void()> onDisableWifi,
        std::function<void()> onActivity
    );

    void update();
    void stop();

    bool isRunning() const;

    void setLibraryService(LibraryService *libraryService);

    // Called after a successful PATCH /books/:id so the device can open
    // the book on the display at the requested page.
    // Signature: (epubFolder, globalPage)
    void setOnOpenBookPage(std::function<void(const String &, uint32_t)> cb) {
        m_onOpenBookPage = cb;
    }

private:
    bool m_running = false;
    bool m_disableWifiRequested = false;

    std::function<void()> m_onDisableWifi;
    std::function<void()> m_onActivity;
    std::function<void(const String &, uint32_t)> m_onOpenBookPage;

    LibraryService *m_libraryService = nullptr;
};