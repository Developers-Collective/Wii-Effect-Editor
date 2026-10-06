#pragma once
#include "../codec/resource_file.h"
#include "../codec/records.h"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace breff {
    class DocumentService {
        using Json = codec::Json;

        struct State {
            codec::ResourceFile effects, textures;
            Json values = Json::object(), originalValues = Json::object(), errors = Json::object();
            std::string selected;
            bool loaded = false;
            unsigned version = 11;
        } document;

        std::vector<State> undo, redo;
        std::filesystem::path directory, effectPath, texturePath;
        codec::Bytes savedEffects, savedTextures;
        mutable std::optional<bool> contentDirty;
        bool savedEffectCompression = false, savedTextureCompression = false;
        codec::Bytes textureImportBytes;
        uint64_t textureImportToken = 0;
        unsigned generation = 0;
        unsigned originalVersion = 11, originalTextureVersion = 11;
        Json textureMetadata = Json::array(), externalTextureMetadata = Json::array();
        Json missingTextures = Json::array();
        Json unusedTextures = Json::array();
        Json conversionWarnings = Json::object();
        std::map<std::string, codec::Bytes> previewTextures;
        std::map<std::string, std::string> previewTexturePaths;
        std::map<codec::Bytes, Json> imageCache;
        codec::ResourceFile effectArchive(const State& state, unsigned version = 0) const;
        codec::ResourceFile textureArchive(const State& state) const;
        Json findUnusedTextures(const State& state, const std::set<std::string>& excluded = {}) const;
        void publish(const State& state, const std::map<std::string, codec::Bytes>* overrides = nullptr);
        Json state() const;
        Json dispatch(const Json& request);

      public:
        DocumentService();
        ~DocumentService();
        Json handle(const Json& request);
    };

    // Owns independent documents; all requests are serialized by the UI worker.
    class Workspace {
        using Json = codec::Json;

        struct Tab {
            uint64_t id;
            std::unique_ptr<DocumentService> document;
            Json state;
        };

        std::vector<Tab> tabs;
        uint64_t selected = 0, nextId = 1;
        Json state() const;

      public:
        Json handle(const Json& request);
    };
}
