#pragma once

#include <Arduino.h>
#include <stdio.h>

class TimestampedSerial : public Stream {
public:
    explicit TimestampedSerial(HardwareSerial &serial);

    void begin(unsigned long baud);

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    size_t write(uint8_t value) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

private:
    void writePrefix();

    HardwareSerial &m_serial;
    bool m_atLineStart = true;
};

extern TimestampedSerial DebugSerial;

#define Serial DebugSerial
