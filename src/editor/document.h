#pragma once
#include "../codec/resource_file.h"
#include "../codec/records.h"
#include <filesystem>
#include <map>

namespace breff {
class DocumentService {
    using Json=codec::Json;
    struct State {
        codec::ResourceFile effects, textures;
        Json values=Json::object(), originalValues=Json::object(), errors=Json::object();
        std::string selected;
        bool loaded=false;
    } document;
    std::vector<State> undo,redo;
    std::filesystem::path directory, effectPath, texturePath;
    codec::Bytes savedEffects,savedTextures;
    unsigned generation=0;
    Json textureMetadata=Json::array();
    Json missingTextures=Json::array();
    std::map<std::string,codec::Bytes> previewTextures;
    std::map<codec::Bytes,Json> imageCache;
    codec::ResourceFile effectArchive(const State& state,unsigned version=0) const;
    void publish(const State& state,const std::map<std::string,codec::Bytes>* overrides=nullptr);
    Json state() const;
    Json dispatch(const Json& request);
public:
    DocumentService();
    ~DocumentService();
    Json handle(const Json& request);
};
}
