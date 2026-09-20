#include "form.h"
#include "animation.h"
#include <algorithm>
namespace breff::codec {
namespace {
void restrictForm(Json& node, const Json& model, unsigned version) {
    if (version != 7)
        return;
    const auto path = node.value("path", std::string());
    std::string target;
    if (path.starts_with("/animations/")) {
        const auto start = std::string("/animations/").size();
        const auto end = path.find('/', start);
        const auto index = std::stoul(path.substr(start, end - start));
        target = model.at("animations").at(index).at("target").get<std::string>();
    }
    if (path.ends_with("/target") && path.starts_with("/animations/")) {
        auto& choices = node["choices"];
        std::erase_if(choices.get_ref<Json::array_t&>(), [](const Json& choice) {
            return !animationSupported(choice.get<std::string>(), 7);
        });
    }
    if (target == "FieldGravity" && path.ends_with("/subTargets")) {
        auto& options = node["choices"]["options"];
        std::erase_if(options.get_ref<Json::array_t&>(), [](const Json& option) {
            return option.at("key") != "power";
        });
    }
    if (!node.contains("children"))
        return;
    auto& children = node["children"].get_ref<Json::array_t&>();
    std::erase_if(children, [&](const Json& child) {
        const auto field = child.value("path", std::string());
        if (field == "/emitter/alphaInput" ||
            (field.starts_with("/emitter/tevStages/") && field.ends_with("/texture")) ||
            field.ends_with("/alphaPrimarySources") || field.ends_with("/alphaSecondarySources"))
            return true;
        if (target == "FieldGravity" &&
            (field.find("/keyFrames/") != std::string::npos || field.find("/frames/") != std::string::npos ||
             field.find("/randomPool/") != std::string::npos) &&
            (field.ends_with("/xRot") || field.ends_with("/yRot") || field.ends_with("/zRot")))
            return true;
        if ((target == "FieldSpeed" || target == "FieldRandom") &&
            (field.ends_with("/info/space") || field.ends_with("/info/addTarget")))
            return true;
        return target == "FieldSpeed" && field.ends_with("/info/option");
    });
    for (auto& child : children)
        restrictForm(child, model, version);
}
}
Json effectForm(const Json& model, unsigned version) {
    Json root = {{"kind", "root"}, {"children", Json::array()}};
    const auto emitter = model.value("emitter", Json::object());
    root["children"].push_back(recordForm("EmitterData", emitter, "/emitter", "emitter"));
    auto particle = recordForm("ParticleData", model.value("particle", Json::object()), "/particle", "particle");
    Json offsets = {{"kind", "record"},
                    {"path", "/particle/rotationOffsets"},
                    {"label", "Particle rotation offsets"},
                    {"children", Json::array()}};
    const char* slots[] = {"texture1", "texture2", "textureInd"};
    const char* axes[] = {"X", "Y", "Z"};
    for (unsigned i = 0; i < 3; ++i) {
        const std::string path = std::string("/particle/") + slots[i];
        offsets["children"].push_back(
            scalarForm(path + "/rotationOffset", std::string(axes[i]) + " offset (radians)", 0.f));
        offsets["children"].push_back(
            scalarForm(path + "/rotationOffsetRandom", std::string(axes[i]) + " randomness (%)", 0));
        particle["children"].push_back(
            {{"kind", "texture"}, {"path", path}, {"label", slots[i]}, {"default", Json::object()}});
    }
    particle["children"].push_back(offsets);
    root["children"].push_back(particle);
    Json animations = {{"kind", "list"},           {"path", "/animations"},
                       {"label", "animations"},    {"fixedLength", nullptr},
                       {"maxLength", 65535},       {"default", defaultAnimation("ParticleSize", emitter)},
                       {"children", Json::array()}};
    const auto tracks = model.value("animations", Json::array());
    for (size_t i = 0; i < tracks.size(); ++i)
        animations["children"].push_back(
            animationForm(tracks[i], emitter, "/animations/" + std::to_string(i), "[" + std::to_string(i) + "]"));
    root["children"].push_back(animations);
    restrictForm(root, model, version);
    return root;
}
}
