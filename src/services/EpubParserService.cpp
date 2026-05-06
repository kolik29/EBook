#include "EpubParserService.h"
#include "BookTextCodec.h"

#include <Arduino.h>
#include <rom/miniz.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "../utils/DebugLog.h"

namespace {
    constexpr uint32_t ZIP_LOCAL_FILE_HEADER_SIGNATURE = 0x04034b50UL;
    constexpr uint32_t ZIP_CENTRAL_DIR_SIGNATURE = 0x02014b50UL;
    constexpr uint32_t ZIP_END_OF_CENTRAL_DIR_SIGNATURE = 0x06054b50UL;
    constexpr size_t ZIP_EOCD_MIN_SIZE = 22;
    constexpr size_t ZIP_MAX_COMMENT_SIZE = 0xFFFF;
    constexpr size_t ZIP_EOCD_SEARCH_WINDOW = ZIP_EOCD_MIN_SIZE + ZIP_MAX_COMMENT_SIZE;
    constexpr size_t ZIP_INFLATE_INPUT_CHUNK = 1024;

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

    bool inflateZipEntryFromFile(
        File &file,
        const String &entryName,
        uint32_t compressedSize,
        uint32_t uncompressedSize,
        uint8_t *outputData,
        String &errorDetail
    ) {
        tinfl_decompressor decompressor;
        tinfl_init(&decompressor);

        uint8_t inputBuffer[ZIP_INFLATE_INPUT_CHUNK];
        size_t inputAvailable = 0;
        size_t inputOffset = 0;
        size_t outputOffset = 0;
        uint32_t compressedRemaining = compressedSize;

        while (true) {
            if (inputOffset >= inputAvailable && compressedRemaining > 0) {
                const size_t nextChunk = compressedRemaining > ZIP_INFLATE_INPUT_CHUNK
                    ? ZIP_INFLATE_INPUT_CHUNK
                    : compressedRemaining;
                inputAvailable = file.read(inputBuffer, nextChunk);
                inputOffset = 0;

                if (inputAvailable == 0) {
                    errorDetail = entryName + " read returned 0 during inflate";
                    return false;
                }

                compressedRemaining -= static_cast<uint32_t>(inputAvailable);
            }

            size_t inputSize = inputAvailable - inputOffset;
            size_t outputSize = uncompressedSize - outputOffset;
            mz_uint32 flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;

            if (compressedRemaining > 0) {
                flags |= TINFL_FLAG_HAS_MORE_INPUT;
            }

            const tinfl_status status = tinfl_decompress(
                &decompressor,
                inputBuffer + inputOffset,
                &inputSize,
                outputData,
                outputData + outputOffset,
                &outputSize,
                flags
            );

            inputOffset += inputSize;
            outputOffset += outputSize;

            if (status == TINFL_STATUS_DONE) {
                if (outputOffset != uncompressedSize) {
                    errorDetail = entryName;
                    errorDetail += " output=";
                    errorDetail += String(static_cast<unsigned long>(outputOffset));
                    errorDetail += " expected=";
                    errorDetail += String(static_cast<unsigned long>(uncompressedSize));
                    return false;
                }

                return true;
            }

            if (status < 0) {
                errorDetail = entryName;
                errorDetail += " status=";
                errorDetail += String(static_cast<int>(status));
                errorDetail += " output=";
                errorDetail += String(static_cast<unsigned long>(outputOffset));
                return false;
            }

            if (status == TINFL_STATUS_HAS_MORE_OUTPUT
                && outputOffset >= uncompressedSize) {
                errorDetail = entryName + " output buffer exhausted";
                return false;
            }

            if (status == TINFL_STATUS_NEEDS_MORE_INPUT
                && compressedRemaining == 0
                && inputOffset >= inputAvailable) {
                errorDetail = entryName + " input exhausted";
                return false;
            }
        }
    }

    bool inflateZipEntryToFile(
        File &file,
        File &outFile,
        const String &entryName,
        uint32_t compressedSize,
        uint32_t uncompressedSize,
        String &errorDetail
    ) {
        tinfl_decompressor decompressor;
        tinfl_init(&decompressor);

        uint8_t inputBuffer[ZIP_INFLATE_INPUT_CHUNK];
        uint8_t *dictBuffer = static_cast<uint8_t *>(malloc(TINFL_LZ_DICT_SIZE));
        if (!dictBuffer) {
            errorDetail = entryName + " dictionary allocation failed";
            return false;
        }

        size_t inputAvailable = 0;
        size_t inputOffset = 0;
        size_t dictOffset = 0;
        uint32_t compressedRemaining = compressedSize;
        uint32_t outputRemaining = uncompressedSize;
        uint32_t totalOutput = 0;

        while (true) {
            if (inputOffset >= inputAvailable && compressedRemaining > 0) {
                const size_t nextChunk = compressedRemaining > ZIP_INFLATE_INPUT_CHUNK
                    ? ZIP_INFLATE_INPUT_CHUNK
                    : compressedRemaining;
                inputAvailable = file.read(inputBuffer, nextChunk);
                inputOffset = 0;

                if (inputAvailable == 0) {
                    errorDetail = entryName + " read returned 0 during stream inflate";
                    free(dictBuffer);
                    return false;
                }

                compressedRemaining -= static_cast<uint32_t>(inputAvailable);
            }

            if (dictOffset >= TINFL_LZ_DICT_SIZE) {
                dictOffset = 0;
            }

            size_t inputSize = inputAvailable - inputOffset;
            size_t outputSize = TINFL_LZ_DICT_SIZE - dictOffset;
            if (outputSize > outputRemaining) {
                outputSize = outputRemaining;
            }

            mz_uint32 flags = 0;
            if (compressedRemaining > 0) {
                flags |= TINFL_FLAG_HAS_MORE_INPUT;
            }

            const tinfl_status status = tinfl_decompress(
                &decompressor,
                inputBuffer + inputOffset,
                &inputSize,
                dictBuffer,
                dictBuffer + dictOffset,
                &outputSize,
                flags
            );

            inputOffset += inputSize;

            if (outputSize > 0) {
                const size_t written = outFile.write(dictBuffer + dictOffset, outputSize);
                if (written != outputSize) {
                    errorDetail = entryName + " output file write failed";
                    free(dictBuffer);
                    return false;
                }

                dictOffset += outputSize;
                totalOutput += static_cast<uint32_t>(outputSize);
                outputRemaining -= static_cast<uint32_t>(outputSize);
            }

            if (status == TINFL_STATUS_DONE) {
                free(dictBuffer);

                if (totalOutput != uncompressedSize) {
                    errorDetail = entryName;
                    errorDetail += " streamed output=";
                    errorDetail += String(static_cast<unsigned long>(totalOutput));
                    errorDetail += " expected=";
                    errorDetail += String(static_cast<unsigned long>(uncompressedSize));
                    return false;
                }

                return true;
            }

            if (status < 0) {
                errorDetail = entryName;
                errorDetail += " stream status=";
                errorDetail += String(static_cast<int>(status));
                errorDetail += " output=";
                errorDetail += String(static_cast<unsigned long>(totalOutput));
                free(dictBuffer);
                return false;
            }

            if (inputSize == 0 && outputSize == 0) {
                errorDetail = entryName + " stream inflate made no progress";
                free(dictBuffer);
                return false;
            }

            if (status == TINFL_STATUS_NEEDS_MORE_INPUT
                && compressedRemaining == 0
                && inputOffset >= inputAvailable) {
                errorDetail = entryName + " stream input exhausted";
                free(dictBuffer);
                return false;
            }
        }
    }

    int hexNibble(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    String percentDecodePath(const String &value) {
        String decoded = "";
        decoded.reserve(value.length());

        for (int i = 0; i < value.length(); i++) {
            if (value[i] == '%' && i + 2 < value.length()) {
                const int hi = hexNibble(value[i + 1]);
                const int lo = hexNibble(value[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    decoded += static_cast<char>((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }

            decoded += value[i];
        }

        return decoded;
    }

    String normalizeZipLookupPath(String path) {
        path.replace("\\", "/");

        const int hashPos = path.indexOf('#');
        if (hashPos >= 0) {
            path = path.substring(0, hashPos);
        }

        const int queryPos = path.indexOf('?');
        if (queryPos >= 0) {
            path = path.substring(0, queryPos);
        }

        while (path.startsWith("/")) {
            path = path.substring(1);
        }

        path = percentDecodePath(path);

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
        const size_t searchStartLimit = fileSize - searchSize;

        constexpr size_t CHUNK_SIZE = 1024;
        constexpr size_t OVERLAP_SIZE = ZIP_EOCD_MIN_SIZE - 1;
        uint8_t buffer[CHUNK_SIZE];

        size_t chunkEnd = fileSize;

        while (chunkEnd > searchStartLimit) {
            size_t chunkStart = chunkEnd > CHUNK_SIZE
                ? chunkEnd - CHUNK_SIZE
                : 0;

            if (chunkStart < searchStartLimit) {
                chunkStart = searchStartLimit;
            }

            const size_t chunkSize = chunkEnd - chunkStart;

            if (chunkSize < ZIP_EOCD_MIN_SIZE) {
                break;
            }

            if (!seekExact(file, static_cast<uint32_t>(chunkStart))
                || !readExact(file, buffer, chunkSize)) {
                return false;
            }

            for (int i = static_cast<int>(chunkSize - ZIP_EOCD_MIN_SIZE); i >= 0; --i) {
                if (readLe32(buffer + i) != ZIP_END_OF_CENTRAL_DIR_SIGNATURE) {
                    continue;
                }

                const uint8_t *eocd = buffer + i;
                outTotalEntries = readLe16(eocd + 10);
                outCentralDirOffset = readLe32(eocd + 16);

                return true;
            }

            if (chunkStart == searchStartLimit) {
                break;
            }

            chunkEnd = chunkStart + OVERLAP_SIZE;
        }

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
        setLastError("ZIP entry is a directory", entry.name);
        return false;
    }

    if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxSize) {
        Serial.println("EPUB: entry too large or empty");
        String detail = entry.name;
        detail += " size=";
        detail += String(static_cast<unsigned long>(entry.uncompressedSize));
        detail += " max=";
        detail += String(static_cast<unsigned long>(maxSize));
        setLastError("ZIP entry is empty or too large", detail);
        return false;
    }

    if (!seekToZipEntryPayload(file, entry)) {
        return false;
    }

    if (entry.method == 0) {
        uint8_t *outputData = static_cast<uint8_t *>(malloc(entry.uncompressedSize + 1));
        if (!outputData) {
            Serial.println("EPUB: failed to allocate output buffer for stored entry");
            setLastError("Not enough memory for stored ZIP entry", entry.name);
            return false;
        }

        if (!readExact(file, outputData, entry.uncompressedSize)) {
            Serial.println("EPUB: failed to read stored entry data");
            setLastError("Cannot read stored ZIP entry data", entry.name);
            free(outputData);
            return false;
        }

        outputData[entry.uncompressedSize] = '\0';
        outData = outputData;
        outSize = entry.uncompressedSize;
        return true;
    }

    uint8_t *outputData = static_cast<uint8_t *>(malloc(entry.uncompressedSize + 1));
    if (!outputData) {
        Serial.println("EPUB: failed to allocate output buffer");
        setLastError("Not enough memory to inflate ZIP entry", entry.name);
        return false;
    }

    bool ok = false;

    if (entry.method == 8) {
        String detail;
        ok = inflateZipEntryFromFile(
            file,
            entry.name,
            entry.compressedSize,
            entry.uncompressedSize,
            outputData,
            detail
        );

        if (!ok) {
            Serial.print("EPUB: deflate decompression failed: ");
            Serial.println(detail);
            setLastError("Deflate decompression failed", detail);
        }
    } else {
        Serial.print("EPUB: unsupported compression method: ");
        Serial.println(entry.method);
        String detail = entry.name;
        detail += " method=";
        detail += String(entry.method);
        setLastError("Unsupported ZIP compression method", detail);
    }

    if (!ok) {
        free(outputData);
        return false;
    }

    outputData[entry.uncompressedSize] = '\0';
    outData = outputData;
    outSize = entry.uncompressedSize;

    return true;
}

EpubParserService::EpubParserService(fs::FS &fs)
    : m_fs(fs) {
}

bool EpubParserService::seekToZipEntryPayload(File &file, const ZipEntryInfo &entry) {
    if (!seekExact(file, entry.localHeaderOffset)) {
        Serial.println("EPUB: failed to seek local header");
        setLastError("Cannot seek to ZIP local header", entry.name);
        return false;
    }

    uint8_t localHeader[30];
    if (!readExact(file, localHeader, sizeof(localHeader))) {
        Serial.println("EPUB: failed to read local header");
        setLastError("Cannot read ZIP local header", entry.name);
        return false;
    }

    if (readLe32(localHeader) != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        Serial.println("EPUB: invalid local file header signature");
        setLastError("Invalid ZIP local header signature", entry.name);
        return false;
    }

    const uint16_t fileNameLength = readLe16(localHeader + 26);
    const uint16_t extraLength = readLe16(localHeader + 28);

    if (!skipBytes(file, fileNameLength + extraLength)) {
        Serial.println("EPUB: failed to skip local header fields");
        setLastError("Cannot skip ZIP local header fields", entry.name);
        return false;
    }

    return true;
}

bool EpubParserService::extractZipEntryToFile(
    File &file,
    const ZipEntryInfo &entry,
    size_t maxSize,
    const String &outputPath
) {
    if (entry.isDirectory) {
        setLastError("ZIP entry is a directory", entry.name);
        return false;
    }

    if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxSize) {
        String detail = entry.name;
        detail += " size=";
        detail += String(static_cast<unsigned long>(entry.uncompressedSize));
        detail += " max=";
        detail += String(static_cast<unsigned long>(maxSize));
        setLastError("ZIP entry is empty or too large", detail);
        return false;
    }

    if (m_fs.exists(outputPath)) {
        m_fs.remove(outputPath);
    }

    File outFile = m_fs.open(outputPath, "w");
    if (!outFile) {
        Serial.print("EPUB: failed to open output file: ");
        Serial.println(outputPath);
        setLastError("Cannot open output file", outputPath);
        return false;
    }

    if (entry.method == 0) {
        if (!seekToZipEntryPayload(file, entry)) {
            outFile.close();
            m_fs.remove(outputPath);
            return false;
        }

        uint8_t buffer[1024];
        uint32_t remaining = entry.uncompressedSize;

        while (remaining > 0) {
            const size_t chunkSize = remaining > sizeof(buffer)
                ? sizeof(buffer)
                : remaining;
            const size_t bytesRead = file.read(buffer, chunkSize);

            if (bytesRead == 0) {
                outFile.close();
                m_fs.remove(outputPath);
                setLastError("Cannot read stored ZIP entry data", entry.name);
                return false;
            }

            const size_t written = outFile.write(buffer, bytesRead);
            if (written != bytesRead) {
                outFile.close();
                m_fs.remove(outputPath);
                setLastError("Cannot write output file", outputPath);
                return false;
            }

            remaining -= static_cast<uint32_t>(bytesRead);
        }

        outFile.close();
        return true;
    }

    if (entry.method == 8) {
        if (!seekToZipEntryPayload(file, entry)) {
            outFile.close();
            m_fs.remove(outputPath);
            return false;
        }

        String detail;
        const bool ok = inflateZipEntryToFile(
            file,
            outFile,
            entry.name,
            entry.compressedSize,
            entry.uncompressedSize,
            detail
        );

        outFile.close();

        if (!ok) {
            Serial.print("EPUB: stream deflate to file failed: ");
            Serial.println(detail);
            m_fs.remove(outputPath);
            setLastError("Deflate decompression to file failed", detail);
            return false;
        }

        return true;
    }

    outFile.close();

    uint8_t *data = nullptr;
    size_t size = 0;

    if (!extractZipEntryData(file, entry, maxSize, data, size)) {
        m_fs.remove(outputPath);
        return false;
    }

    outFile = m_fs.open(outputPath, "w");
    if (!outFile) {
        free(data);
        m_fs.remove(outputPath);
        setLastError("Cannot open output file", outputPath);
        return false;
    }

    const size_t written = outFile.write(data, size);
    outFile.close();
    free(data);

    if (written != size) {
        m_fs.remove(outputPath);
        setLastError("Cannot write output file", outputPath);
        return false;
    }

    return true;
}

void EpubParserService::clearLastError() {
    m_lastError = "";
}

void EpubParserService::setLastError(const String &message) {
    m_lastError = message;
}

void EpubParserService::setLastError(const String &message, const String &detail) {
    m_lastError = message;

    if (!detail.isEmpty()) {
        m_lastError += ": ";
        m_lastError += detail;
    }
}

void EpubParserService::setLastErrorIfEmpty(const String &message) {
    if (m_lastError.isEmpty()) {
        m_lastError = message;
    }
}

void EpubParserService::setLastErrorIfEmpty(const String &message, const String &detail) {
    if (m_lastError.isEmpty()) {
        setLastError(message, detail);
    }
}

bool EpubParserService::readMetadata(const String &epubPath, EpubMetadata &outMetadata) {
    clearLastError();
    outMetadata = EpubMetadata();

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file", epubPath);
        return false;
    }

    ZipEntryInfo containerEntry;
    if (!findZipEntry(epubFile, "META-INF/container.xml", containerEntry)) {
        Serial.println("EPUB: META-INF/container.xml not found");
        setLastErrorIfEmpty("META-INF/container.xml was not found in the EPUB archive");
        epubFile.close();
        return false;
    }

    String containerXml;
    if (!extractZipEntryText(epubFile, containerEntry, 32 * 1024, containerXml)) {
        Serial.println("EPUB: failed to extract container.xml");
        setLastErrorIfEmpty("Cannot read META-INF/container.xml");
        epubFile.close();
        return false;
    }

    String packagePath;
    if (!findRootFilePath(containerXml, packagePath)) {
        Serial.println("EPUB: rootfile path not found in container.xml");
        setLastError("container.xml does not contain a rootfile full-path");
        epubFile.close();
        return false;
    }

    ZipEntryInfo packageEntry;
    if (!findZipEntry(epubFile, packagePath.c_str(), packageEntry)) {
        Serial.print("EPUB: package file not found: ");
        Serial.println(packagePath);
        setLastErrorIfEmpty("OPF package file was not found", packagePath);
        epubFile.close();
        return false;
    }

    String packageXml;
    if (!extractZipEntryText(epubFile, packageEntry, 256 * 1024, packageXml)) {
        Serial.print("EPUB: failed to extract package file: ");
        Serial.println(packagePath);
        setLastErrorIfEmpty("Cannot read OPF package file", packagePath);
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
        setLastError("ZIP entry path is empty");
        return false;
    }

    const String requestedPath = normalizeZipLookupPath(String(entryPath));
    String requestedLower = requestedPath;
    requestedLower.toLowerCase();

    uint32_t centralDirOffset = 0;
    uint16_t totalEntries = 0;

    if (!findEndOfCentralDirectory(file, centralDirOffset, totalEntries)) {
        Serial.println("EPUB: failed to find end of central directory");
        setLastError("ZIP end-of-central-directory was not found; file may be damaged or not an EPUB");
        return false;
    }

    if (!seekExact(file, centralDirOffset)) {
        setLastError("Cannot seek to ZIP central directory");
        return false;
    }

    for (uint16_t i = 0; i < totalEntries; i++) {
        uint8_t header[46];

        if (!readExact(file, header, sizeof(header))) {
            setLastError("Cannot read ZIP central directory header");
            return false;
        }

        if (readLe32(header) != ZIP_CENTRAL_DIR_SIGNATURE) {
            Serial.println("EPUB: invalid central directory signature");
            setLastError("Invalid ZIP central directory signature");
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
            setLastError("Cannot read ZIP entry name");
            return false;
        }

        if (!skipBytes(file, extraLength + commentLength)) {
            setLastError("Cannot skip ZIP central directory extra fields");
            return false;
        }

        const String normalizedFileName = normalizeZipLookupPath(fileName);
        String normalizedFileNameLower = normalizedFileName;
        normalizedFileNameLower.toLowerCase();

        if (fileName == String(entryPath)
            || normalizedFileName == requestedPath
            || normalizedFileNameLower == requestedLower) {
            outEntry.name = fileName;
            outEntry.method = method;
            outEntry.compressedSize = compressedSize;
            outEntry.uncompressedSize = uncompressedSize;
            outEntry.localHeaderOffset = localHeaderOffset;
            outEntry.isDirectory = fileName.endsWith("/");

            return true;
        }
    }

    Serial.print("EPUB: ZIP entry not found, requested=");
    Serial.println(requestedPath);
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

    if (!outText.reserve(static_cast<unsigned int>(size))) {
        setLastError("Not enough memory to create text String", entry.name);
        free(data);
        return false;
    }

    if (!outText.concat(data, static_cast<unsigned int>(size))) {
        setLastError("Failed to copy ZIP text entry into String", entry.name);
        free(data);
        return false;
    }

    free(data);

    return true;
}

bool EpubParserService::extractZipEntryText(
    File &file,
    const ZipEntryInfo &entry,
    size_t maxSize,
    std::string &outText
) {
    outText.clear();

    if (entry.isDirectory) {
        setLastError("ZIP entry is a directory", entry.name);
        return false;
    }

    if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxSize) {
        Serial.println("EPUB: text entry too large or empty");
        String detail = entry.name;
        detail += " size=";
        detail += String(static_cast<unsigned long>(entry.uncompressedSize));
        detail += " max=";
        detail += String(static_cast<unsigned long>(maxSize));
        setLastError("ZIP text entry is empty or too large", detail);
        return false;
    }

    if (entry.method != 0 && entry.method != 8) {
        Serial.print("EPUB: unsupported compression method: ");
        Serial.println(entry.method);
        String detail = entry.name;
        detail += " method=";
        detail += String(entry.method);
        setLastError("Unsupported ZIP compression method", detail);
        return false;
    }

    if (!seekToZipEntryPayload(file, entry)) {
        return false;
    }

    try {
        outText.resize(static_cast<size_t>(entry.uncompressedSize));
    } catch (...) {
        Serial.println("EPUB: failed to allocate text buffer");
        outText.clear();
        setLastError("Not enough memory to create text buffer", entry.name);
        return false;
    }

    uint8_t *outputData = reinterpret_cast<uint8_t *>(&outText[0]);

    if (entry.method == 0) {
        if (!readExact(file, outputData, entry.uncompressedSize)) {
            Serial.println("EPUB: failed to read stored text entry data");
            outText.clear();
            setLastError("Cannot read stored ZIP text entry data", entry.name);
            return false;
        }

        return true;
    }

    String detail;
    if (!inflateZipEntryFromFile(
        file,
        entry.name,
        entry.compressedSize,
        entry.uncompressedSize,
        outputData,
        detail
    )) {
        Serial.print("EPUB: deflate text decompression failed: ");
        Serial.println(detail);
        outText.clear();
        setLastError("Deflate text decompression failed", detail);
        return false;
    }

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
    return BookTextCodec::decodeHtmlEntities(value);
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

namespace {
    struct ManifestItem {
        String id;
        String href;
        String mediaType;
    };

    String extractXmlBlock(const String &xml, const String &tagName) {
        const String openTag = "<" + tagName;
        const String closeTag = "</" + tagName + ">";

        const int openPos = xml.indexOf(openTag);
        if (openPos < 0) {
            return "";
        }

        const int openEnd = xml.indexOf('>', openPos);
        if (openEnd < 0) {
            return "";
        }

        const int closePos = xml.indexOf(closeTag, openEnd + 1);
        if (closePos < 0) {
            return "";
        }

        return xml.substring(openEnd + 1, closePos);
    }

    ManifestItem *findManifestItemById(std::vector<ManifestItem> &items, const String &id) {
        for (ManifestItem &item : items) {
            if (item.id == id) {
                return &item;
            }
        }

        return nullptr;
    }

    bool looksLikeTextDocument(const ManifestItem &item) {
        String mediaType = item.mediaType;
        String href = item.href;

        mediaType.toLowerCase();
        href.toLowerCase();

        return mediaType == "application/xhtml+xml"
            || mediaType == "text/html"
            || mediaType == "application/xml"
            || href.endsWith(".xhtml")
            || href.endsWith(".html")
            || href.endsWith(".htm");
    }

    String cleanupHtmlToText(String html) {
        auto removeBlocks = [](String &s, const char *open, const char *close) {
            while (true) {
                String lower = s; lower.toLowerCase();
                int start = lower.indexOf(open);
                if (start < 0) break;
                int end = lower.indexOf(close, start);
                if (end < 0) break;
                s.remove(start, end + strlen(close) - start);
            }
        };
        removeBlocks(html, "<style",  "</style>");
        removeBlocks(html, "<script", "</script>");
        
        html.replace("\r", "\n");
        html = BookTextCodec::decodeHtmlEntities(html);

        String result = "";
        bool insideTag = false;
        bool lastWasSpace = false;
        bool lastWasNewLine = false;

        for (int i = 0; i < html.length(); i++) {
            const char c = html[i];

            if (c == '<') {
                insideTag = true;

                int tagPreviewEnd = i + 12;

                if (tagPreviewEnd > static_cast<int>(html.length())) {
                    tagPreviewEnd = html.length();
                }

                String tag = html.substring(i, tagPreviewEnd);
                tag.toLowerCase();

                if (tag.startsWith("<p")
                    || tag.startsWith("</p")
                    || tag.startsWith("<br")
                    || tag.startsWith("<div")
                    || tag.startsWith("</div")
                    || tag.startsWith("<h1")
                    || tag.startsWith("</h1")
                    || tag.startsWith("<h2")
                    || tag.startsWith("</h2")
                    || tag.startsWith("<h3")
                    || tag.startsWith("</h3")) {
                    if (!lastWasNewLine) {
                        result += '\n';
                        lastWasNewLine = true;
                        lastWasSpace = false;
                    }
                }

                continue;
            }

            if (c == '>') {
                insideTag = false;
                continue;
            }

            if (insideTag) {
                continue;
            }

            if (c == '\n' || c == '\t' || c == ' ') {
                if (!lastWasSpace && !lastWasNewLine) {
                    result += ' ';
                    lastWasSpace = true;
                }

                continue;
            }

            result += c;
            lastWasSpace = false;
            lastWasNewLine = false;
        }

        result.trim();

        while (result.indexOf("\n\n\n") >= 0) {
            result.replace("\n\n\n", "\n\n");
        }

        return result;
    }
}

bool EpubParserService::parseBookStructure(
    const String &epubPath,
    EpubBookStructure &structure
) {
    clearLastError();
    structure = EpubBookStructure();

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file", epubPath);
        return false;
    }

    ZipEntryInfo containerEntry;
    if (!findZipEntry(epubFile, "META-INF/container.xml", containerEntry)) {
        Serial.println("EPUB: META-INF/container.xml not found");
        setLastErrorIfEmpty("META-INF/container.xml was not found in the EPUB archive");
        epubFile.close();
        return false;
    }

    String containerXml;
    if (!extractZipEntryText(epubFile, containerEntry, 32 * 1024, containerXml)) {
        Serial.println("EPUB: failed to extract container.xml");
        setLastErrorIfEmpty("Cannot read META-INF/container.xml");
        epubFile.close();
        return false;
    }

    String packagePath;
    if (!findRootFilePath(containerXml, packagePath)) {
        Serial.println("EPUB: rootfile path not found");
        setLastError("container.xml does not contain a rootfile full-path");
        epubFile.close();
        return false;
    }

    ZipEntryInfo packageEntry;
    if (!findZipEntry(epubFile, packagePath.c_str(), packageEntry)) {
        Serial.print("EPUB: package file not found: ");
        Serial.println(packagePath);
        setLastErrorIfEmpty("OPF package file was not found", packagePath);
        epubFile.close();
        return false;
    }

    String packageXml;
    if (!extractZipEntryText(epubFile, packageEntry, 512 * 1024, packageXml)) {
        Serial.print("EPUB: failed to extract package file: ");
        Serial.println(packagePath);
        setLastErrorIfEmpty("Cannot read OPF package file", packagePath);
        epubFile.close();
        return false;
    }

    EpubMetadata metadata;
    parsePackageMetadata(packageXml, packagePath, metadata);

    structure.packagePath = packagePath;
    structure.title = metadata.title;
    structure.author = metadata.author;

    std::vector<ManifestItem> manifestItems;

    const String manifestXml = extractXmlBlock(packageXml, "manifest");
    if (manifestXml.isEmpty()) {
        setLastError("OPF package has no manifest section", packagePath);
        epubFile.close();
        return false;
    }

    int searchPos = 0;

    while (true) {
        const int itemPos = manifestXml.indexOf("<item", searchPos);
        if (itemPos < 0) {
            break;
        }

        const int tagEnd = manifestXml.indexOf('>', itemPos);
        if (tagEnd < 0) {
            break;
        }

        ManifestItem item;
        item.id = extractXmlAttributeValue(manifestXml, itemPos, "id");
        item.href = extractXmlAttributeValue(manifestXml, itemPos, "href");
        item.mediaType = extractXmlAttributeValue(manifestXml, itemPos, "media-type");

        if (!item.id.isEmpty() && !item.href.isEmpty()) {
            manifestItems.push_back(item);
        }

        searchPos = tagEnd + 1;
    }

    if (manifestItems.empty()) {
        setLastError("OPF manifest has no usable items", packagePath);
        epubFile.close();
        return false;
    }

    const String spineXml = extractXmlBlock(packageXml, "spine");
    if (spineXml.isEmpty()) {
        setLastError("OPF package has no spine section", packagePath);
        epubFile.close();
        return false;
    }

    searchPos = 0;
    int itemRefCount = 0;
    int textItemRefCount = 0;

    while (true) {
        const int itemRefPos = spineXml.indexOf("<itemref", searchPos);
        if (itemRefPos < 0) {
            break;
        }

        const int tagEnd = spineXml.indexOf('>', itemRefPos);
        if (tagEnd < 0) {
            break;
        }

        const String idRef = extractXmlAttributeValue(spineXml, itemRefPos, "idref");
        ManifestItem *manifestItem = findManifestItemById(manifestItems, idRef);
        itemRefCount++;

        if (manifestItem && looksLikeTextDocument(*manifestItem)) {
            textItemRefCount++;
            EpubSpineItem spineItem;
            spineItem.id = idRef;
            spineItem.href = manifestItem->href;
            spineItem.path = resolveRelativePath(packagePath, manifestItem->href);

            if (!spineItem.path.isEmpty()) {
                structure.spine.push_back(spineItem);
            }
        }

        searchPos = tagEnd + 1;
    }

    epubFile.close();

    Serial.print("EPUB: package path = ");
    Serial.println(structure.packagePath);

    Serial.print("EPUB: title = ");
    Serial.println(structure.title);

    Serial.print("EPUB: author = ");
    Serial.println(structure.author);

    Serial.print("EPUB: spine items = ");
    Serial.println(static_cast<int>(structure.spine.size()));

    if (structure.spine.empty()) {
        if (itemRefCount == 0) {
            setLastError("OPF spine has no itemref entries", packagePath);
        } else if (textItemRefCount == 0) {
            setLastError("OPF spine does not reference any HTML/XHTML documents", packagePath);
        } else {
            setLastError("OPF spine items resolved to empty paths", packagePath);
        }

        return false;
    }

    return true;
}

bool EpubParserService::readSpineItemText(
    const String &epubPath,
    const EpubSpineItem &item,
    String &text
) {
    text = "";

    String html;
    if (!readSpineItemHtml(epubPath, item, html)) {
        return false;
    }

    text = cleanupHtmlToText(html);

    Serial.print("EPUB: text length = ");
    Serial.println(text.length());

    if (text.isEmpty()) {
        setLastError("EPUB section contains no readable text", item.path);
        return false;
    }

    return true;
}

bool EpubParserService::readSpineItemHtml(
    const String &epubPath,
    const EpubSpineItem &item,
    String &html
) {
    clearLastError();
    html = "";

    if (item.path.isEmpty()) {
        Serial.println("EPUB: empty spine item path");
        setLastError("Spine item path is empty");
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file", epubPath);
        return false;
    }

    ZipEntryInfo spineEntry;
    if (!findZipEntry(epubFile, item.path.c_str(), spineEntry)) {
        Serial.print("EPUB: spine entry not found: ");
        Serial.println(item.path);
        setLastErrorIfEmpty("EPUB spine document was not found in archive", item.path);
        epubFile.close();
        return false;
    }

    if (!extractZipEntryText(epubFile, spineEntry, 512 * 1024, html)) {
        Serial.print("EPUB: failed to extract spine entry: ");
        Serial.println(item.path);
        setLastErrorIfEmpty("Cannot read EPUB spine document", item.path);
        epubFile.close();
        return false;
    }

    epubFile.close();

    Serial.print("EPUB: read spine item: ");
    Serial.println(item.path);

    Serial.print("EPUB: html length = ");
    Serial.println(html.length());

    if (html.isEmpty()) {
        setLastError("EPUB spine document is empty", item.path);
        return false;
    }

    return true;
}

bool EpubParserService::readSpineItemHtml(
    const String &epubPath,
    const EpubSpineItem &item,
    std::string &html
) {
    clearLastError();
    html.clear();

    if (item.path.isEmpty()) {
        Serial.println("EPUB: empty spine item path");
        setLastError("Spine item path is empty");
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file", epubPath);
        return false;
    }

    ZipEntryInfo spineEntry;
    if (!findZipEntry(epubFile, item.path.c_str(), spineEntry)) {
        Serial.print("EPUB: spine entry not found: ");
        Serial.println(item.path);
        setLastErrorIfEmpty("EPUB spine document was not found in archive", item.path);
        epubFile.close();
        return false;
    }

    if (!extractZipEntryText(epubFile, spineEntry, 512 * 1024, html)) {
        Serial.print("EPUB: failed to extract spine entry: ");
        Serial.println(item.path);
        setLastErrorIfEmpty("Cannot read EPUB spine document", item.path);
        epubFile.close();
        return false;
    }

    epubFile.close();

    Serial.print("EPUB: read spine item: ");
    Serial.println(item.path);

    Serial.print("EPUB: html length = ");
    Serial.println(static_cast<unsigned long>(html.length()));

    if (html.empty()) {
        setLastError("EPUB spine document is empty", item.path);
        return false;
    }

    return true;
}

bool EpubParserService::extractResourceData(
    const String &epubPath,
    const String &resourcePath,
    size_t maxSize,
    uint8_t *&outData,
    size_t &outSize
) {
    clearLastError();
    outData = nullptr;
    outSize = 0;

    if (resourcePath.isEmpty() || resourcePath.startsWith("data:")) {
        setLastError("Resource path is empty or inline data is unsupported");
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file for resource: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file for resource", epubPath);
        return false;
    }

    ZipEntryInfo entry;
    if (!findZipEntry(epubFile, resourcePath.c_str(), entry)) {
        Serial.print("EPUB: resource entry not found: ");
        Serial.println(resourcePath);
        setLastErrorIfEmpty("EPUB resource was not found in archive", resourcePath);
        epubFile.close();
        return false;
    }

    const bool ok = extractZipEntryData(epubFile, entry, maxSize, outData, outSize);
    epubFile.close();
    if (!ok) {
        setLastErrorIfEmpty("Cannot extract EPUB resource");
    }

    if (ok) {
        Serial.print("EPUB: resource extracted: ");
        Serial.print(resourcePath);
        Serial.print(" bytes=");
        Serial.println(static_cast<unsigned long>(outSize));
    }

    return ok;
}

bool EpubParserService::extractResourceToFile(
    const String &epubPath,
    const String &resourcePath,
    size_t maxSize,
    const String &outputPath
) {
    clearLastError();

    if (resourcePath.isEmpty() || resourcePath.startsWith("data:")) {
        setLastError("Resource path is empty or inline data is unsupported");
        return false;
    }

    if (outputPath.isEmpty()) {
        setLastError("Output path is empty");
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.print("EPUB: failed to open file for resource: ");
        Serial.println(epubPath);
        setLastError("Cannot open EPUB file for resource", epubPath);
        return false;
    }

    ZipEntryInfo entry;
    if (!findZipEntry(epubFile, resourcePath.c_str(), entry)) {
        Serial.print("EPUB: resource entry not found: ");
        Serial.println(resourcePath);
        setLastErrorIfEmpty("EPUB resource was not found in archive", resourcePath);
        epubFile.close();
        return false;
    }

    const bool ok = extractZipEntryToFile(epubFile, entry, maxSize, outputPath);
    epubFile.close();

    if (!ok) {
        setLastErrorIfEmpty("Cannot extract EPUB resource to file");
        return false;
    }

    Serial.print("EPUB: resource extracted to file: ");
    Serial.print(resourcePath);
    Serial.print(" -> ");
    Serial.println(outputPath);

    return true;
}

bool EpubParserService::extractCoverToFile(
    const String &epubPath,
    const EpubMetadata &metadata,
    const String &outputBasePath,
    String &outCoverPath
) {
    clearLastError();
    outCoverPath = "";

    if (!metadata.hasCover || metadata.coverInternalPath.isEmpty()) {
        setLastError("EPUB metadata does not contain a cover image");
        return false;
    }

    File epubFile = m_fs.open(epubPath, "r");
    if (!epubFile || epubFile.isDirectory()) {
        Serial.println("EPUB: failed to open file for cover extraction");
        setLastError("Cannot open EPUB file for cover extraction", epubPath);
        return false;
    }

    ZipEntryInfo coverEntry;
    if (!findZipEntry(epubFile, metadata.coverInternalPath.c_str(), coverEntry)) {
        Serial.print("EPUB: cover entry not found: ");
        Serial.println(metadata.coverInternalPath);
        setLastErrorIfEmpty("Cover image was not found in EPUB archive", metadata.coverInternalPath);
        epubFile.close();
        return false;
    }

    String ext = getLowerFileExtension(metadata.coverInternalPath);
    if (ext.isEmpty()) {
        ext = ".jpg";
    }

    const String outputPath = outputBasePath + ext;

    if (!extractZipEntryToFile(epubFile, coverEntry, 5 * 1024 * 1024, outputPath)) {
        Serial.println("EPUB: failed to extract cover data");
        setLastErrorIfEmpty("Cannot extract EPUB cover image");
        epubFile.close();
        return false;
    }

    epubFile.close();
    outCoverPath = outputPath;

    Serial.print("EPUB: cover saved to ");
    Serial.println(outCoverPath);

    return true;
}
