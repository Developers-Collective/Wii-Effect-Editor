#pragma once
#include <nw4r/ef.h>
#include <filesystem>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace breff {
struct EffectResource {
    nw4r::ef::EmitterResource resource;
    nw4r::ef::ParticleParameterDesc particle{};
    std::array<std::string,3> textures;
};
struct TextureResource {
    nw4r::ef::TextureData resource{};
    std::string name;
    std::vector<uint8_t> image,palette;
};
class Archive {
public:
    std::map<std::string,std::unique_ptr<EffectResource>> effects;
    std::map<std::string,std::unique_ptr<TextureResource>> textures;
    void load(const std::filesystem::path& breff,const std::filesystem::path& breft);
};
}
