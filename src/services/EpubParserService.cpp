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

bool EpubParserService::extractZipEntryData(
    File &file,
    const ZipEntryInfo &entry,
    size_t maxSize,
    uint8_t *&outData,
    size_t &outSize
) {
    outData = nullptr;
    outSize = 0;

    if (entry.isDirectory) {
        return false;
    }

    if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxSize) {
        Serial.println("EPUB: entry too large or empty");
        return false;
    }

    if (!seekExact(file, entry.localHeaderOffset)) {
        Serial.println("EPUB: failed to seek local header");
        return false;
    }

    uint8_t localHeader[30];
    if (!readExact(file, localHeader, sizeof(localHeader))) {
        Serial.println("EPUB: failed to read local header");
        return false;
    }

    if (readLe32(localHeader) != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        Serial.println("EPUB: invalid local file header signature");
        return false;
    }

    const uint16_t fileNameLength = readLe16(localHeader + 26);
    const uint16_t extraLength = readLe16(localHeader + 28);

    if (!skipBytes(file, fileNameLength + extraLength)) {
        Serial.println("EPUB: failed to skip local header fields");
        return false;
    }

    if (entry.method == 0) {
        uint8_t *outputData = static_cast<uint8_t *>(malloc(entry.uncompressedSize));
        if (!outputData) {
            Serial.println("EPUB: failed to allocate output buffer for stored entry");
            return false;
        }

        if (!readExact(file, outputData, entry.uncompressedSize)) {
            Serial.println("EPUB: failed to read stored entry data");
            free(outputData);
            return false;
        }

        outData = outputData;
        outSize = entry.uncompressedSize;
        return true;
    }

    uint8_t *compressedData = static_cast<uint8_t *>(malloc(entry.compressedSize));
    if (!compressedData) {
        Serial.println("EPUB: failed to allocate compressed buffer");
        return false;
    }

    if (!readExact(file, compressedData, entry.compressedSize)) {
        Serial.println("EPUB: failed to read compressed entry data");
        free(compressedData);
        return false;
    }

    uint8_t *outputData = static_cast<uint8_t *>(malloc(entry.uncompressedSize));
    if (!outputData) {
        Serial.println("EPUB: failed to allocate output buffer");
        free(compressedData);
        return false;
    }

    bool ok = false;

    if (entry.method == 8) {
        const size_t result = tinfl_decompress_mem_to_mem(
            outputData,
            entry.uncompressedSize,
            compressedData,
            entry.compressedSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
        );

        ok = (result != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
            && (result == entry.uncompressedSize);

        if (!ok) {
            Serial.print("EPUB: deflate decompression failed, result=");
            Serial.println(static_cast<unsigned long>(result));
        }
    } else {
        Serial.print("EPUB: unsupported compression method: ");
        Serial.println(entry.method);
    }

    free(compressedData);

    if (!ok) {
        free(outputData);
        return false;
    }

    outData = outputData;
    outSize = entry.uncompressedSize;

    return true;
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

    parsePackageMetadata(packageXml, packagePath, outMetadata);

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

    uint8_t *data = nullptr;
    size_t size = 0;

    if (!extractZipEntryData(file, entry, maxSize, data, size)) {
        return false;
    }

    char *textBuffer = static_cast<char *>(malloc(size + 1));
    if (!textBuffer) {
        free(data);
        return false;
    }

    memcpy(textBuffer, data, size);
    textBuffer[size] = '\0';

    outText = String(textBuffer);

    free(textBuffer);
    free(data);

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
    const String &packagePath,
    EpubMetadata &outMetadata
) const {
    outMetadata.title = extractXmlTagValue(packageXml, "dc:title");

    if (outMetadata.title.isEmpty()) {
        outMetadata.title = extractXmlTagValue(packageXml, "title");
    }

    outMetadata.author = extractXmlTagValuesJoined(packageXml, "dc:creator");

    if (outMetadata.author.isEmpty()) {
        outMetadata.author = extractXmlTagValuesJoined(packageXml, "creator");
    }

    outMetadata.coverInternalPath = "";
    outMetadata.hasCover = false;

    String metaCoverId = "";
    int searchPos = 0;

    while (true) {
        const int metaPos = packageXml.indexOf("<meta", searchPos);
        if (metaPos < 0) {
            break;
        }

        const int tagEnd = packageXml.indexOf('>', metaPos);
        if (tagEnd < 0) {
            break;
        }

        String name = extractXmlAttributeValue(packageXml, metaPos, "name");
        name.toLowerCase();

        if (name == "cover") {
            metaCoverId = extractXmlAttributeValue(packageXml, metaPos, "content");
            break;
        }

        searchPos = tagEnd + 1;
    }

    String coverHrefByProperties = "";
    String coverHrefByMetaId = "";
    String fallbackCoverHref = "";

    searchPos = 0;

    while (true) {
        const int itemPos = packageXml.indexOf("<item", searchPos);
        if (itemPos < 0) {
            break;
        }

        const int tagEnd = packageXml.indexOf('>', itemPos);
        if (tagEnd < 0) {
            break;
        }

        String id = extractXmlAttributeValue(packageXml, itemPos, "id");
        String href = extractXmlAttributeValue(packageXml, itemPos, "href");
        String mediaType = extractXmlAttributeValue(packageXml, itemPos, "media-type");
        String properties = extractXmlAttributeValue(packageXml, itemPos, "properties");

        String idLower = id;
        String hrefLower = href;
        String mediaTypeLower = mediaType;
        String propertiesLower = properties;

        idLower.toLowerCase();
        hrefLower.toLowerCase();
        mediaTypeLower.toLowerCase();
        propertiesLower.toLowerCase();

        const bool isImage = isImageMediaType(mediaTypeLower);

        if (coverHrefByProperties.isEmpty()
            && isImage
            && propertiesLower.indexOf("cover-image") >= 0) {
            coverHrefByProperties = href;
        }

        if (coverHrefByMetaId.isEmpty()
            && !metaCoverId.isEmpty()
            && id == metaCoverId
            && isImage) {
            coverHrefByMetaId = href;
        }

        if (fallbackCoverHref.isEmpty()
            && isImage
            && (idLower.indexOf("cover") >= 0 || hrefLower.indexOf("cover") >= 0)) {
            fallbackCoverHref = href;
        }

        searchPos = tagEnd + 1;
    }

    String selectedCoverHref = "";

    if (!coverHrefByProperties.isEmpty()) {
        selectedCoverHref = coverHrefByProperties;
    } else if (!coverHrefByMetaId.isEmpty()) {
        selectedCoverHref = coverHrefByMetaId;
    } else if (!fallbackCoverHref.isEmpty()) {
        selectedCoverHref = fallbackCoverHref;
    }

    if (!selectedCoverHref.isEmpty()) {
        outMetadata.coverInternalPath = resolveRelativePath(packagePath, selectedCoverHref);
        outMetadata.hasCover = !outMetadata.coverInternalPath.isEmpty();
    }

    return !outMetadata.title.isEmpty()
        || !outMetadata.author.isEmpty()
        || outMetadata.hasCover;
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

String EpubParserService::extractXmlTagValuesJoined(const String &xml, const char *tagName) const {
    if (!tagName || !*tagName) {
        return "";
    }

    const String openTagStart = "<" + String(tagName);
    const String closeTag = "</" + String(tagName) + ">";

    String result = "";
    int searchPos = 0;

    while (true) {
        const int openPos = xml.indexOf(openTagStart, searchPos);
        if (openPos < 0) {
            break;
        }

        const int contentStart = xml.indexOf('>', openPos);
        if (contentStart < 0) {
            break;
        }

        const int closePos = xml.indexOf(closeTag, contentStart + 1);
        if (closePos < 0) {
            break;
        }

        String value = xml.substring(contentStart + 1, closePos);
        value.trim();

        if (value.startsWith("<![CDATA[") && value.endsWith("]]>") && value.length() >= 12) {
            value = value.substring(9, value.length() - 3);
        }

        value = decodeXmlEntities(value);
        value.trim();

        if (!value.isEmpty()) {
            if (!result.isEmpty()) {
                result += ", ";
            }

            result += value;
        }

        searchPos = closePos + closeTag.length();
    }

    return result;
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

String EpubParserService::normalizePath(const String &path) const {
    String result = "";
    int start = 0;
    const int len = path.length();

    while (start <= len) {
        int slashPos = path.indexOf('/', start);
        if (slashPos < 0) {
            slashPos = len;
        }

        String part = path.substring(start, slashPos);
        part.trim();

        if (!part.isEmpty() && part != ".") {
            if (part == "..") {
                const int lastSlash = result.lastIndexOf('/');

                if (lastSlash >= 0) {
                    result.remove(lastSlash);
                } else {
                    result = "";
                }
            } else {
                if (!result.isEmpty()) {
                    result += "/";
                }

                result += part;
            }
        }

        if (slashPos >= len) {
            break;
        }

        start = slashPos + 1;
    }

    return result;
}

String EpubParserService::resolveRelativePath(const String &baseFilePath, const String &relativePath) const {
    if (relativePath.isEmpty()) {
        return "";
    }

    if (relativePath[0] == '/') {
        return normalizePath(relativePath.substring(1));
    }

    const int slashPos = baseFilePath.lastIndexOf('/');
    const String baseDir = (slashPos >= 0)
        ? baseFilePath.substring(0, slashPos + 1)
        : "";

    return normalizePath(baseDir + relativePath);
}

String EpubParserService::getLowerFileExtension(const String &path) const {
    const int slashPos = path.lastIndexOf('/');
    const int dotPos = path.lastIndexOf('.');

    if (dotPos < 0 || (slashPos >= 0 && dotPos < slashPos)) {
        return "";
    }

    String ext = path.substring(dotPos);
    ext.toLowerCase();

    return ext;
}

bool EpubParserService::isImageMediaType(const String &mediaType) const {
    return mediaType == "image/jpeg"
        || mediaType == "image/jpg"
        || mediaType == "image/png"
        || mediaType == "image/gif"
        || mediaType == "image/webp"
        || mediaType == "image/svg+xml";
}

bool EpubParserService::extractCoverToFile(
    const String &epubPath,
    const EpubMetadata &metadata,
    const String &outputBasePath,
    String &outCoverPath
) {
    outCoverPath = "";

    if (!metadata.hasCover || metadata.coverInternalPath.isEmpty()) {
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.println("EPUB: failed to open file for cover extraction");
        return false;
    }

    ZipEntryInfo coverEntry;
    if (!findZipEntry(epubFile, metadata.coverInternalPath.c_str(), coverEntry)) {
        Serial.print("EPUB: cover entry not found: ");
        Serial.println(metadata.coverInternalPath);
        epubFile.close();
        return false;
    }

    uint8_t *coverData = nullptr;
    size_t coverSize = 0;

    if (!extractZipEntryData(epubFile, coverEntry, 5 * 1024 * 1024, coverData, coverSize)) {
        Serial.println("EPUB: failed to extract cover data");
        epubFile.close();
        return false;
    }

    epubFile.close();

    String ext = getLowerFileExtension(metadata.coverInternalPath);
    if (ext.isEmpty()) {
        ext = ".jpg";
    }

    const String outputPath = outputBasePath + ext;

    if (m_fs.exists(outputPath)) {
        m_fs.remove(outputPath);
    }

    File outFile = m_fs.open(outputPath, "w");
    if (!outFile) {
        Serial.print("EPUB: failed to open cover output file: ");
        Serial.println(outputPath);
        free(coverData);
        return false;
    }

    const size_t written = outFile.write(coverData, coverSize);
    outFile.close();
    free(coverData);

    if (written != coverSize) {
        Serial.print("EPUB: failed to fully write cover file: ");
        Serial.println(outputPath);
        return false;
    }

    outCoverPath = outputPath;

    Serial.print("EPUB: cover saved to ");
    Serial.println(outCoverPath);

    return true;
}