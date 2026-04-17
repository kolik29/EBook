#pragma once

#include <functional>

class WebServerService {
public:
    WebServerService();

    bool begin(
        std::function<void()> onDisableWifi,
        std::function<void()> onActivity = nullptr
    );

    void update();
    void stop();

    bool isRunning() const;

private:
    bool m_running = false;
    bool m_disableWifiRequested = false;

    std::function<void()> m_onDisableWifi;
    std::function<void()> m_onActivity;
};