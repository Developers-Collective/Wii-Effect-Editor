#include "compatibility.h"
#include "animation.h"
#include "version7.h"
#include "version5.h"
#include "legacy_animation.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>

namespace breff::codec {

    namespace {
        std::array<int, 3> remapLegacyTextures(Json& effect, std::vector<std::string>& warnings) {
            constexpr const char* slots[] = {"texture1", "texture2", "textureInd"};
            constexpr const char* enabled[] = {"useTexture1", "useTexture2", "useIndirectTexture"};
            constexpr const char* projection[] = {"isTexture1Projection", "isTexture2Projection",
                                                  "isIndirectTextureProjection"};
            const auto original = effect;
            std::array<int, 3> mapping{-1, -1, 2};
            auto& stages = effect["emitter"]["tevStages"];
            for (size_t i = 0; i < stages.size(); ++i) {
                const auto& stage = stages[i];
                bool samplesTexture = false;
                for (const char* channel : {"color", "alpha"})
                    for (const char* component : {"A", "B", "C", "D"}) {
                        const auto argument = stage.at(std::string(channel) + "Selection" + component);
                        samplesTexture |= argument == "TextureColor" || argument == "TextureAlpha";
                    }
                if (!samplesTexture)
                    continue;
                const int source = stage.at("texture");
                const int destination = i == 0 ? 0 : 1;
                if (source < 0 || source >= 3)
                    continue;
                if (mapping[destination] != -1 && mapping[destination] != source) {
                    warnings.push_back("TEV stages after the first require different textures. This runtime gives "
                                       "all of them one shared texture slot.");
                    continue;
                }
                mapping[destination] = source;
            }
            for (unsigned destination = 0; destination < 3; ++destination) {
                if (mapping[destination] == -1)
                    mapping[destination] = int(destination);
                const auto source = unsigned(mapping[destination]);
                if (source == destination)
                    continue;
                auto slot = original.at("particle").at(slots[source]);
                // These historical JSON members describe particle rotation axes,
                // not texture transforms. They must not move with a texture slot.
                for (const char* key : {"rotationOffset", "rotationOffsetRandom"})
                    slot[key] = original.at("particle").at(slots[destination]).value(key, Json(0));
                effect["particle"][slots[destination]] = std::move(slot);
                auto& flags = effect["emitter"]["drawFlags"];
                const auto& originalFlags = original.at("emitter").at("drawFlags");
                flags[enabled[destination]] = originalFlags.at(enabled[source]);
                flags[projection[destination]] = originalFlags.at(projection[source]);
            }
            return mapping;
        }

        void remapLegacyAlpha(Json& emitter, std::vector<std::string>& warnings) {
            const auto original = emitter;
            auto& input = emitter["colorInput"];
            const auto& alpha = original.at("alphaInput");
            constexpr const char* registers[] = {"Alpha0", "Alpha1", "Alpha2", "OutputAlpha"};
            std::array<std::string, 4> expected, actual;
            std::array<std::string, 4> constants, expectedConstants;
            std::array<bool, 3> needsColor{}, colorWritten{}, alphaWritten{}, locked{};
            std::array<bool, 4> constantNeedsColor{}, constantLocked{};
            for (unsigned i = 0; i < 3; ++i) {
                const auto key = "tevColor" + std::to_string(i + 1);
                expected[i] = alpha.at(key);
                actual[i] = input.at(key);
            }
            expected[3] = actual[3] = "OutputAlpha";
            for (unsigned i = 0; i < 4; ++i) {
                const auto key = "tevKColor" + std::to_string(i + 1);
                constants[i] = input.at(key);
                expectedConstants[i] = alpha.at(key);
            }
            auto registerIndex = [&](const std::string& selection) {
                for (unsigned i = 0; i < 4; ++i)
                    if (selection == registers[i])
                        return int(i);
                return -1;
            };

            // Only an initial RGB value read before its first write constrains a
            // regular register. Constant RGB components never get overwritten.
            for (const auto& stage : original.at("tevStages")) {
                for (const char* channel : {"color", "alpha"})
                    for (const char* component : {"A", "B", "C", "D"}) {
                        const std::string selection = stage.at(std::string(channel) + "Selection" + component);
                        for (unsigned i = 0; i < 3; ++i)
                            if (selection == "Color" + std::to_string(i) && !colorWritten[i])
                                needsColor[i] = true;
                        const bool color = std::string_view(channel) == "color";
                        if (selection == (color ? "ConstantColorSelection" : "ConstantAlphaSelection")) {
                            const auto code =
                                enumValue(color ? "GXTevKColorSel" : "GXTevKAlphaSel",
                                          stage.at(color ? "constantColorSelection" : "constantAlphaSelection"));
                            if (code >= 12 && code < 28)
                                constantNeedsColor[code & 0x03] = true;
                        }
                    }
                const auto output = enumValue("GXTevRegID", stage.at("colorRegister"));
                if (output)
                    colorWritten[output - 1] = true;
            }

            for (size_t index = 0; index < emitter.at("tevStages").size(); ++index) {
                auto& stage = emitter["tevStages"][index];
                const auto& source = original.at("tevStages")[index];
                std::array<int, 2> selectedConstant{-1, -1};
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const bool color = channel == 0;
                    const auto selectionKey = color ? "constantColorSelection" : "constantAlphaSelection";
                    const auto selectionType = color ? "GXTevKColorSel" : "GXTevKAlphaSel";
                    const auto constantArgument = color ? "ConstantColorSelection" : "ConstantAlphaSelection";
                    const auto code = enumValue(selectionType, source.at(selectionKey));
                    // Fractions and RGB components keep their existing constant selector.
                    if (code < 28)
                        for (const char* component : {"A", "B", "C", "D"})
                            if (source.at(std::string(color ? "color" : "alpha") + "Selection" + component) ==
                                constantArgument)
                                selectedConstant[channel] = int(code);
                }
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const bool color = channel == 0;
                    const auto selectionKey = color ? "constantColorSelection" : "constantAlphaSelection";
                    const auto selectionType = color ? "GXTevKColorSel" : "GXTevKAlphaSel";
                    const auto constantArgument = color ? "ConstantColorSelection" : "ConstantAlphaSelection";
                    for (const char* component : {"A", "B", "C", "D"}) {
                        const auto key = std::string(color ? "color" : "alpha") + "Selection" + component;
                        const std::string argument = source.at(key);
                        std::string wanted;
                        const int reg = registerIndex(argument);
                        if (reg >= 0)
                            wanted = expected[reg];
                        else if (argument == constantArgument) {
                            const auto code = enumValue(selectionType, source.at(selectionKey));
                            if (code < 28)
                                continue;
                            wanted = expectedConstants[code & 0x03];
                        } else if (argument == "RasterAlpha") {
                            if (alpha.at("rasColor") != input.at("rasColor"))
                                warnings.push_back("TEV stage " + std::to_string(index + 1) +
                                                   ": separate raster alpha routing cannot be retained.");
                            continue;
                        } else
                            continue;

                        bool found = false;
                        auto useRegister = [&](unsigned i) {
                            stage[key] = registers[i];
                            if (i < 3 && !alphaWritten[i])
                                locked[i] = true;
                            found = true;
                        };
                        if (reg >= 0 && actual[reg] == wanted)
                            useRegister(unsigned(reg));
                        for (unsigned i = 0; i < 4 && !found; ++i)
                            if (actual[i] == wanted)
                                useRegister(i);
                        if (!found && wanted == "Null") {
                            stage[key] = "Zero";
                            found = true;
                        }
                        auto useConstant = [&](unsigned i) {
                            const int code = 28 + int(i);
                            if (selectedConstant[channel] != -1 && selectedConstant[channel] != code)
                                return false;
                            selectedConstant[channel] = code;
                            stage[selectionKey] = enumName(selectionType, unsigned(code));
                            stage[key] = constantArgument;
                            constantLocked[i] = true;
                            found = true;
                            return true;
                        };
                        for (unsigned i = 0; i < 4 && !found; ++i)
                            if (constants[i] == wanted)
                                useConstant(i);
                        // A free register may hold the needed alpha even though its
                        // RGB components will not be read by the shader.
                        for (unsigned i = 0; i < 3 && !found; ++i)
                            if (!needsColor[i] && !locked[i] && !alphaWritten[i] && !wanted.starts_with("stage:")) {
                                actual[i] = wanted;
                                input["tevColor" + std::to_string(i + 1)] = wanted;
                                useRegister(i);
                            }
                        for (unsigned i = 0; i < 4 && !found; ++i)
                            if (!constantNeedsColor[i] && !constantLocked[i] && !wanted.starts_with("stage:") &&
                                useConstant(i)) {
                                constants[i] = wanted;
                                input["tevKColor" + std::to_string(i + 1)] = wanted;
                            }
                        if (!found)
                            warnings.push_back("TEV stage " + std::to_string(index + 1) + ": cannot preserve " + key +
                                               " with the available shared color/alpha registers.");
                    }
                }
                const auto output = registerIndex(source.at("alphaRegister"));
                expected[output] = actual[output] = "stage:" + std::to_string(index);
                if (output < 3)
                    alphaWritten[output] = true;
            }
            emitter["alphaInput"] = input;
        }
    }

    EffectProjection projectEffect(const Json& retained, unsigned version) {
        if (version < 5 || version > 11)
            throw std::runtime_error("Unsupported BREFF version");
        EffectProjection result{retained, {}, {}};
        result.value["animations"] = Json::array();
        std::array<int, 3> textureMapping{0, 1, 2};
        if (version <= 7) {
            textureMapping = remapLegacyTextures(result.value, result.warnings);
            remapLegacyAlpha(result.value["emitter"], result.warnings);
            auto& emitter = result.value["emitter"];
            if (emitter.at("particleType") == "Billboard") {
                auto& options = emitter["particleOptions"];
                if (options.value("expression", std::string()) == "NormalNoRoll")
                    options["expression"] = "Normal";
                const auto ahead = options.value("yDirection", std::string());
                if (ahead == "Speed5" || ahead == "Speed6" || ahead == "Speed7")
                    options["yDirection"] = "Speed";
                if (version == 5 && ahead == "ParticleBoth") {
                    options["yDirection"] = "Particle";
                    if (options.value("expression", std::string()) == "Directional")
                        result.warnings.push_back("v5 cannot orient a billboard from both neighboring particles. "
                                                  "The previous-particle direction is used.");
                }
            }
            result.value["emitter"] =
                decodeEmitter(expandV7Emitter(packV7Emitter(encodeEmitter(result.value.at("emitter")))));
        }
        std::array<float, 3> rotationOffsets{};
        bool hasRotationAnimation = false;
        for (const auto& track : retained.at("animations"))
            hasRotationAnimation |= track.at("target") == "ParticleRotate" || track.at("target") == "ParticleRotation";
        Json initialRotation;
        if (version <= 6 && !hasRotationAnimation) {
            constexpr const char* slots[] = {"texture1", "texture2", "textureInd"};
            constexpr const char* axes[] = {"x", "y", "z"};
            bool randomOffset = false;
            for (const char* slot : slots) {
                const auto& value = retained.at("particle").at(slot);
                randomOffset |=
                    value.value("rotationOffset", 0.f) != 0.f && value.value("rotationOffsetRandom", 0) != 0;
            }
            if (randomOffset) {
                initialRotation = defaultAnimation("ParticleRotate", retained.at("emitter"));
                initialRotation["isInit"] = true;
                auto& frame = initialRotation["keyFrames"][0];
                frame["valueType"] = "Range";
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const auto& slot = retained.at("particle").at(slots[axis]);
                    const float offset = slot.value("rotationOffset", 0.f);
                    const float spread = offset * float(slot.value("rotationOffsetRandom", 0)) / 100.f;
                    const float base = retained.at("particle").at("particleRotation").at(axes[axis]);
                    frame[axes[axis]] = {{"interpolation", "Linear"},
                                         {"range", Json::array({base + offset - spread, 2.f * spread / 65535.f})}};
                }
            }
        }
        if (version <= 6) {
            constexpr const char* textures[] = {"texture1", "texture2", "textureInd"};
            constexpr const char* axes[] = {"x", "y", "z"};
            for (unsigned i = 0; i < 3; ++i) {
                const auto slot = result.value.at("particle").value(textures[i], Json::object());
                const float offset = slot.value("rotationOffset", 0.f);
                constexpr float pi = 3.14159265358979323846f;
                rotationOffsets[i] = float(uint8_t(int(std::ceil(offset / pi * 128.f - .5f)))) * pi / 128.f;
                result.value["particle"]["particleRotation"][axes[i]] =
                    retained.at("particle").at("particleRotation").at(axes[i]).get<float>() + rotationOffsets[i];
                if (offset != 0.f && slot.value("rotationOffsetRandom", 0) != 0)
                    result.warnings.push_back(
                        initialRotation.is_null()
                            ? std::string("Independent random ") + axes[i] +
                                  " rotation offsets cannot persist alongside animated rotation before v7. The fixed offset is retained."
                            : "Random rotation offsets are translated into initialization ranges. The older runtime "
                              "cannot reproduce their separate quantization and random sequence exactly.");
            }
            result.value["particle"] = decodeParticle(
                expandLegacyParticle(packLegacyParticle(encodeParticle(result.value.at("particle")))), true);
        }
        if (!initialRotation.is_null()) {
            result.value["animations"].push_back(std::move(initialRotation));
            result.animationIndices.push_back(std::numeric_limits<size_t>::max());
        }
        const auto& animations = retained.at("animations");
        for (size_t i = 0; i < animations.size(); ++i) {
            auto track = animations[i];
            auto target = track.at("target").get<std::string>();
            if (version <= 7 && target == "ParticleRotation") {
                target = "ParticleRotate";
                track["target"] = target;
            }
            if (!animationSupported(target, version)) {
                if (target.starts_with("PostField"))
                    result.warnings.push_back(
                        "Particle collision, bounce, and collision-triggered child effects cannot "
                        "be represented by the force fields available before v8.");
                else
                    result.warnings.push_back(target + " animation is unavailable in v" + std::to_string(version) +
                                              ".");
                continue;
            }
            if (version == 5 && track.value("isBaked", false)) {
                auto keys = Json::array();
                for (const auto& values : track.at("frames")) {
                    Json key = {{"frame", keys.size()}, {"valueType", "Fixed"}};
                    for (const auto& [name, value] : values.items())
                        if (name == "t") {
                            key["value"] = value;
                            key["interpolation"] = "Step";
                        } else
                            key[name] = {{"value", value}, {"interpolation", "Step"}};
                    keys.push_back(std::move(key));
                }
                track["frameCount"] = keys.size();
                track["keyFrames"] = std::move(keys);
                track.erase("frames");
                track["isBaked"] = false;
            }
            if (version <= 6 && target == "ParticleRotate") {
                constexpr const char* axes[] = {"x", "y", "z"};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    if (!rotationOffsets[axis] || !track.at("subTargets").value(axes[axis], false))
                        continue;
                    if (track.value("isBaked", false))
                        for (auto& frame : track["frames"])
                            frame[axes[axis]] = frame[axes[axis]].get<float>() + rotationOffsets[axis];
                    for (auto& frame : track["keyFrames"]) {
                        auto& component = frame[axes[axis]];
                        if (component.contains("value"))
                            component["value"] = component["value"].get<float>() + rotationOffsets[axis];
                        if (component.contains("range"))
                            component["range"][0] = component["range"][0].get<float>() + rotationOffsets[axis];
                        if (frame.value("randomRotationDirection", false))
                            result.warnings.push_back(
                                "Random rotation direction can also reverse the folded rotation offset before v7.");
                    }
                    for (auto& pool : track["randomPool"])
                        if (pool.contains(axes[axis]) && !pool[axes[axis]].empty())
                            pool[axes[axis]][0] = pool[axes[axis]][0].get<float>() + rotationOffsets[axis];
                }
            }
            auto translated = translateLegacyAnimation(track, result.value, version, result.warnings);
            for (auto& track : translated) {
                if (version <= 7) {
                    if (target == "Child")
                        for (const char* table : {"frames", "randomPool"})
                            for (const auto& entry : track.at(table)) {
                                if (!entry.value("alpha", 0))
                                    continue;
                                const auto& primary = entry.at("alphaPrimarySources");
                                const auto& secondary = entry.at("alphaSecondarySources");
                                if (!primary.value("primaryAlpha", false) || primary.value("secondaryAlpha", false) ||
                                    primary.value("alphaFlickAndModifier", false) ||
                                    secondary.value("primaryAlpha", false) ||
                                    !secondary.value("secondaryAlpha", false) ||
                                    secondary.value("alphaFlickAndModifier", false))
                                    result.warnings.push_back(
                                        "Child alpha inheritance combines or redirects parent alpha "
                                        "channels. This runtime can inherit only matching channels.");
                            }
                    if (target == "FieldSpeed" || target == "FieldRandom") {
                        const auto& info = track.at("info");
                        if (target == "FieldSpeed" && info.value("addTarget", std::string("Velocity")) != "Velocity")
                            result.warnings.push_back(target + ": this runtime can apply the field only to velocity, "
                                                               "not directly to position.");
                        if (target == "FieldRandom" && info.value("space", std::string("Emitter")) != "Emitter")
                            result.warnings.push_back("The older random field operates in emitter space. Independently "
                                                      "rotating emitter/world axes cannot be retained.");
                    }
                    if (target == "FieldRandom") {
                        const auto& options = track.at("info").at("option");
                        if (options.value("randomSaveVelocity", false))
                            result.warnings.push_back(
                                "The older random field cannot preserve the particle's speed magnitude.");
                        if (!options.value("randomEnableX", false) || !options.value("randomEnableY", false) ||
                            !options.value("randomEnableZ", false))
                            result.warnings.push_back(
                                "The older random field cannot independently disable individual axes.");
                        if (track["subTargets"].value("diffusion", false)) {
                            result.warnings.push_back("The older random field has a fixed diffusion angle. Animated "
                                                      "random-direction diffusion cannot be retained.");
                            track["subTargets"]["diffusion"] = false;
                        }
                    }
                    if (target == "FieldGravity")
                        for (const char* axis : {"xRot", "yRot", "zRot"}) {
                            track["subTargets"][axis] = false;
                        }
                    track = decodeAnimation(
                        expandV7Animation(packV7Animation(encodeAnimation(track, retained.at("emitter")))),
                        result.value.at("emitter"), track.value("isInit", false));
                }
                if (version == 5) {
                    auto bytes = packV7Animation(encodeAnimation(track, result.value.at("emitter")));
                    const auto narrowed = decodeAnimation(expandV7Animation(expandV5Animation(packV5Animation(bytes))),
                                                          result.value.at("emitter"), track.value("isInit", false));
                    track = narrowed;
                }
                bool textureTrack = false;
                constexpr const char* textureTargets[] = {"Texture1", "Texture2", "TextureInd"};
                for (unsigned source = 0; source < 3; ++source)
                    if (target.starts_with(textureTargets[source])) {
                        textureTrack = true;
                        for (unsigned destination = 0; destination < 3; ++destination)
                            if (textureMapping[destination] == int(source)) {
                                auto remapped = track;
                                remapped["target"] = std::string(textureTargets[destination]) +
                                                     target.substr(std::string_view(textureTargets[source]).size());
                                result.animationIndices.push_back(i);
                                result.value["animations"].push_back(std::move(remapped));
                            }
                        break;
                    }
                if (!textureTrack) {
                    result.animationIndices.push_back(i);
                    result.value["animations"].push_back(std::move(track));
                }
            }
        }
        std::sort(result.warnings.begin(), result.warnings.end());
        result.warnings.erase(std::unique(result.warnings.begin(), result.warnings.end()), result.warnings.end());
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
        std::vector<std::vector<size_t>> rows(retained.at("animations").size());
        for (size_t i = 0; i < after.size(); ++i)
            if (indices[i] != added && projection.animationIndices.at(indices[i]) != added)
                rows[projection.animationIndices.at(indices[i])].push_back(i);
        std::vector<bool> emitted(rows.size());
        size_t cursor = 0;
        auto emit = [&](size_t original) {
            if (emitted[original])
                return;
            emitted[original] = true;
            const auto count =
                std::count(projection.animationIndices.begin(), projection.animationIndices.end(), original);
            if (!count) {
                tracks.push_back(retained.at("animations")[original]);
                return;
            }
            bool unchanged = rows[original].size() == size_t(count);
            for (const auto row : rows[original])
                unchanged &= before[indices[row]] == after[row];
            if (unchanged) {
                tracks.push_back(retained.at("animations")[original]);
                return;
            }
            for (const auto row : rows[original])
                tracks.push_back(count == 1
                                     ? merge(retained.at("animations")[original], before[indices[row]], after[row])
                                     : after[row]);
        };
        for (size_t i = 0; i < after.size(); ++i) {
            if (indices[i] == added || projection.animationIndices.at(indices[i]) == added) {
                if (indices[i] == added || before[indices[i]] != after[i])
                    tracks.push_back(after[i]);
                continue;
            }
            const auto original = projection.animationIndices.at(indices[i]);
            while (cursor < original) {
                emit(cursor);
                ++cursor;
            }
            emit(original);
            cursor = std::max(cursor, original + 1);
        }
        while (cursor < retained.at("animations").size())
            emit(cursor++);
        result["animations"] = std::move(tracks);
        return result;
    }
}
