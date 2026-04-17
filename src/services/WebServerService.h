#pragma once

class WebServerService {
public:
    WebServerService();

    bool begin();
    void update();
    void stop();

    bool isRunning() const;

private:
    bool m_running = false;
};