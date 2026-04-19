#include "EpubParserService.h"

#include <Arduino.h>
#include <rom/miniz.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {
    constexpr uint32_t ZIP_LOCAL_FILE_HEADER_SIGNATURE = 0x04034b50UL;
    constexpr uint32_t ZIP_CENTRAL_DIR_SIGNATURE = 0x02014b50UL;
    constexpr uint32_t ZIP_END_OF_CENTRAL_DIR_SIGNATURE = 0x06054b50UL;
    constexpr size_t ZIP_EOCD_MIN_SIZE = 22;
    constexpr size_t ZIP_MAX_COMMENT_SIZE = 0xFFFF;
    constexpr size_t ZIP_EOCD_SEARCH_WINDOW = ZIP_EOCD_MIN_SIZE + ZIP_MAX_COMMENT_SIZE;

    uint16_t readLe16(const uint8_t *buf) {
        return static_cast<uint16_t>(buf[0])
            | (static_cast<uint16_t>(buf[1]) << 8);
    }

    uint32_t readLe32(const uint8_t *buf) {
        return static_cast<uint32_t>(buf[0])
            | (static_cast<uint32_t>(buf[1]) << 8)
            | (static_cast<uint32_t>(buf[2]) << 16)
            | (static_cast<uint32_t>(buf[3]) << 24);
    }

    bool seekExact(File &file, uint32_t position) {
        return file.seek(position, SeekSet);
    }

    bool readExact(File &file, void *dst, size_t len) {
        uint8_t *out = static_cast<uint8_t *>(dst);
        size_t total = 0;

        while (total < len) {
            const size_t chunk = file.read(out + total, len - total);
            if (chunk == 0) {
                return false;
            }

            total += chunk;
        }

        return true;
    }

    bool skipBytes(File &file, size_t len) {
        const uint32_t pos = static_cast<uint32_t>(file.position());
        return seekExact(file, pos + static_cast<uint32_t>(len));
    }

    bool readString(File &file, size_t len, String &out) {
        out = "";

        if (len == 0) {
            return true;
        }

        char *buffer = static_cast<char *>(malloc(len + 1));
        if (!buffer) {
            return false;
        }

        const bool ok = readExact(file, buffer, len);
        if (!ok) {
            free(buffer);
            return false;
        }

        buffer[len] = '\0';
        out = String(buffer);
        free(buffer);

        return true;
    }

    bool findEndOfCentralDirectory(
        File &file,
        uint32_t &outCentralDirOffset,
        uint16_t &outTotalEntries
    ) {
        const size_t fileSize = file.size();
        if (fileSize < ZIP_EOCD_MIN_SIZE) {
            return false;
        }

        const size_t searchSize = (fileSize < ZIP_EOCD_SEARCH_WINDOW)
            ? fileSize
            : ZIP_EOCD_SEARCH_WINDOW;

        uint8_t *buffer = static_cast<uint8_t *>(malloc(searchSize));
        if (!buffer) {
            return false;
        }

        const uint32_t searchOffset = static_cast<uint32_t>(fileSize - searchSize);
        if (!seekExact(file, searchOffset) || !readExact(file, buffer, searchSize)) {
            free(buffer);
            return false;
        }

        for (int i = static_cast<int>(searchSize - ZIP_EOCD_MIN_SIZE); i >= 0; --i) {
            if (readLe32(buffer + i) != ZIP_END_OF_CENTRAL_DIR_SIGNATURE) {
                continue;
            }

            const uint8_t *eocd = buffer + i;
            outTotalEntries = readLe16(eocd + 10);
            outCentralDirOffset = readLe32(eocd + 16);

            free(buffer);
            return true;
        }

        free(buffer);
        return false;
    }
}

EpubParserService::EpubParserService(fs::FS &fs)
    : m_fs(fs) {
}

bool EpubParserService::readMetadata(const String &epubPath, EpubMetadata &outMetadata) {
    outMetadata = EpubMetadata();

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file: ");
        Serial.println(epubPath);
        return false;
    }

    ZipEntryInfo containerEntry;
    if (!findZipEntry(epubFile, "META-INF/container.xml", containerEntry)) {
        Serial.println("EPUB: META-INF/container.xml not found");
        epubFile.close();
        return false;
    }

    String containerXml;
    if (!extractZipEntryText(epubFile, containerEntry, 32 * 1024, containerXml)) {
        Serial.println("EPUB: failed to extract container.xml");
        epubFile.close();
        return false;
    }

    String packagePath;
    if (!findRootFilePath(containerXml, packagePath)) {
        Serial.println("EPUB: rootfile path not found in container.xml");
        epubFile.close();
        return false;
    }

    ZipEntryInfo packageEntry;
    if (!findZipEntry(epubFile, packagePath.c_str(), packageEntry)) {
        Serial.print("EPUB: package file not found: ");
        Serial.println(packagePath);
        epubFile.close();
        return false;
    }

    String packageXml;
    if (!extractZipEntryText(epubFile, packageEntry, 256 * 1024, packageXml)) {
        Serial.print("EPUB: failed to extract package file: ");
        Serial.println(packagePath);
        epubFile.close();
        return false;
    }

    parsePackageMetadata(packageXml, outMetadata);

    Serial.print("EPUB: parsed title = ");
    Serial.println(outMetadata.title);
    Serial.print("EPUB: parsed author = ");
    Serial.println(outMetadata.author);

    epubFile.close();
    return true;
}

bool EpubParserService::findZipEntry(File &file, const char *entryPath, ZipEntryInfo &outEntry) {
    if (!entryPath || !*entryPath) {
        return false;
    }

    uint32_t centralDirOffset = 0;
    uint16_t totalEntries = 0;

    if (!findEndOfCentralDirectory(file, centralDirOffset, totalEntries)) {
        Serial.println("EPUB: failed to find end of central directory");
        return false;
    }

    if (!seekExact(file, centralDirOffset)) {
        return false;
    }

    for (uint16_t i = 0; i < totalEntries; i++) {
        uint8_t header[46];

        if (!readExact(file, header, sizeof(header))) {
            return false;
        }

        if (readLe32(header) != ZIP_CENTRAL_DIR_SIGNATURE) {
            Serial.println("EPUB: invalid central directory signature");
            return false;
        }

        const uint16_t method = readLe16(header + 10);
        const uint32_t compressedSize = readLe32(header + 20);
        const uint32_t uncompressedSize = readLe32(header + 24);
        const uint16_t fileNameLength = readLe16(header + 28);
        const uint16_t extraLength = readLe16(header + 30);
        const uint16_t commentLength = readLe16(header + 32);
        const uint32_t localHeaderOffset = readLe32(header + 42);

        String fileName;
        if (!readString(file, fileNameLength, fileName)) {
            return false;
        }

        if (!skipBytes(file, extraLength + commentLength)) {
            return false;
        }

        if (fileName == String(entryPath)) {
            outEntry.name = fileName;
            outEntry.method = method;
            outEntry.compressedSize = compressedSize;
            outEntry.uncompressedSize = uncompressedSize;
            outEntry.localHeaderOffset = localHeaderOffset;
            outEntry.isDirectory = fileName.endsWith("/");

            return true;
        }
    }

    return false;
}

bool EpubParserService::extractZipEntryText(
    File &file,
    const ZipEntryInfo &entry,
    size_t maxSize,
    String &outText
) {
    outText = "";

    if (entry.isDirectory) {
        return false;
    }

    if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxSize) {
        Serial.println("EPUB: entry too large or empty");
        return false;
    }

    if (!seekExact(file, entry.localHeaderOffset)) {
        return false;
    }

    uint8_t localHeader[30];
    if (!readExact(file, localHeader, sizeof(localHeader))) {
        return false;
    }

    if (readLe32(localHeader) != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        Serial.println("EPUB: invalid local file header signature");
        return false;
    }

    const uint16_t fileNameLength = readLe16(localHeader + 26);
    const uint16_t extraLength = readLe16(localHeader + 28);

    if (!skipBytes(file, fileNameLength + extraLength)) {
        return false;
    }

    uint8_t *compressedData = static_cast<uint8_t *>(malloc(entry.compressedSize));
    if (!compressedData) {
        return false;
    }

    if (!readExact(file, compressedData, entry.compressedSize)) {
        free(compressedData);
        return false;
    }

    char *outputData = static_cast<char *>(malloc(entry.uncompressedSize + 1));
    if (!outputData) {
        free(compressedData);
        return false;
    }

    bool ok = false;

    if (entry.method == 0) {
        memcpy(outputData, compressedData, entry.uncompressedSize);
        ok = true;
    } else if (entry.method == 8) {
        const size_t result = tinfl_decompress_mem_to_mem(
            outputData,
            entry.uncompressedSize,
            compressedData,
            entry.compressedSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
        );

        ok = (result != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
            && (result == entry.uncompressedSize);
    } else {
        Serial.print("EPUB: unsupported compression method: ");
        Serial.println(entry.method);
    }

    free(compressedData);

    if (!ok) {
        free(outputData);
        return false;
    }

    outputData[entry.uncompressedSize] = '\0';
    outText = String(outputData);
    free(outputData);

    return true;
}

bool EpubParserService::findRootFilePath(const String &containerXml, String &outRootFilePath) const {
    outRootFilePath = "";

    int searchPos = 0;

    while (true) {
        const int rootPos = containerXml.indexOf("rootfile", searchPos);
        if (rootPos < 0) {
            return false;
        }

        const int tagStart = containerXml.lastIndexOf('<', rootPos);
        const int tagEnd = containerXml.indexOf('>', rootPos);

        if (tagStart < 0 || tagEnd < 0 || tagEnd <= tagStart) {
            return false;
        }

        const String tag = containerXml.substring(tagStart, tagEnd + 1);

        if (!tag.startsWith("</")) {
            outRootFilePath = extractXmlAttributeValue(containerXml, tagStart, "full-path");

            if (!outRootFilePath.isEmpty()) {
                return true;
            }
        }

        searchPos = tagEnd + 1;
    }
}

bool EpubParserService::parsePackageMetadata(
    const String &packageXml,
    EpubMetadata &outMetadata
) const {
    outMetadata.title = extractXmlTagValue(packageXml, "dc:title");

    if (outMetadata.title.isEmpty()) {
        outMetadata.title = extractXmlTagValue(packageXml, "title");
    }

    outMetadata.author = extractXmlTagValue(packageXml, "dc:creator");

    if (outMetadata.author.isEmpty()) {
        outMetadata.author = extractXmlTagValue(packageXml, "creator");
    }

    return !outMetadata.title.isEmpty() || !outMetadata.author.isEmpty();
}

String EpubParserService::extractXmlTagValue(const String &xml, const char *tagName) const {
    if (!tagName || !*tagName) {
        return "";
    }

    const String openTagStart = "<" + String(tagName);
    const String closeTag = "</" + String(tagName) + ">";

    const int openPos = xml.indexOf(openTagStart);
    if (openPos < 0) {
        return "";
    }

    const int contentStart = xml.indexOf('>', openPos);
    if (contentStart < 0) {
        return "";
    }

    const int closePos = xml.indexOf(closeTag, contentStart + 1);
    if (closePos < 0) {
        return "";
    }

    String value = xml.substring(contentStart + 1, closePos);
    value.trim();

    if (value.startsWith("<![CDATA[") && value.endsWith("]]>") && value.length() >= 12) {
        value = value.substring(9, value.length() - 3);
    }

    value = decodeXmlEntities(value);
    value.trim();

    return value;
}

String EpubParserService::extractXmlAttributeValue(
    const String &xml,
    int tagStartPos,
    const char *attributeName
) const {
    if (tagStartPos < 0 || !attributeName || !*attributeName) {
        return "";
    }

    const int tagEnd = xml.indexOf('>', tagStartPos);
    if (tagEnd < 0) {
        return "";
    }

    const size_t attrLen = strlen(attributeName);
    int searchPos = tagStartPos;

    while (true) {
        const int attrPos = xml.indexOf(attributeName, searchPos);

        if (attrPos < 0 || attrPos >= tagEnd) {
            return "";
        }

        const int nameEnd = attrPos + static_cast<int>(attrLen);

        if (nameEnd >= tagEnd) {
            return "";
        }

        if (attrPos > tagStartPos) {
            const char prev = xml[attrPos - 1];

            if (!(isspace(static_cast<unsigned char>(prev)) || prev == '<' || prev == '/')) {
                searchPos = nameEnd;
                continue;
            }
        }

        int pos = nameEnd;

        while (pos < tagEnd && isspace(static_cast<unsigned char>(xml[pos]))) {
            pos++;
        }

        if (pos >= tagEnd || xml[pos] != '=') {
            searchPos = nameEnd;
            continue;
        }

        pos++;

        while (pos < tagEnd && isspace(static_cast<unsigned char>(xml[pos]))) {
            pos++;
        }

        if (pos >= tagEnd) {
            return "";
        }

        const char quote = xml[pos];

        if (quote != '"' && quote != '\'') {
            searchPos = pos + 1;
            continue;
        }

        const int valueStart = pos + 1;
        const int valueEnd = xml.indexOf(quote, valueStart);

        if (valueEnd < 0 || valueEnd > tagEnd) {
            return "";
        }

        String value = xml.substring(valueStart, valueEnd);
        value = decodeXmlEntities(value);
        value.trim();

        return value;
    }
}

String EpubParserService::decodeXmlEntities(String value) const {
    value.replace("&amp;", "&");
    value.replace("&quot;", "\"");
    value.replace("&apos;", "'");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    value.replace("&#39;", "'");
    value.replace("&#34;", "\"");

    return value;
}