#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace breff::codec {
using Bytes = std::vector<uint8_t>;
class Reader {
    std::span<const uint8_t> bytes_;

  public:
    size_t position = 0;
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {
    }
    size_t size() const {
        return bytes_.size();
    }
    void check(size_t offset, size_t count) const {
        if (offset > size() || count > size() - offset)
            throw std::runtime_error("Truncated resource at byte " + std::to_string(offset));
    }
    std::span<const uint8_t> slice(size_t offset, size_t count) const {
        check(offset, count);
        return bytes_.subspan(offset, count);
    }
    uint64_t at(size_t offset, unsigned width) const {
        if (width > 8)
            throw std::logic_error("Invalid integer width");
        check(offset, width);
        uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value = (value << 8) | bytes_[offset + i];
        return value;
    }
    uint64_t integer(unsigned width) {
        auto v = at(position, width);
        position += width;
        return v;
    }
    float real() {
        return std::bit_cast<float>(uint32_t(integer(4)));
    }
    void skip(size_t count) {
        check(position, count);
        position += count;
    }
    void align(size_t alignment) {
        skip((alignment - position % alignment) % alignment);
    }
    std::string name() {
        const auto count = integer(2);
        auto bytes = slice(position, count);
        if (!count || bytes.back() != 0)
            throw std::runtime_error("Unterminated resource name");
        std::string result(reinterpret_cast<const char*>(bytes.data()), count - 1);
        if (result.find('\0') != std::string::npos)
            throw std::runtime_error("Embedded null in resource name");
        position += count;
        return result;
    }
};
class Writer {
  public:
    Bytes bytes;
    void integer(uint64_t value, unsigned width) {
        if (!width || width > 8)
            throw std::logic_error("Invalid integer width");
        if (width < 8 && value >= (uint64_t(1) << (width * 8)))
            throw std::runtime_error("Integer exceeds binary field width");
        for (unsigned i = width; i; --i)
            bytes.push_back(uint8_t(value >> ((i - 1) * 8)));
    }
    void real(float value) {
        integer(std::bit_cast<uint32_t>(value), 4);
    }
    void zeros(size_t count) {
        bytes.resize(bytes.size() + count, 0);
    }
    void align(size_t alignment) {
        zeros((alignment - bytes.size() % alignment) % alignment);
    }
    void append(std::span<const uint8_t> value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void patch(size_t offset, uint64_t value, unsigned width) {
        if (offset > bytes.size() || width > bytes.size() - offset)
            throw std::logic_error("Patch outside output");
        Writer field;
        field.integer(value, width);
        for (unsigned i = 0; i < width; ++i)
            bytes[offset + i] = field.bytes[i];
    }
    void name(const std::string& value) {
        if (value.size() >= 65535 || value.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid resource name");
        integer(value.size() + 1, 2);
        append({reinterpret_cast<const uint8_t*>(value.data()), value.size()});
        zeros(1);
    }
};
}
