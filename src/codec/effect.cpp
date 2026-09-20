#include "effect.h"
#include "animation.h"
#include "version7.h"
#include <algorithm>
#include <tuple>

namespace breff::codec {
Json decodeEffect(std::span<const uint8_t> bytes, unsigned version, bool includeUnboundTextures) {
    if (version < 7 || version > 11)
        throw std::runtime_error("Unsupported BREFF version");
    Reader r(bytes);
    const size_t particleOffset = 8 + r.at(4, 4);
    const auto emitterBytes = r.slice(0, particleOffset);
    Json result = {
        {"emitter", version == 7 ? decodeEmitter(expandV7Emitter(emitterBytes)) : decodeEmitter(emitterBytes)}};
    const size_t particleSize = 4 + r.at(particleOffset, 4);
    result["particle"] = decodeParticle(r.slice(particleOffset, particleSize), includeUnboundTextures);
    result["animations"] = Json::array();
    size_t offset = particleOffset + particleSize;
    std::vector<std::pair<size_t, bool>> tracks;
    for (unsigned group = 0; group < 2; ++group) {
        const size_t count = r.at(offset, 2), init = r.at(offset + 2, 2);
        if (init > count)
            throw std::runtime_error("Initialization track count exceeds total count");
        r.check(offset + 4, 8 * count);
        for (size_t i = 0; i < count; ++i)
            tracks.emplace_back(r.at(offset + 4 + 4 * count + 4 * i, 4), i < init);
        offset += 4 + 8 * count;
    }
    for (const auto& [size, init] : tracks) {
        const auto track = r.slice(offset, size);
        result["animations"].push_back(version == 7 ? decodeAnimation(expandV7Animation(track), result["emitter"], init)
                                                    : decodeAnimation(track, result["emitter"], init));
        offset += size;
    }
    if (offset != bytes.size())
        throw std::runtime_error("Unexpected trailing effect bytes");
    return result;
}
Bytes encodeEffect(const Json& value, unsigned version) {
    if (version < 7 || version > 11)
        throw std::runtime_error("Unsupported BREFF version");
    const auto emitter = encodeEmitter(value.at("emitter"));
    Writer w;
    if (version == 7)
        w.append(packV7Emitter(emitter));
    else
        w.append(emitter);
    w.append(encodeParticle(value.at("particle")));
    struct Track {
        Bytes bytes;
        bool init;
    };
    std::vector<Track> groups[2];
    for (const auto& animation : value.at("animations")) {
        const std::string target = animation.at("target");
        auto track = encodeAnimation(animation, value.at("emitter"));
        if (version == 7)
            track = packV7Animation(track);
        groups[target.starts_with("Emitter") ? 1 : 0].push_back({std::move(track), animation.value("isInit", false)});
    }
    for (auto& group : groups) {
        std::stable_sort(group.begin(), group.end(), [](const Track& a, const Track& b) {
            return a.init > b.init;
        });
        w.integer(group.size(), 2);
        w.integer(std::count_if(group.begin(), group.end(),
                                [](const Track& t) {
                                    return t.init;
                                }),
                  2);
        w.zeros(4 * group.size());
        for (const auto& t : group)
            w.integer(t.bytes.size(), 4);
    }
    for (const auto& group : groups)
        for (const auto& t : group)
            w.append(t.bytes);
    return std::move(w.bytes);
}
Bytes encodeEffectPreserving(const Json& value, unsigned version, std::span<const uint8_t> original) {
    if (original.empty())
        return encodeEffect(value, version);
    auto encoded = encodeEffect(value, version);
    Reader source(original), destination(encoded);
    const auto previous = decodeEffect(original, version, true);
    const size_t oldParticle = 8 + source.at(4, 4), newParticle = 8 + destination.at(4, 4);
    if (value.at("emitter") == previous.at("emitter") && oldParticle == newParticle)
        std::copy(original.begin(), original.begin() + oldParticle, encoded.begin());
    const size_t oldParticleSize = 4 + source.at(oldParticle, 4), newParticleSize = 4 + destination.at(newParticle, 4);
    if (value.at("particle") == previous.at("particle") && oldParticleSize == newParticleSize)
        std::copy(original.begin() + oldParticle, original.begin() + oldParticle + oldParticleSize,
                  encoded.begin() + newParticle);
    else {
        // These three values are particle rotation offsets. The legacy JSON
        // groups them with texture slots and omits them for unnamed textures.
        const char* names[] = {"texture1", "texture2", "textureInd"};
        for (size_t i = 0; i < 3; ++i) {
            const auto texture = value.at("particle").value(names[i], Json::object());
            if (!texture.contains("rotationOffsetRandom"))
                encoded[newParticle + 4 + 121 + i] = uint8_t(source.at(oldParticle + 4 + 121 + i, 1));
            if (!texture.contains("rotationOffset")) {
                const auto bytes = source.slice(oldParticle + 4 + 124 + 4 * i, 4);
                std::copy(bytes.begin(), bytes.end(), encoded.begin() + newParticle + 4 + 124 + 4 * i);
            }
        }
    }
    // An unrelated property edit must not rewrite animation tables: their raw
    // range indices also participate in the game's deterministic random seed.
    struct RawTrack {
        Bytes bytes;
        bool init;
        unsigned group;
    };
    auto readTracks = [](std::span<const uint8_t> bytes, size_t offset) {
        Reader r(bytes);
        std::vector<std::tuple<size_t, bool, unsigned>> sizes;
        for (unsigned group = 0; group < 2; ++group) {
            const auto count = r.at(offset, 2), init = r.at(offset + 2, 2);
            for (size_t i = 0; i < count; ++i)
                sizes.emplace_back(r.at(offset + 4 + 4 * count + 4 * i, 4), i < init, group);
            offset += 4 + 8 * count;
        }
        std::vector<RawTrack> result;
        for (auto [size, init, group] : sizes) {
            const auto data = r.slice(offset, size);
            result.push_back({Bytes(data.begin(), data.end()), init, group});
            offset += size;
        }
        return result;
    };
    const auto originals = readTracks(original, oldParticle + oldParticleSize);
    auto replacements = readTracks(encoded, newParticle + newParticleSize);
    std::vector<const Json*> sorted;
    for (unsigned group = 0; group < 2; ++group)
        for (bool init : {true, false})
            for (const auto& animation : value.at("animations"))
                if (unsigned(animation.at("target").get<std::string>().starts_with("Emitter")) == group &&
                    animation.value("isInit", false) == init)
                    sorted.push_back(&animation);
    std::vector<bool> used(originals.size());
    for (size_t i = 0; i < replacements.size(); ++i) {
        const auto& animation = *sorted.at(i);
        if (animation.at("target") == "EmitterParam" &&
            value.at("emitter").at("shape") != previous.at("emitter").at("shape"))
            continue;
        for (size_t j = 0; j < originals.size(); ++j)
            if (!used[j] && nlohmann::json(animation) == nlohmann::json(previous.at("animations")[j])) {
                replacements[i].bytes = originals[j].bytes;
                used[j] = true;
                break;
            }
    }
    Writer result;
    result.append(std::span<const uint8_t>(encoded).first(newParticle + newParticleSize));
    for (unsigned group = 0; group < 2; ++group) {
        size_t count = 0, init = 0;
        for (const auto& track : replacements)
            if (track.group == group) {
                ++count;
                init += track.init;
            }
        result.integer(count, 2);
        result.integer(init, 2);
        result.zeros(count * 4);
        for (const auto& track : replacements)
            if (track.group == group)
                result.integer(track.bytes.size(), 4);
    }
    for (const auto& track : replacements)
        result.append(track.bytes);
    return std::move(result.bytes);
}
}
