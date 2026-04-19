#pragma once

#include <Arduino.h>
#include <FS.h>

#include "../models/EpubModels.h"

class EpubParserService {
public:
    explicit EpubParserService(fs::FS &fs);

    bool readMetadata(const String &epubPath, EpubMetadata &outMetadata);

private:
    fs::FS &m_fs;

    struct ZipEntryInfo {
        String name;
        uint16_t method = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t localHeaderOffset = 0;
        bool isDirectory = false;
    };

    bool findZipEntry(File &file, const char *entryPath, ZipEntryInfo &outEntry);
    bool extractZipEntryText(File &file, const ZipEntryInfo &entry, size_t maxSize, String &outText);

    bool findRootFilePath(const String &containerXml, String &outRootFilePath) const;
    bool parsePackageMetadata(const String &packageXml, EpubMetadata &outMetadata) const;

    String extractXmlTagValue(const String &xml, const char *tagName) const;
    String extractXmlAttributeValue(const String &xml, int tagStartPos, const char *attributeName) const;
    String decodeXmlEntities(String value) const;
};