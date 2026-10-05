#pragma once
#include "binary.h"
#include <string>
#include <vector>

namespace breff::codec {
    struct ResourceEntry {
        std::string name;
        Bytes data;
    };

    struct ResourceFile {
        std::string magic = "REFF", projectName;
        uint16_t version = 11;
        bool compressed = false;
        // Texture entries retain the first 0x20 header bytes in memory. The newer
        // disk structure adds LOD bias at 0x18, moves the runtime pointers to
        // 0x1C/0x20 and starts image data at 0x40 instead of 0x20.
        size_t textureHeaderSize = 0x20;
        std::vector<ResourceEntry> entries;
        static ResourceFile decode(std::span<const uint8_t> bytes);
        Bytes encode(bool applyCompression = true) const;
    };
}
