#include "BookTextCodec.h"

namespace {
    bool isContinuation(uint8_t value) {
        return (value & 0xC0) == 0x80;
    }

    int hexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }

        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }

        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }

        return -1;
    }

    bool parseUnsigned(const String &value, int start, bool hex, uint32_t &outCodepoint) {
        if (start >= value.length()) {
            return false;
        }

        uint32_t result = 0;

        for (int i = start; i < value.length(); i++) {
            const char c = value[i];
            const int digit = hex ? hexValue(c) : (c >= '0' && c <= '9' ? c - '0' : -1);

            if (digit < 0) {
                return false;
            }

            result = result * (hex ? 16 : 10) + static_cast<uint32_t>(digit);

            if (result > 0x10FFFFUL) {
                return false;
            }
        }

        outCodepoint = result;
        return true;
    }

    bool appendNamedEntity(const String &entity, String &out) {
        String name = entity;
        name.toLowerCase();

        if (name == "nbsp") {
            out += ' ';
        } else if (name == "amp") {
            out += '&';
        } else if (name == "quot") {
            out += '"';
        } else if (name == "apos") {
            out += '\'';
        } else if (name == "lt") {
            out += '<';
        } else if (name == "gt") {
            out += '>';
        } else if (name == "laquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x00AB);
        } else if (name == "raquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x00BB);
        } else if (name == "mdash") {
            BookTextCodec::appendUtf8Codepoint(out, 0x2014);
        } else if (name == "ndash") {
            BookTextCodec::appendUtf8Codepoint(out, 0x2013);
        } else if (name == "hellip") {
            BookTextCodec::appendUtf8Codepoint(out, 0x2026);
        } else if (name == "lsquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x2018);
        } else if (name == "rsquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x2019);
        } else if (name == "ldquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x201C);
        } else if (name == "rdquo") {
            BookTextCodec::appendUtf8Codepoint(out, 0x201D);
        } else {
            return false;
        }

        return true;
    }
}

namespace BookTextCodec {
    bool readUtf8Codepoint(const String &text, int &index, uint32_t &codepoint) {
        codepoint = 0;

        if (index < 0) {
            index = 0;
        }

        if (index >= text.length()) {
            return false;
        }

        const uint8_t c0 = static_cast<uint8_t>(text[index]);

        if (c0 < 0x80) {
            codepoint = c0;
            index++;
            return true;
        }

        if (c0 >= 0xC2 && c0 <= 0xDF && index + 1 < text.length()) {
            const uint8_t c1 = static_cast<uint8_t>(text[index + 1]);

            if (isContinuation(c1)) {
                codepoint = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
                index += 2;
                return true;
            }
        }

        if (c0 >= 0xE0 && c0 <= 0xEF && index + 2 < text.length()) {
            const uint8_t c1 = static_cast<uint8_t>(text[index + 1]);
            const uint8_t c2 = static_cast<uint8_t>(text[index + 2]);
            const bool validPrefix =
                (c0 == 0xE0 && c1 >= 0xA0 && c1 <= 0xBF)
                || (c0 >= 0xE1 && c0 <= 0xEC && isContinuation(c1))
                || (c0 == 0xED && c1 >= 0x80 && c1 <= 0x9F)
                || (c0 >= 0xEE && c0 <= 0xEF && isContinuation(c1));

            if (validPrefix && isContinuation(c2)) {
                codepoint =
                    ((c0 & 0x0F) << 12)
                    | ((c1 & 0x3F) << 6)
                    | (c2 & 0x3F);
                index += 3;
                return true;
            }
        }

        if (c0 >= 0xF0 && c0 <= 0xF4 && index + 3 < text.length()) {
            const uint8_t c1 = static_cast<uint8_t>(text[index + 1]);
            const uint8_t c2 = static_cast<uint8_t>(text[index + 2]);
            const uint8_t c3 = static_cast<uint8_t>(text[index + 3]);
            const bool validPrefix =
                (c0 == 0xF0 && c1 >= 0x90 && c1 <= 0xBF)
                || (c0 >= 0xF1 && c0 <= 0xF3 && isContinuation(c1))
                || (c0 == 0xF4 && c1 >= 0x80 && c1 <= 0x8F);

            if (validPrefix && isContinuation(c2) && isContinuation(c3)) {
                codepoint =
                    ((c0 & 0x07) << 18)
                    | ((c1 & 0x3F) << 12)
                    | ((c2 & 0x3F) << 6)
                    | (c3 & 0x3F);
                index += 4;
                return true;
            }
        }

        codepoint = c0;
        index++;
        return false;
    }

    uint8_t glyphCodeForCodepoint(uint32_t codepoint, bool validUtf8) {
        if (!validUtf8 && codepoint >= 0x80 && codepoint <= 0xFF) {
            return static_cast<uint8_t>(codepoint);
        }

        if (codepoint == '\t' || codepoint == 0x00A0) {
            return ' ';
        }

        if (codepoint >= 0x20 && codepoint <= 0x7E) {
            return static_cast<uint8_t>(codepoint);
        }

        if (codepoint >= 0x0410 && codepoint <= 0x044F) {
            return static_cast<uint8_t>(0xC0 + (codepoint - 0x0410));
        }

        switch (codepoint) {
            case 0x0402: return 0x80;
            case 0x0403: return 0x81;
            case 0x201A: return 0x82;
            case 0x0453: return 0x83;
            case 0x201E: return 0x84;
            case 0x2026: return 0x85;
            case 0x2020: return 0x86;
            case 0x2021: return 0x87;
            case 0x20AC: return 0x88;
            case 0x2030: return 0x89;
            case 0x0409: return 0x8A;
            case 0x2039: return 0x8B;
            case 0x040A: return 0x8C;
            case 0x040C: return 0x8D;
            case 0x040B: return 0x8E;
            case 0x040F: return 0x8F;
            case 0x0452: return 0x90;
            case 0x2018: return 0x91;
            case 0x2019: return 0x92;
            case 0x201C: return 0x93;
            case 0x201D: return 0x94;
            case 0x2022: return 0x95;
            case 0x2013: return 0x96;
            case 0x2014: return 0x97;
            case 0x2122: return 0x99;
            case 0x0459: return 0x9A;
            case 0x203A: return 0x9B;
            case 0x045A: return 0x9C;
            case 0x045C: return 0x9D;
            case 0x045B: return 0x9E;
            case 0x045F: return 0x9F;
            case 0x040E: return 0xA1;
            case 0x045E: return 0xA2;
            case 0x0408: return 0xA3;
            case 0x00A4: return 0xA4;
            case 0x0490: return 0xA5;
            case 0x00A6: return 0xA6;
            case 0x00A7: return 0xA7;
            case 0x0401: return 0xA8;
            case 0x00A9: return 0xA9;
            case 0x0404: return 0xAA;
            case 0x00AB: return 0xAB;
            case 0x00AC: return 0xAC;
            case 0x00AD: return '-';
            case 0x00AE: return 0xAE;
            case 0x0407: return 0xAF;
            case 0x00B0: return 0xB0;
            case 0x00B1: return 0xB1;
            case 0x0406: return 0xB2;
            case 0x0456: return 0xB3;
            case 0x0491: return 0xB4;
            case 0x00B5: return 0xB5;
            case 0x00B6: return 0xB6;
            case 0x00B7: return 0xB7;
            case 0x0451: return 0xB8;
            case 0x2116: return 0xB9;
            case 0x0454: return 0xBA;
            case 0x00BB: return 0xBB;
            case 0x0458: return 0xBC;
            case 0x0405: return 0xBD;
            case 0x0455: return 0xBE;
            case 0x0457: return 0xBF;
            case 0x2212: return '-';
            default: return '?';
        }
    }

    String encodeUtf8ToCp1251(const String &text) {
        String encoded = "";
        encoded.reserve(text.length());

        int index = 0;

        while (index < text.length()) {
            uint32_t codepoint = 0;
            const bool validUtf8 = readUtf8Codepoint(text, index, codepoint);

            if (codepoint == '\n' || codepoint == '\r') {
                encoded += static_cast<char>(codepoint);
            } else {
                encoded += static_cast<char>(glyphCodeForCodepoint(codepoint, validUtf8));
            }
        }

        return encoded;
    }

    String utf8PrefixByCodepoints(const String &text, int maxCodepoints) {
        if (maxCodepoints <= 0 || text.isEmpty()) {
            return "";
        }

        int index = 0;
        int count = 0;

        while (index < text.length() && count < maxCodepoints) {
            uint32_t codepoint = 0;
            readUtf8Codepoint(text, index, codepoint);
            count++;
        }

        return text.substring(0, index);
    }

    void appendUtf8Codepoint(String &text, uint32_t codepoint) {
        if (codepoint <= 0x7F) {
            text += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
            text += static_cast<char>(0xC0 | (codepoint >> 6));
            text += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            text += static_cast<char>(0xE0 | (codepoint >> 12));
            text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFFUL) {
            text += static_cast<char>(0xF0 | (codepoint >> 18));
            text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    String decodeHtmlEntities(const String &value) {
        String decoded = "";
        decoded.reserve(value.length());

        int index = 0;

        while (index < value.length()) {
            if (value[index] != '&') {
                decoded += value[index];
                index++;
                continue;
            }

            const int semicolon = value.indexOf(';', index + 1);

            if (semicolon < 0 || semicolon - index > 16) {
                decoded += value[index];
                index++;
                continue;
            }

            const String entity = value.substring(index + 1, semicolon);
            bool handled = false;

            if (entity.startsWith("#")) {
                uint32_t codepoint = 0;
                bool ok = false;

                if (entity.length() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                    ok = parseUnsigned(entity, 2, true, codepoint);
                } else {
                    ok = parseUnsigned(entity, 1, false, codepoint);
                }

                if (ok) {
                    appendUtf8Codepoint(decoded, codepoint);
                    handled = true;
                }
            } else {
                handled = appendNamedEntity(entity, decoded);
            }

            if (handled) {
                index = semicolon + 1;
            } else {
                decoded += value[index];
                index++;
            }
        }

        return decoded;
    }
}
