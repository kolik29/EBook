#include "DebugLog.h"

#undef Serial

TimestampedSerial DebugSerial(Serial);

TimestampedSerial::TimestampedSerial(HardwareSerial &serial)
    : m_serial(serial) {
}

void TimestampedSerial::begin(unsigned long baud) {
    m_serial.begin(baud);
}

int TimestampedSerial::available() {
    return m_serial.available();
}

int TimestampedSerial::read() {
    return m_serial.read();
}

int TimestampedSerial::peek() {
    return m_serial.peek();
}

void TimestampedSerial::flush() {
    m_serial.flush();
}

size_t TimestampedSerial::write(uint8_t value) {
    if (m_atLineStart && value != '\r' && value != '\n') {
        writePrefix();
    }

    const size_t written = m_serial.write(value);

    if (value == '\n') {
        m_atLineStart = true;
    } else if (value != '\r') {
        m_atLineStart = false;
    }

    return written;
}

size_t TimestampedSerial::write(const uint8_t *buffer, size_t size) {
    size_t written = 0;

    for (size_t i = 0; i < size; i++) {
        written += write(buffer[i]);
    }

    return written;
}

void TimestampedSerial::writePrefix() {
    char prefix[20];
    snprintf(prefix, sizeof(prefix), "[%10lu ms] ", static_cast<unsigned long>(millis()));
    m_serial.print(prefix);
    m_atLineStart = false;
}
