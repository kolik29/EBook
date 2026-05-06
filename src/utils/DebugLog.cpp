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
    size_t offset = 0;

    while (offset < size) {
        if (m_atLineStart) {
            const uint8_t value = buffer[offset];

            if (value == '\r' || value == '\n') {
                written += m_serial.write(value);
                m_atLineStart = true;
                offset++;
                continue;
            }

            writePrefix();
        }

        const size_t chunkStart = offset;

        while (offset < size && buffer[offset] != '\n') {
            offset++;
        }

        if (offset < size && buffer[offset] == '\n') {
            offset++;
            m_atLineStart = true;
        } else {
            m_atLineStart = false;
        }

        written += m_serial.write(buffer + chunkStart, offset - chunkStart);
    }

    return written;
}

void TimestampedSerial::writePrefix() {
    char prefix[20];
    snprintf(prefix, sizeof(prefix), "[%10lu ms] ", static_cast<unsigned long>(millis()));
    m_serial.print(prefix);
    m_atLineStart = false;
}
