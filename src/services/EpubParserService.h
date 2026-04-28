#pragma once

#include <Arduino.h>
#include <FS.h>

#include "../models/EpubModels.h"

class EpubParserService {
public:
    explicit EpubParserService(fs::FS &fs);

    bool readMetadata(const String &epubPath, EpubMetadata &outMetadata);
    bool extractCoverToFile(
        const String &epubPath,
        const EpubMetadata &metadata,
        const String &outputBasePath,
        String &outCoverPath
    );

    bool parseBookStructure(const String &epubPath, EpubBookStructure &structure);
    bool readSpineItemHtml(const String &epubPath, const EpubSpineItem &item, String &html);
    bool readSpineItemText(const String &epubPath, const EpubSpineItem &item, String &text);
    bool extractResourceData(
        const String &epubPath,
        const String &resourcePath,
        size_t maxSize,
        uint8_t *&outData,
        size_t &outSize
    );
    const String &getLastError() const { return m_lastError; }

private:
    fs::FS &m_fs;
    String m_lastError;

    struct ZipEntryInfo {
        String name;
        uint16_t method = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t localHeaderOffset = 0;
        bool isDirectory = false;
    };

    bool findZipEntry(File &file, const char *entryPath, ZipEntryInfo &outEntry);

    bool extractZipEntryData(
        File &file,
        const ZipEntryInfo &entry,
        size_t maxSize,
        uint8_t *&outData,
        size_t &outSize
    );

    bool extractZipEntryText(
        File &file,
        const ZipEntryInfo &entry,
        size_t maxSize,
        String &outText
    );

    bool findRootFilePath(const String &containerXml, String &outRootFilePath) const;
    bool parsePackageMetadata(
        const String &packageXml,
        const String &packagePath,
        EpubMetadata &outMetadata
    ) const;

    String extractXmlTagValue(const String &xml, const char *tagName) const;
    String extractXmlTagValuesJoined(const String &xml, const char *tagName) const;
    String extractXmlAttributeValue(const String &xml, int tagStartPos, const char *attributeName) const;
    String decodeXmlEntities(String value) const;

    String resolveRelativePath(const String &baseFilePath, const String &relativePath) const;
    String normalizePath(const String &path) const;
    String getLowerFileExtension(const String &path) const;
    bool isImageMediaType(const String &mediaType) const;

    void clearLastError();
    void setLastError(const String &message);
    void setLastError(const String &message, const String &detail);
    void setLastErrorIfEmpty(const String &message);
    void setLastErrorIfEmpty(const String &message, const String &detail);
};
