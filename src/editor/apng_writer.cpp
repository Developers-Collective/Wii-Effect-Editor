#include "apng_writer.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <zlib.h>

namespace breff {
    namespace {
        void append(std::vector<uint8_t>& bytes, uint32_t value) {
            bytes.push_back(uint8_t(value >> 24));
            bytes.push_back(uint8_t(value >> 16));
            bytes.push_back(uint8_t(value >> 8));
            bytes.push_back(uint8_t(value));
        }
    }

    void ApngWriter::chunk(const char* name, std::span<const uint8_t> bytes) {
        std::vector<uint8_t> header;
        append(header, uint32_t(bytes.size()));
        stream.write(reinterpret_cast<const char*>(header.data()), header.size());
        stream.write(name, 4);
        if (!bytes.empty())
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto crc = crc32(0, reinterpret_cast<const Bytef*>(name), 4);
        if (!bytes.empty())
            crc = crc32(crc, bytes.data(), unsigned(bytes.size()));
        header.clear();
        append(header, uint32_t(crc));
        stream.write(reinterpret_cast<const char*>(header.data()), header.size());
    }

    ApngWriter::ApngWriter(const std::filesystem::path& path, unsigned dimension, unsigned frames)
        : stream(path, std::ios::binary), size(dimension), frameCount(frames) {
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        const std::array<uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        stream.write(reinterpret_cast<const char*>(signature.data()), signature.size());
        std::vector<uint8_t> header;
        append(header, size);
        append(header, size);
        header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, lossless PNG compression.
        chunk("IHDR", header);
        header.clear();
        append(header, frameCount);
        append(header, 0); // Repeat indefinitely.
        chunk("acTL", header);
        filtered.resize((size_t(size) * 4 + 1) * size);
        compressed.resize(compressBound(uLong(filtered.size())) + 4);
    }

    void ApngWriter::frame(std::span<const uint8_t> rgba) {
        if (rgba.size() != size_t(size) * size * 4 || written == frameCount)
            throw std::runtime_error("Invalid APNG frame");
        std::vector<uint8_t> control;
        append(control, sequence++);
        append(control, size);
        append(control, size);
        append(control, 0);
        append(control, 0);
        control.insert(control.end(), {0, 1, 0, 60, 0, 0}); // 1/60 second, replace the entire frame.
        chunk("fcTL", control);
        const size_t stride = size_t(size) * 4;
        for (unsigned y = 0; y < size; ++y) {
            auto* destination = filtered.data() + y * (stride + 1);
            const auto* source = rgba.data() + y * stride;
            *destination++ = 1; // PNG Sub filter.
            for (size_t x = 0; x < stride; ++x)
                destination[x] = uint8_t(source[x] - (x >= 4 ? source[x - 4] : 0));
        }
        uLongf length = uLongf(compressed.size() - 4);
        if (compress2(compressed.data() + 4, &length, filtered.data(), uLong(filtered.size()), Z_BEST_SPEED) != Z_OK)
            throw std::runtime_error("Could not compress APNG frame");
        if (written == 0)
            chunk("IDAT", std::span(compressed).subspan(4, length));
        else {
            control.clear();
            append(control, sequence++);
            std::copy(control.begin(), control.end(), compressed.begin());
            chunk("fdAT", std::span(compressed).first(length + 4));
        }
        ++written;
    }

    void ApngWriter::finish() {
        if (written != frameCount)
            throw std::runtime_error("Incomplete APNG animation");
        chunk("IEND", {});
        stream.close();
    }
}
