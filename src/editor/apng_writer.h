#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace breff {
    class ApngWriter {
        std::ofstream stream;
        unsigned size, frameCount, written = 0, sequence = 0;
        std::vector<uint8_t> filtered, compressed;
        void chunk(const char* name, std::span<const uint8_t> bytes);

      public:
        ApngWriter(const std::filesystem::path& path, unsigned size, unsigned frames);
        void frame(std::span<const uint8_t> rgba);
        void finish();
    };
}
