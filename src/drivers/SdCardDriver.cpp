#include "SdCardDriver.h"

SdCardDriver::SdCardDriver(int sckPin, int misoPin, int mosiPin, int csPin)
    : m_spi(FSPI),
      m_sckPin(sckPin),
      m_misoPin(misoPin),
      m_mosiPin(mosiPin),
      m_csPin(csPin) {
}

bool SdCardDriver::begin() {
    m_spi.begin(m_sckPin, m_misoPin, m_mosiPin, m_csPin);

    m_mounted = SD.begin(m_csPin, m_spi);
    return m_mounted;
}

bool SdCardDriver::isMounted() const {
    return m_mounted;
}

uint8_t SdCardDriver::cardType() const {
    if (!m_mounted) {
        return CARD_NONE;
    }
    return SD.cardType();
}

uint64_t SdCardDriver::cardSizeMb() const {
    if (!m_mounted) {
        return 0;
    }
    return SD.cardSize() / (1024ULL * 1024ULL);
}

uint64_t SdCardDriver::totalSizeMb() const {
    if (!m_mounted) {
        return 0;
    }
    return SD.totalBytes() / (1024ULL * 1024ULL);
}

uint64_t SdCardDriver::usedSizeMb() const {
    if (!m_mounted) {
        return 0;
    }
    return SD.usedBytes() / (1024ULL * 1024ULL);
}

void SdCardDriver::printCardInfo(Stream &out) const {
    if (!m_mounted) {
        out.println("SD: not mounted");
        return;
    }

    out.print("SD card type: ");

    switch (SD.cardType()) {
        case CARD_MMC:
            out.println("MMC");
            break;
        case CARD_SD:
            out.println("SDSC");
            break;
        case CARD_SDHC:
            out.println("SDHC/SDXC");
            break;
        default:
            out.println("UNKNOWN");
            break;
    }

    out.print("SD card size: ");
    out.print(cardSizeMb());
    out.println(" MB");

    out.print("SD total size: ");
    out.print(totalSizeMb());
    out.println(" MB");

    out.print("SD used size: ");
    out.print(usedSizeMb());
    out.println(" MB");
}

void SdCardDriver::listDir(const char *path, uint8_t levels, Stream &out) const {
    if (!m_mounted) {
        out.println("SD: not mounted");
        return;
    }

    File root = SD.open(path);
    if (!root) {
        out.print("Failed to open dir: ");
        out.println(path);
        return;
    }

    if (!root.isDirectory()) {
        out.print("Not a directory: ");
        out.println(path);
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            out.print("[DIR] ");
            out.println(file.name());

            if (levels > 0) {
                listDir(file.path(), levels - 1, out);
            }
        } else {
            out.print("[FILE] ");
            out.print(file.name());
            out.print(" (");
            out.print(file.size());
            out.println(" bytes)");
        }

        file = root.openNextFile();
    }
}

fs::FS &SdCardDriver::fs() {
    return SD;
}