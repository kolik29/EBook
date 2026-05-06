#pragma once

#include <Arduino.h>

namespace BookTextCodec {
    bool readUtf8Codepoint(const String &text, int &index, uint32_t &codepoint);
    uint8_t glyphCodeForCodepoint(uint32_t codepoint, bool validUtf8 = true);
    String encodeUtf8ToCp1251(const String &text);
    String utf8PrefixByCodepoints(const String &text, int maxCodepoints);
    void appendUtf8Codepoint(String &text, uint32_t codepoint);
    String decodeHtmlEntities(const String &value);
}
