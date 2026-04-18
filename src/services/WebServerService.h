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

private:
    bool m_running = false;
    bool m_disableWifiRequested = false;

    std::function<void()> m_onDisableWifi;
    std::function<void()> m_onActivity;

    LibraryService *m_libraryService = nullptr;
};