#pragma once

#include <Arduino.h>

struct EpubMetadata {
    String title;
    String author;
    String coverInternalPath;
    bool hasCover = false;
};