#include "compatibility.h"
#include "animation.h"
#include "version7.h"
#include <algorithm>
#include <limits>

namespace breff::codec {
EffectProjection projectEffect(const Json& retained, unsigned version) {
    if (version < 7 || version > 11)
        throw std::runtime_error("Unsupported BREFF version");
    EffectProjection result{retained, {}};
    result.value["animations"] = Json::array();
    if (version == 7)
        result.value["emitter"] = decodeEmitter(expandV7Emitter(packV7Emitter(encodeEmitter(retained.at("emitter")))));
    const auto& animations = retained.at("animations");
    for (size_t i = 0; i < animations.size(); ++i) {
        auto track = animations[i];
        const auto target = track.at("target").get<std::string>();
        if (!animationSupported(target, version))
            continue;
        if (version == 7) {
            if (target == "FieldGravity")
                for (const char* axis : {"xRot", "yRot", "zRot"})
                    track["subTargets"][axis] = false;
            track = decodeAnimation(expandV7Animation(packV7Animation(encodeAnimation(track, retained.at("emitter")))),
                                    result.value.at("emitter"), track.value("isInit", false));
        }
        result.animationIndices.push_back(i);
        result.value["animations"].push_back(std::move(track));
    }
    return result;
}
namespace {
constexpr size_t added = std::numeric_limits<size_t>::max();
// Preserve identity across list insertions/removals; unchanged rows anchor edits.
std::vector<size_t> align(const Json& before, const Json& after) {
    std::vector<size_t> result(after.size(), added);
    if (before.size() == after.size()) {
        for (size_t i = 0; i < after.size(); ++i)
            result[i] = i;
        return result;
    }
    size_t old = 0, next = 0;
    while (old < before.size() && next < after.size()) {
        if (before[old] == after[next]) {
            result[next++] = old++;
            continue;
        }
        size_t remove = old + 1, insert = next + 1;
        while (remove < before.size() && before[remove] != after[next])
            ++remove;
        while (insert < after.size() && after[insert] != before[old])
            ++insert;
        if (remove < before.size() && (insert == after.size() || remove - old <= insert - next))
            old = remove;
        else if (insert < after.size())
            next = insert;
        else
            result[next++] = old++;
    }
    return result;
}
Json merge(const Json& retained, const Json& before, const Json& after) {
    if (before == after)
        return retained;
    if (before.is_object() && after.is_object() && retained.is_object()) {
        if (after.empty())
            return after;
        auto result = retained;
        for (auto it = before.begin(); it != before.end(); ++it)
            if (!after.contains(it.key()))
                result.erase(it.key());
        for (auto it = after.begin(); it != after.end(); ++it)
            result[it.key()] = before.contains(it.key()) && retained.contains(it.key())
                                   ? merge(retained.at(it.key()), before.at(it.key()), it.value())
                                   : it.value();
        return result;
    }
    if (before.is_array() && after.is_array() && retained.is_array() && before.size() == retained.size()) {
        auto result = Json::array();
        const auto indices = align(before, after);
        for (size_t i = 0; i < after.size(); ++i)
            result.push_back(indices[i] == added ? after[i]
                                                 : merge(retained[indices[i]], before[indices[i]], after[i]));
        return result;
    }
    return after;
}
}
Json mergeEffectEdit(const Json& retained, const Json& edited, unsigned version) {
    const auto projection = projectEffect(retained, version);
    auto result = retained;
    for (const char* key : {"emitter", "particle"})
        result[key] = merge(retained.at(key), projection.value.at(key), edited.at(key));
    const auto& before = projection.value.at("animations");
    const auto& after = edited.at("animations");
    const auto indices = align(before, after);
    auto tracks = Json::array();
    // Hidden tracks belong to the session, not the narrowed editable list.
    size_t cursor = 0;
    for (size_t i = 0; i < after.size(); ++i) {
        if (indices[i] == added) {
            tracks.push_back(after[i]);
            continue;
        }
        const auto original = projection.animationIndices.at(indices[i]);
        while (cursor < original) {
            if (!std::binary_search(projection.animationIndices.begin(), projection.animationIndices.end(), cursor))
                tracks.push_back(retained.at("animations")[cursor]);
            ++cursor;
        }
        tracks.push_back(merge(retained.at("animations")[original], before[indices[i]], after[i]));
        cursor = original + 1;
    }
    while (cursor < retained.at("animations").size()) {
        if (!std::binary_search(projection.animationIndices.begin(), projection.animationIndices.end(), cursor))
            tracks.push_back(retained.at("animations")[cursor]);
        ++cursor;
    }
    result["animations"] = std::move(tracks);
    return result;
}
}
