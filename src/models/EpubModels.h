#pragma once

#include <Arduino.h>
#include <vector>

struct EpubMetadata {
    String title;
    String author;
    String coverInternalPath;
    bool hasCover = false;
};

struct EpubSpineItem {
    String id;
    String href;
    String path;
};

struct EpubBookStructure {
    String packagePath;
    String title;
    String author;
    std::vector<EpubSpineItem> spine;
};