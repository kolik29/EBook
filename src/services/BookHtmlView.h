#pragma once

#include <Arduino.h>
#include <string>

class BookHtmlView {
public:
    explicit BookHtmlView(const String &text)
        : m_string(&text), m_stdString(nullptr) {
    }

    explicit BookHtmlView(const std::string &text)
        : m_string(nullptr), m_stdString(&text) {
    }

    int length() const {
        return m_string
            ? m_string->length()
            : static_cast<int>(m_stdString->size());
    }

    char operator[](int index) const {
        return m_string
            ? (*m_string)[index]
            : (*m_stdString)[static_cast<size_t>(index)];
    }

    int indexOf(char c, int fromIndex = 0) const {
        if (fromIndex < 0) {
            fromIndex = 0;
        }

        if (fromIndex >= length()) {
            return -1;
        }

        if (m_string) {
            return m_string->indexOf(c, fromIndex);
        }

        const size_t pos = m_stdString->find(c, static_cast<size_t>(fromIndex));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    int indexOf(const String &pattern, int fromIndex = 0) const {
        if (pattern.isEmpty()) {
            return fromIndex <= length() ? fromIndex : -1;
        }

        if (fromIndex < 0) {
            fromIndex = 0;
        }

        if (fromIndex >= length()) {
            return -1;
        }

        if (m_string) {
            return m_string->indexOf(pattern, fromIndex);
        }

        const size_t pos = m_stdString->find(
            pattern.c_str(),
            static_cast<size_t>(fromIndex),
            static_cast<size_t>(pattern.length())
        );
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    int lastIndexOf(char c, int fromIndex) const {
        const int len = length();

        if (len <= 0) {
            return -1;
        }

        if (fromIndex >= len) {
            fromIndex = len - 1;
        }

        if (fromIndex < 0) {
            return -1;
        }

        if (m_string) {
            return m_string->lastIndexOf(c, fromIndex);
        }

        const size_t pos = m_stdString->rfind(c, static_cast<size_t>(fromIndex));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    String substring(int start, int end) const {
        const int len = length();

        if (start < 0) {
            start = 0;
        }

        if (end < start) {
            end = start;
        }

        if (end > len) {
            end = len;
        }

        if (m_string) {
            return m_string->substring(start, end);
        }

        return String(
            m_stdString->data() + static_cast<size_t>(start),
            static_cast<unsigned int>(end - start)
        );
    }

    String substring(int start) const {
        return substring(start, length());
    }

private:
    const String *m_string;
    const std::string *m_stdString;
};
