#include "legacy_animation.h"
#include "animation.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>

namespace breff::codec {
    namespace {
        float initialValue(const Json& effect, const Json& track, const std::string& component) {
            const std::string target = track.at("target");
            const auto& particle = effect.at("particle");
            const auto& emitter = effect.at("emitter");
            if (target.starts_with("Field")) {
                const auto& info = track.at("info");
                if (info.contains(component))
                    return info.at(component).get<float>();
                if (component.ends_with("Rot"))
                    return info.at("rotation").at(component.substr(0, 1)).get<float>();
                if (component.ends_with("Trans"))
                    return info.at("translation").at(component.substr(0, 1)).get<float>();
            }
            for (const char* name : {"ParticleSize", "ParticleScale", "ParticleRotate", "Color1Primary",
                                     "Color1Secondary", "Color2Primary", "Color2Secondary"})
                if (target == name) {
                    auto field = target == "ParticleRotate" ? std::string("particleRotation") : target;
                    field[0] = char(std::tolower(static_cast<unsigned char>(field[0])));
                    return particle.at(field).at(component).get<float>();
                }
            for (const char* name : {"Texture1", "Texture2", "TextureInd"})
                if (target.starts_with(name)) {
                    auto field = std::string(name);
                    field[0] = 't';
                    const auto& texture = particle.at(field);
                    const auto suffix = target.substr(field.size());
                    if (suffix == "Scale")
                        return texture.at("scale").at(component).get<float>();
                    if (suffix == "Translation")
                        return texture.at("translation").at(component).get<float>();
                }
            if (target == "EmitterParam")
                return emitter.at("shapeParams").at(component).get<float>();
            for (const char* name : {"EmitterScale", "EmitterRotation", "EmitterTranslation"})
                if (target == name) {
                    auto field = target.substr(7);
                    field[0] = char(std::tolower(static_cast<unsigned char>(field[0])));
                    return emitter.at(field).at(component).get<float>();
                }
            if (target == "EmitterSpeedSpecDir") {
                if (component == "powerSpecDir")
                    return emitter.at("specifiedDirEmissionSpeed").get<float>();
                if (component == "diffusionSpecDir")
                    return emitter.at("specifiedDirDiffusionAngle").get<float>();
                return emitter.at("specifiedDir").at(std::string(1, char(std::tolower(component.back())))).get<float>();
            }
            throw std::runtime_error("Missing legacy animation default for " + target + "/" + component);
        }

        bool fixedKeys(const Json& track) {
            for (const auto& frame : track.value("keyFrames", Json::array()))
                if (frame.at("valueType") != "Fixed")
                    return false;
            return !track.value("keyFrames", Json::array()).empty();
        }

        const Json& componentValue(const Json& frame, const std::string& component) {
            return component == "t" ? frame : frame.at(component);
        }

        float sample(const Json& track, const std::string& component, unsigned time, float fallback) {
            if (track.contains("subTargets") && !track.at("subTargets").value(component, false))
                return fallback;
            const auto& keys = track.at("keyFrames");
            if (keys.empty())
                return fallback;
            size_t left = 0;
            while (left + 1 < keys.size() && keys[left + 1].at("frame").get<unsigned>() <= time)
                ++left;
            const auto& a = componentValue(keys[left], component);
            const float value = a.at("value");
            const unsigned start = keys[left].at("frame");
            if (left + 1 == keys.size() || time <= start)
                return value;
            const float next = componentValue(keys[left + 1], component).at("value");
            float t = float(time - start) / float(keys[left + 1].at("frame").get<unsigned>() - start);
            const std::string interpolation = a.at("interpolation");
            if (interpolation == "Step")
                return value;
            if (interpolation == "Hermite") {
                const auto slopes = a.value("slopeAdjust", Json::object());
                const float s = slopes.value("endSlopeAdjust", false) ? 1.5f : 0.f;
                const float e = slopes.value("startSlopeAdjust", false) ? 1.5f : 0.f;
                t *= s + t * (t * (s + e - 2.f) + 3.f - 2.f * s - e);
            }
            return value + t * (next - value);
        }

        void preservePreviousComponents(Json& track, const Json& effect, std::vector<std::string>& warnings) {
            if (!track.contains("subTargets") || track.at("target").get<std::string>().starts_with("Field"))
                return;
            std::map<std::string, const Json*> previous;
            for (const auto& [component, enabled] : track.at("subTargets").items()) {
                if (enabled.get<bool>())
                    continue;
                for (const auto& candidate : effect.at("animations"))
                    if (candidate.at("target") == track.at("target") &&
                        candidate.value("subTargets", Json::object()).value(component, false))
                        previous[component] = &candidate;
            }
            if (previous.empty())
                return;
            bool sameKeys = fixedKeys(track);
            for (const auto& [component, curve] : previous) {
                sameKeys &= fixedKeys(*curve) && curve->at("processFlag") == track.at("processFlag") &&
                            curve->at("frameCount") == track.at("frameCount") &&
                            curve->at("loopCount") == track.at("loopCount") &&
                            curve->at("isInit") == track.at("isInit") &&
                            curve->at("keyFrames").size() == track.at("keyFrames").size();
                if (sameKeys)
                    for (size_t i = 0; i < track.at("keyFrames").size(); ++i)
                        sameKeys &= curve->at("keyFrames")[i].at("frame") == track.at("keyFrames")[i].at("frame");
            }
            if (sameKeys) {
                for (const auto& [component, curve] : previous) {
                    track["subTargets"][component] = true;
                    for (size_t i = 0; i < track.at("keyFrames").size(); ++i)
                        track["keyFrames"][i][component] = curve->at("keyFrames")[i].at(component);
                }
                return;
            }
            auto finite = [](const Json& curve) {
                const auto& flags = curve.at("processFlag");
                return fixedKeys(curve) && !flags.value("fitting", false) && !flags.value("loopInfinitely", false) &&
                       curve.value("loopCount", 0) < 2;
            };
            bool canCombine = finite(track);
            unsigned frames = std::max(1u, track.at("frameCount").get<unsigned>());
            for (const auto& [component, curve] : previous) {
                canCombine &= finite(*curve) && curve->at("processFlag").at("emitterTiming") ==
                                                    track.at("processFlag").at("emitterTiming");
                if (!curve->value("isInit", false))
                    frames = std::max(frames, curve->at("frameCount").get<unsigned>());
            }
            if (!canCombine) {
                warnings.push_back(
                    track.at("target").get<std::string>() +
                    ": v5 updates all components together. Independent random, looping, or fitted component "
                    "curves cannot retain their separate timing and random streams in one vector curve.");
                return;
            }
            Json combined = Json::array();
            const auto components = defaultAnimation(track.at("target"), effect.at("emitter")).at("subTargets");
            unsigned lastKey = 0;
            for (const auto& frame : track.at("keyFrames"))
                lastKey = std::max(lastKey, frame.at("frame").get<unsigned>());
            for (const auto& [component, curve] : previous)
                if (!curve->value("isInit", false))
                    for (const auto& frame : curve->at("keyFrames"))
                        lastKey = std::max(lastKey, frame.at("frame").get<unsigned>());
            for (unsigned time = 0; time < std::min(frames, lastKey + 1); ++time) {
                Json frame = {{"frame", time}, {"valueType", "Fixed"}};
                for (const auto& [component, enabled] : components.items()) {
                    const auto found = previous.find(component);
                    const Json& source = found == previous.end() ? track : *found->second;
                    const unsigned at = found != previous.end() && source.value("isInit", false)
                                            ? 0u
                                            : std::min(time, std::max(1u, source.at("frameCount").get<unsigned>()) - 1);
                    frame[component] = {
                        {"value", sample(source, component, at, initialValue(effect, track, component))},
                        {"interpolation", "Linear"}};
                }
                combined.push_back(std::move(frame));
            }
            track["keyFrames"] = std::move(combined);
            track["subTargets"] = components;
            track["frameCount"] = frames;
        }

        void completeV5Components(Json& track, const Json& effect) {
            if (!track.contains("subTargets"))
                return;
            const std::string target = track.at("target");
            const auto defaults = defaultAnimation(target, effect.at("emitter"));
            for (const auto& [component, enabled] : defaults.at("subTargets").items()) {
                if ((target == "FieldGravity" || target == "FieldRandom") && component != "power")
                    continue;
                if (track["subTargets"].value(component, false))
                    continue;
                const float base = initialValue(effect, track, component);
                track["subTargets"][component] = true;
                if (track["keyFrames"].empty())
                    track["keyFrames"].push_back({{"frame", 0}, {"valueType", "Fixed"}});
                for (auto& frame : track["keyFrames"]) {
                    auto& value = frame[component];
                    value = {{"interpolation", "Linear"}};
                    if (frame.at("valueType") == "Fixed")
                        value["value"] = base;
                    else if (frame.at("valueType") == "Range")
                        value["range"] = Json::array({base, 0.f});
                }
                for (auto& entry : track["randomPool"])
                    entry[component] = Json::array({base, 0.f});
            }
        }

        unsigned sampleCount(const Json& track) {
            unsigned last = 0;
            for (const auto& frame : track.at("keyFrames"))
                last = std::max(last, frame.at("frame").get<unsigned>());
            return std::min(last + 1, std::max(1u, track.at("frameCount").get<unsigned>()));
        }

        void translateSlopes(Json& track, std::vector<std::string>& warnings) {
            bool adjusted = false;
            auto inspect = [&](const Json& value) {
                const auto slopes = value.value("slopeAdjust", Json::object());
                adjusted |= value.value("interpolation", std::string()) == "Hermite" &&
                            (slopes.value("startSlopeAdjust", false) || slopes.value("endSlopeAdjust", false));
            };
            for (const auto& frame : track.value("keyFrames", Json::array())) {
                inspect(frame);
                for (const auto& [name, value] : frame.items())
                    if (value.is_object())
                        inspect(value);
            }
            if (!adjusted)
                return;
            const std::string target = track.at("target");
            if (!fixedKeys(track)) {
                warnings.push_back(target + ": v5 cannot combine random endpoints with adjusted Hermite slopes.");
                return;
            }
            auto keys = Json::array();
            const auto components = track.value("subTargets", Json{{"t", true}});
            const bool bytes = target.starts_with("Color") || target.starts_with("Alpha");
            for (unsigned time = 0; time < sampleCount(track); ++time) {
                Json frame = {{"frame", time}, {"valueType", "Fixed"}};
                for (const auto& [component, enabled] : components.items())
                    if (enabled.get<bool>()) {
                        const auto value = sample(track, component, time, 0.f);
                        Json entry = {{"value", bytes ? Json(unsigned(std::clamp(value, 0.f, 255.f))) : Json(value)},
                                      {"interpolation", "Linear"}};
                        if (component == "t")
                            frame.update(entry);
                        else
                            frame[component] = std::move(entry);
                    }
                keys.push_back(std::move(frame));
            }
            track["keyFrames"] = std::move(keys);
            if (track.at("processFlag").value("fitting", false))
                warnings.push_back(target + ": v5 preserves adjusted slopes at whole frames, but lifetime fitting "
                                            "can sample between those frames.");
        }
    }

    std::vector<Json> translateLegacyAnimation(const Json& source, const Json& effect, unsigned version,
                                               std::vector<std::string>& warnings) {
        auto track = source;
        if (version == 5) {
            preservePreviousComponents(track, effect, warnings);
            completeV5Components(track, effect);
            translateSlopes(track, warnings);
        }
        if (version > 7 || track.at("target") != "FieldGravity")
            return {std::move(track)};
        const auto& subTargets = track.at("subTargets");
        if (!subTargets.value("xRot", false) && !subTargets.value("yRot", false) && !subTargets.value("zRot", false))
            return {std::move(track)};
        if (!fixedKeys(track)) {
            warnings.push_back("The older gravity field cannot reproduce a randomly animated direction.");
            return {std::move(track)};
        }
        // Gravity is additive. Three fixed world/emitter axes reproduce its
        // animated direction using scalar power tracks supported by v5-v7.
        std::vector<Json> axes(3, track);
        constexpr float pi = 3.14159265358979323846f;
        for (unsigned axis = 0; axis < 3; ++axis) {
            axes[axis]["subTargets"] = {{"power", true}, {"xRot", false}, {"yRot", false}, {"zRot", false}};
            axes[axis]["info"]["rotation"] = {
                {"x", axis == 2 ? pi / 2.f : 0.f}, {"y", 0.f}, {"z", axis == 0 ? -pi / 2.f : 0.f}};
            axes[axis]["keyFrames"] = Json::array();
        }
        const auto& info = track.at("info");
        for (unsigned time = 0; time < sampleCount(track); ++time) {
            const float power = sample(track, "power", time, info.at("power"));
            const float x = sample(track, "xRot", time, info.at("rotation").at("x"));
            const float y = sample(track, "yRot", time, info.at("rotation").at("y"));
            const float z = sample(track, "zRot", time, info.at("rotation").at("z"));
            const float values[] = {power * (std::sin(x) * std::sin(y) * std::cos(z) - std::cos(x) * std::sin(z)),
                                    power * (std::sin(x) * std::sin(y) * std::sin(z) + std::cos(x) * std::cos(z)),
                                    power * std::sin(x) * std::cos(y)};
            for (unsigned axis = 0; axis < 3; ++axis)
                axes[axis]["keyFrames"].push_back({{"frame", time},
                                                   {"valueType", "Fixed"},
                                                   {"power", {{"value", values[axis]}, {"interpolation", "Linear"}}}});
        }
        if (track.at("processFlag").value("fitting", false))
            warnings.push_back("Animated gravity is preserved at whole frames. Lifetime fitting can interpolate "
                               "between the converted direction samples.");
        return axes;
    }
}
