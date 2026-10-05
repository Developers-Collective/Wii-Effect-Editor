#include "resource_file.h"
#include "texture.h"
#include <algorithm>
#include <array>
#include <set>

namespace breff::codec {

    namespace {
        constexpr uint8_t Lz11 = 0x11;
        constexpr size_t WindowSize = 0x1000;
        constexpr size_t MaximumMatch = 0x10110;
        constexpr size_t MaximumArchiveSize = 256 * 1024 * 1024;

        Bytes decompressArchive(std::span<const uint8_t> bytes) {
            size_t position = 1;
            auto readByte = [&]() {
                if (position >= bytes.size())
                    throw std::runtime_error("Truncated LZ11 archive");
                return size_t(bytes[position++]);
            };
            auto readSize = [&](unsigned count) {
                size_t size = 0;
                for (unsigned i = 0; i < count; ++i)
                    size |= readByte() << (8 * i);
                return size;
            };
            size_t size = readSize(3);
            if (!size)
                size = readSize(4);
            if (!size || size > MaximumArchiveSize)
                throw std::runtime_error("Invalid LZ11 archive size");

            Bytes output;
            output.reserve(size);
            while (output.size() < size) {
                const auto flags = readByte();
                for (unsigned mask = 0x80; mask && output.size() < size; mask >>= 1) {
                    if (!(flags & mask)) {
                        output.push_back(uint8_t(readByte()));
                        continue;
                    }

                    const auto a = readByte(), b = readByte();
                    size_t length, distance;
                    if ((a >> 4) == 0) {
                        const auto c = readByte();
                        length = ((a & 0x0F) << 4 | b >> 4) + 0x11;
                        distance = ((b & 0x0F) << 8 | c) + 1;
                    } else if ((a >> 4) == 1) {
                        const auto c = readByte(), d = readByte();
                        length = ((a & 0x0F) << 12 | b << 4 | c >> 4) + 0x111;
                        distance = ((c & 0x0F) << 8 | d) + 1;
                    } else {
                        length = (a >> 4) + 1;
                        distance = ((a & 0x0F) << 8 | b) + 1;
                    }
                    if (distance > output.size() || length > size - output.size())
                        throw std::runtime_error("Invalid LZ11 back-reference");
                    for (size_t i = 0; i < length; ++i)
                        output.push_back(output[output.size() - distance]);
                }
            }
            return output;
        }

        Bytes compressArchive(std::span<const uint8_t> bytes) {
            if (bytes.empty() || bytes.size() > MaximumArchiveSize)
                throw std::runtime_error("Invalid LZ11 archive size");
            Bytes output{Lz11};
            const bool extended = bytes.size() > 0xFFFFFF;
            for (unsigned i = 0; i < 3; ++i)
                output.push_back(extended ? 0 : uint8_t(bytes.size() >> (8 * i)));
            if (extended)
                for (unsigned i = 0; i < 4; ++i)
                    output.push_back(uint8_t(bytes.size() >> (8 * i)));

            // Hash chains restrict the match search to the format's 4 KiB window.
            // Store links in a ring so large textures do not require a second archive-sized buffer.
            constexpr size_t Missing = std::numeric_limits<size_t>::max();
            std::array<size_t, 0x10000> heads;
            std::array<size_t, WindowSize> previous;
            heads.fill(Missing);
            previous.fill(Missing);
            auto hash = [&](size_t position) {
                return ((size_t(bytes[position]) * 251 + bytes[position + 1]) * 251 + bytes[position + 2]) & 0xFFFF;
            };
            auto remember = [&](size_t position) {
                if (bytes.size() - position >= 3) {
                    const auto key = hash(position);
                    previous[position % WindowSize] = heads[key];
                    heads[key] = position;
                }
            };

            size_t position = 0;
            while (position < bytes.size()) {
                const auto flagPosition = output.size();
                output.push_back(0);
                for (unsigned mask = 0x80; mask && position < bytes.size(); mask >>= 1) {
                    size_t length = 0, distance = 0;
                    const auto limit = std::min(MaximumMatch, bytes.size() - position);
                    if (limit >= 3) {
                        auto match = heads[hash(position)];
                        unsigned attempts = 0;
                        while (match != Missing && position - match <= WindowSize && attempts++ < 256) {
                            size_t count = 0;
                            while (count < limit && bytes[match + count] == bytes[position + count])
                                ++count;
                            if (count > length) {
                                length = count;
                                distance = position - match;
                                if (length == limit)
                                    break;
                            }
                            match = previous[match % WindowSize];
                        }
                    }

                    if (length < 3) {
                        output.push_back(bytes[position]);
                        remember(position++);
                        continue;
                    }
                    output[flagPosition] |= uint8_t(mask);
                    const auto offset = distance - 1;
                    if (length <= 0x10) {
                        output.push_back(uint8_t((length - 1) << 4 | offset >> 8));
                    } else if (length <= 0x110) {
                        const auto encoded = length - 0x11;
                        output.push_back(uint8_t(encoded >> 4));
                        output.push_back(uint8_t((encoded & 0x0F) << 4 | offset >> 8));
                    } else {
                        const auto encoded = length - 0x111;
                        output.push_back(uint8_t(0x10 | encoded >> 12));
                        output.push_back(uint8_t(encoded >> 4));
                        output.push_back(uint8_t((encoded & 0x0F) << 4 | offset >> 8));
                    }
                    output.push_back(uint8_t(offset));
                    const auto end = position + length;
                    while (position < end)
                        remember(position++);
                }
            }
            return output;
        }
    }

    ResourceFile ResourceFile::decode(std::span<const uint8_t> bytes) {
        const bool compressed = !bytes.empty() && bytes.front() == Lz11;
        Bytes decompressed;
        if (compressed) {
            decompressed = decompressArchive(bytes);
            bytes = decompressed;
        }
        Reader r(bytes);
        r.check(0x00, 40);
        ResourceFile result;
        result.compressed = compressed;
        result.magic = std::string(reinterpret_cast<const char*>(bytes.data()), 4);
        if ((result.magic != "REFF" && result.magic != "REFT") || r.at(0x04, 2) != 0xfeff ||
            r.at(0x08, 4) != bytes.size() || r.at(0x0C, 2) != 16 || r.at(0x0E, 2) != 1 ||
            r.at(0x10, 4) != r.at(0x00, 4))
            throw std::runtime_error("Invalid BREFF/BREFT archive header");

        result.version = uint16_t(r.at(0x06, 2));
        if (result.version < 5 || result.version > 11)
            throw std::runtime_error("Supported resource versions are 5 through 11");

        const auto projectSize = r.at(0x14, 4), headerSize = r.at(0x18, 4);
        if (projectSize != bytes.size() - 0x18 || headerSize < 16 || headerSize > projectSize)
            throw std::runtime_error("Invalid resource project size");

        const size_t table = 0x18 + headerSize;
        r.position = 0x24;
        const auto length = r.integer(2);
        r.skip(2);
        const auto name = r.slice(r.position, length);
        if (!length || name.back() != 0 || length > table - r.position)
            throw std::runtime_error("Invalid project name");
        result.projectName = std::string(reinterpret_cast<const char*>(name.data()), length - 1);

        const size_t tableSize = r.at(table, 4), count = r.at(table + 0x04, 2);
        if (tableSize < 8)
            throw std::runtime_error("Invalid resource table size");
        r.check(table, tableSize);
        r.position = table + 0x08;

        std::set<std::string> names;
        struct Record {
            size_t offset;
            size_t size;
        };
        std::vector<Record> records;
        for (size_t i = 0; i < count; ++i) {
            ResourceEntry entry;
            entry.name = r.name();
            const size_t offset = r.integer(4);
            size_t size = r.integer(4);
            if (r.position > table + tableSize || offset < tableSize || offset > bytes.size() - table)
                throw std::runtime_error("Resource entry points outside archive");
            if (!names.insert(entry.name).second)
                throw std::runtime_error("Duplicate resource name: " + entry.name);
            records.push_back({table + offset, size});
            result.entries.push_back(std::move(entry));
        }

        // Physical record boundaries distinguish the two texture header layouts.
        // The version alone cannot do this: v11 archives exist with both layouts.
        std::vector<size_t> boundaries{bytes.size()};
        for (const auto& record : records)
            boundaries.push_back(record.offset);
        std::sort(boundaries.begin(), boundaries.end());
        if (std::adjacent_find(boundaries.begin(), boundaries.end()) != boundaries.end())
            throw std::runtime_error("Overlapping resource entries");

        for (size_t i = 0; i < records.size(); ++i) {
            const auto [offset, size] = records[i];
            const auto end = *std::upper_bound(boundaries.begin(), boundaries.end(), offset);
            auto& entry = result.entries[i];
            if (result.magic == "REFT") {
                r.check(offset, 0x20);
                const size_t payloadSize = r.at(offset + 0x08, 4) + r.at(offset + 0x10, 4);
                const size_t extent = end - offset;
                if (extent < 0x20 || payloadSize > extent - 0x20)
                    throw std::runtime_error("Truncated texture record: " + entry.name);

                // Alignment after a compact record occupies fewer than 0x20 bytes.
                // An additional full block belongs to the extended texture header.
                // Its 0x20 palette pointer is populated by the game at load time.
                const size_t headerSize = extent - payloadSize >= 0x40 ? 0x40 : 0x20;
                if (extent - payloadSize - headerSize >= 0x20)
                    throw std::runtime_error("Invalid texture record size: " + entry.name);
                result.textureHeaderSize = std::max(result.textureHeaderSize, headerSize);

                const auto header = r.slice(offset, 0x20);
                const auto payload = r.slice(offset + headerSize, payloadSize);
                entry.data.assign(header.begin(), header.end());
                entry.data.insert(entry.data.end(), payload.begin(), payload.end());
                // Older runtimes overwrite 0x18 with the image pointer and use
                // fixed sampling settings. Keep their effective values in memory.
                if (headerSize == 0x20)
                    setTextureSampling(entry.data, defaultTextureSampling(entry.data));
            } else {
                if (size > end - offset)
                    throw std::runtime_error("Overlapping resource entries");
                const auto data = r.slice(offset, size);
                entry.data.assign(data.begin(), data.end());
            }
        }

        return result;
    }

    Bytes ResourceFile::encode(bool applyCompression) const {
        if ((magic != "REFF" && magic != "REFT") || version < 5 || version > 11)
            throw std::runtime_error("Invalid destination resource format");
        if (magic == "REFT" && textureHeaderSize != 0x20 && textureHeaderSize != 0x40)
            throw std::runtime_error("Invalid texture header size");

        Writer w;
        w.append({reinterpret_cast<const uint8_t*>(magic.data()), 4});
        w.integer(0xfeff, 2);
        w.integer(version, 2);
        w.zeros(4);
        w.integer(16, 2);
        w.integer(1, 2);
        w.append({reinterpret_cast<const uint8_t*>(magic.data()), 4});
        w.zeros(16);

        // The project name's length and string are separated by two reserved bytes.
        if (projectName.size() >= 65535 || projectName.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid project name");
        w.integer(projectName.size() + 1, 2);
        w.zeros(2);
        w.append({reinterpret_cast<const uint8_t*>(projectName.data()), projectName.size()});
        w.zeros(1);

        w.align(4);

        const size_t table = w.bytes.size();
        w.patch(0x18, table - 0x18, 4);
        w.zeros(4);
        w.integer(entries.size(), 2);
        w.zeros(2);
        std::vector<size_t> patches;

        std::set<std::string> names;
        for (const auto& entry : entries) {
            if (!names.insert(entry.name).second)
                throw std::runtime_error("Duplicate resource name: " + entry.name);
            if (magic == "REFT" && entry.data.size() < 32)
                throw std::runtime_error("Truncated texture record");
            w.name(entry.name);
            patches.push_back(w.bytes.size());
            w.zeros(4);
            w.integer(entry.data.size() - (magic == "REFT" ? 32 : 0), 4);
        }

        w.align(4);
        w.patch(table, w.bytes.size() - table, 4);
        for (size_t i = 0; i < entries.size(); ++i) {
            // GX texture data must stay aligned in the archive, including after renames.
            if (magic == "REFT")
                w.align(32);
            w.patch(patches[i], w.bytes.size() - table, 4);
            if (magic == "REFT") {
                const std::span<const uint8_t> data = entries[i].data;
                // Runtime pointers are relocated by the game, never serialized.
                const size_t fieldsSize = textureHeaderSize == 0x40 ? 0x1C : 0x18;
                w.append(data.first(fieldsSize));
                w.zeros(textureHeaderSize - fieldsSize);
                w.append(data.subspan(0x20));
            } else {
                w.append(entries[i].data);
            }
        }

        w.patch(0x08, w.bytes.size(), 4);
        w.patch(0x14, w.bytes.size() - 0x18, 4);
        if (compressed && applyCompression)
            return compressArchive(w.bytes);
        return std::move(w.bytes);
    }
}
