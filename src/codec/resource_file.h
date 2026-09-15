#pragma once
#include "binary.h"
#include <string>
#include <vector>

namespace breff::codec {
struct ResourceEntry { std::string name; Bytes data; };
struct ResourceFile {
    std::string magic="REFF", projectName;
    uint16_t version=11;
    std::vector<ResourceEntry> entries;
    static ResourceFile decode(std::span<const uint8_t> bytes);
    Bytes encode() const;
};
}
