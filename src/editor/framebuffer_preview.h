#pragma once

#include "preview_view.h"
#include "../codec/texture.h"
#include "../codec/records.h"
#include "../runtime/engine.h"
#include <dolphin/gx.h>
#include <memory>
#include <set>
#include <string>

namespace breff {
    struct FramebufferSettings {
        bool enabled = false;
        bool showBackground = true;
        std::string imagePath;
        std::shared_ptr<const codec::TextureImage> image;
        std::set<std::string> names;

        bool active() const {
            return enabled && image && !names.empty();
        }
    };

    std::set<std::string> referencedTextureNames(const codec::Json& state);

    class FramebufferPreview {
        std::shared_ptr<const codec::TextureImage> image;
        codec::Bytes pixels;
        GXTexObj texture{};
        alignas(32) std::array<uint8_t, 32> captureAddress{};
        unsigned width = 0, height = 0;

        void prepareImage(const FramebufferSettings& settings);
        void drawImage(const PreviewRect& rect, const Mtx44 projection, float depth);

      public:
        FramebufferPreview() = default;
        FramebufferPreview(const FramebufferPreview&) = delete;
        FramebufferPreview& operator=(const FramebufferPreview&) = delete;
        ~FramebufferPreview();

        void reset();
        void capture(const FramebufferSettings& settings, unsigned width, unsigned height);
        void background(const FramebufferSettings& settings, const PreviewRect& rect,
                        const Mtx44 projection, float depth);
        void draw(Engine& engine, Archive& archive, const FramebufferSettings& settings,
                  nw4r::ef::DrawInfo info, const Mtx44 projection);
    };
}
