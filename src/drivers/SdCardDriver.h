#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

class SdCardDriver {
public:
    SdCardDriver(int sckPin, int misoPin, int mosiPin, int csPin);

    bool begin();

    void printCardInfo(Stream &out) const;
    void listDir(const char *path, uint8_t levels, Stream &out) const;

    fs::FS &fs();

private:
    SPIClass m_spi;
    int m_sckPin;
    int m_misoPin;
    int m_mosiPin;
    int m_csPin;

    bool m_mounted = false;

    uint64_t cardSizeMb() const;
    uint64_t totalSizeMb() const;
    uint64_t usedSizeMb() const;
};
