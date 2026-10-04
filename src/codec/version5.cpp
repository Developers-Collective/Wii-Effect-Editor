#include "version5.h"
#include <array>
#include <bit>

namespace breff::codec {
    // v5 puts the particle drawing options before user data; v6/v7 put them last.
    Bytes expandV5Emitter(std::span<const uint8_t> bytes) {
        Reader r(bytes);
        if (bytes.size() != 0x148 || r.at(0x04, 4) != 0x140)
            throw std::runtime_error("Unexpected v5 emitter size");
        Writer w;
        w.append(r.slice(0x00, 0x94));
        w.append(r.slice(0xA0, 0x08));
        w.append(r.slice(0xA8, 0xA0));
        w.append(r.slice(0x94, 0x0C));
        return std::move(w.bytes);
    }

    Bytes packV5Emitter(std::span<const uint8_t> bytes) {
        Reader r(bytes);
        if (bytes.size() != 0x148 || r.at(0x04, 4) != 0x140)
            throw std::runtime_error("Unexpected legacy emitter size");
        Writer w;
        w.append(r.slice(0x00, 0x94));
        w.append(r.slice(0x13C, 0x0C));
        w.append(r.slice(0x94, 0x08));
        w.append(r.slice(0x9C, 0xA0));
        return std::move(w.bytes);
    }

    namespace {
        Bytes convertParticle(std::span<const uint8_t> bytes, bool expand) {
            Reader r(bytes);
            if (r.at(0x00, 4) != bytes.size() - 4)
                throw std::runtime_error("Invalid legacy particle size");
            Writer w;
            w.zeros(4);
            w.append(r.slice(0x04, 0x79));
            // v5/v6 have one padding byte instead of the three random offsets
            // and three floating-point offsets introduced in v7.
            if (expand)
                w.zeros(15);
            else
                w.zeros(1);
            r.position = 0x04 + (expand ? 0x7A : 0x88);
            for (unsigned i = 0; i < 3; ++i)
                w.name(r.name());
            r.align(4);
            if (r.position != bytes.size())
                throw std::runtime_error("Unexpected legacy particle data");
            w.align(4);
            w.patch(0x00, w.bytes.size() - 4, 4);
            return std::move(w.bytes);
        }

        Bytes convertAnimation(std::span<const uint8_t> bytes, bool expand) {
            Reader r(bytes);
            r.check(0x00, 0x20);
            if (r.at(0x00, 1) == 0xAB)
                return Bytes(bytes.begin(), bytes.end());
            if (r.at(0x00, 1) != 0xAC)
                throw std::runtime_error("Invalid legacy animation magic");

            std::array<Bytes, 5> tables;
            size_t offset = 0x20;
            for (unsigned i = 0; i < tables.size(); ++i) {
                const auto part = r.slice(offset, r.at(0x0C + 4 * i, 4));
                tables[i].assign(part.begin(), part.end());
                offset += part.size();
            }
            if (offset != bytes.size())
                throw std::runtime_error("Invalid legacy animation table sizes");

            if (tables[0].empty())
                return Bytes(bytes.begin(), bytes.end());

            const unsigned family = r.at(0x02, 1), components = std::popcount(unsigned(r.at(0x03, 1)));
            const size_t payload = family == 5   ? 10
                                   : family == 4 ? 4
                                   : family == 0 ? (components + 1) / 2 * 2
                                                 : components * 4;
            Reader keys(tables[0]);
            Writer converted;
            const size_t count = keys.at(0x00, 2), header = expand ? 0x08 : 0x0C;
            converted.append(keys.slice(0x00, 4));
            for (size_t i = 0; i < count; ++i) {
                const size_t start = 0x04 + i * (header + payload);
                converted.append(keys.slice(start, 2));
                if (expand) {
                    converted.integer(keys.at(start + 0x06, 1), 1);
                    converted.zeros(1);
                    const auto curves = keys.at(start + 0x02, 2);
                    for (unsigned component = 0; component < 8; ++component)
                        converted.integer((curves >> (component * 2)) & 0x03, 1);
                } else {
                    unsigned curves = 0;
                    for (unsigned component = 0; component < 8; ++component)
                        curves |= (unsigned(keys.at(start + 0x04 + component, 1)) & 0x03) << (component * 2);
                    converted.integer(curves, 2);
                    converted.zeros(2);
                    converted.integer(keys.at(start + 0x02, 1), 1);
                    converted.zeros(1);
                }
                converted.append(keys.slice(start + header, payload));
            }
            keys.position = 0x04 + count * (header + payload);
            keys.align(4);
            if (keys.position != keys.size())
                throw std::runtime_error("Invalid legacy keyframe stride");
            converted.align(4);
            tables[0] = std::move(converted.bytes);

            Writer w;
            w.append(r.slice(0x00, 0x0C));
            for (const auto& table : tables)
                w.integer(table.size(), 4);
            for (const auto& table : tables)
                w.append(table);
            return std::move(w.bytes);
        }
    }

    Bytes expandLegacyParticle(std::span<const uint8_t> bytes) {
        return convertParticle(bytes, true);
    }

    Bytes packLegacyParticle(std::span<const uint8_t> bytes) {
        return convertParticle(bytes, false);
    }

    Bytes expandV5Animation(std::span<const uint8_t> bytes) {
        return convertAnimation(bytes, true);
    }

    Bytes packV5Animation(std::span<const uint8_t> bytes) {
        return convertAnimation(bytes, false);
    }
}
