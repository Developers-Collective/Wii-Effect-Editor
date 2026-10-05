#include "framebuffer_preview.h"
#include <aurora/gfx.hpp>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXExtra.h>
#include <dolphin/mtx.h>
#include <algorithm>
#include <cmath>

namespace breff {
    std::set<std::string> referencedTextureNames(const codec::Json& state) {
        std::set<std::string> names;
        for (const auto* key : {"textures", "externalTextures"}) {
            if (!state.contains(key))
                continue;
            for (const auto& texture : state.at(key)) {
                if (!texture.value("dependencies", codec::Json::array()).empty())
                    names.insert(texture.at("name").get<std::string>());
            }
        }
        names.erase("");
        return names;
    }

    FramebufferPreview::~FramebufferPreview() {
        reset();
    }

    void FramebufferPreview::reset() {
        if (image)
            GXDestroyTexObj(&texture);
        if (width)
            GXDestroyCopyTex(captureAddress.data());
        if (image || width)
            AuroraGXSync();
        image.reset();
        pixels.clear();
        width = height = 0;
    }

    void FramebufferPreview::drawImage(const PreviewRect& rect, const Mtx44 projection, float depth) {
        AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
        GXSetViewportRender(rect.x, rect.y, rect.width, rect.height, 0, 1);
        GXSetScissorRender(unsigned(rect.x), unsigned(rect.y), unsigned(rect.width), unsigned(rect.height));
        const bool perspective = projection[3][3] == 0;
        GXSetProjection(projection, perspective ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC);
        Mtx identity;
        PSMTXIdentity(identity);
        GXLoadPosMtxImm(identity, GX_PNMTX0);
        GXSetCurrentMtx(GX_PNMTX0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetNumChans(0);
        GXSetNumTexGens(1);
        GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GXSetNumTevStages(1);
        GXSetNumIndStages(0);
        GXSetTevDirect(GX_TEVSTAGE0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
        GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
        GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetZCompLoc(GX_TRUE);
        // A backdrop is not scene geometry. Its visibility must not change the
        // effect's depth buffer or hide particles using equal-depth comparisons.
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_TRUE);
        GXSetDstAlpha(GX_FALSE, 0);
        GXSetFog(GX_FOG_NONE, 0, 1, .001f, 1000000.f, GXColor{0, 0, 0, 0});
        GXLoadTexObj(&texture, GX_TEXMAP0);

        const float extent = perspective ? depth : 1.f;
        const float halfWidth = extent / projection[0][0];
        const float halfHeight = extent / projection[1][1];
        const float aspect = rect.width / rect.height;
        const float sourceAspect = float(image->width) / image->height;
        const float spanS = std::min(1.f, aspect / sourceAspect);
        const float spanT = std::min(1.f, sourceAspect / aspect);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        for (const auto& point : std::array<std::array<float, 2>, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
            GXPosition3f32(point[0] * halfWidth, point[1] * halfHeight, -depth);
            GXTexCoord2f32(.5f + point[0] * spanS * .5f, .5f - point[1] * spanT * .5f);
        }
        GXEnd();
    }

    void FramebufferPreview::prepareImage(const FramebufferSettings& settings) {
        if (image != settings.image) {
            reset();
            image = settings.image;
            pixels = image->rgba;
            // Make the source opaque over the ordinary preview background.
            constexpr uint8_t clear[] = {19, 22, 28};
            for (size_t i = 0; i < pixels.size(); i += 4) {
                for (unsigned c = 0; c < 3; ++c)
                    pixels[i + c] = uint8_t((unsigned(pixels[i + c]) * pixels[i + 3] +
                                            unsigned(clear[c]) * (255 - pixels[i + 3]) + 127) / 255);
                pixels[i + 3] = 255;
            }
            GXInitTexObj(&texture, pixels.data(), image->width, image->height, GX_TF_RGBA8_PC,
                         GX_CLAMP, GX_CLAMP, GX_FALSE);
            GXInitTexObjLOD(&texture, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
        }
    }

    void FramebufferPreview::capture(const FramebufferSettings& settings, unsigned w, unsigned h) {
        if (!settings.active())
            return;
        if (!w || !h || w > 8192 || h > 8192)
            throw std::runtime_error("Framebuffer dimensions must be between 1 and 8192 pixels");
        prepareImage(settings);
        if (width && (width != w || height != h))
            GXDestroyCopyTex(captureAddress.data());
        width = w;
        height = h;
        GXCreateFrameBuffer(width, height);
        Mtx44 projection;
        C_MTXOrtho(projection, 1, -1, -1, 1, 0, 1);
        drawImage({0, 0, float(width), float(height)}, projection, .5f);
        GXSetTexCopySrc(0, 0, width, height);
        GXSetTexCopyDst(width, height, GX_TF_RGB565, GX_FALSE);
        GXCopyTex(captureAddress.data(), GX_FALSE);
        GXRestoreFrameBuffer();
    }

    void FramebufferPreview::background(const FramebufferSettings& settings, const PreviewRect& rect,
                                       const Mtx44 projection, float depth) {
        if (settings.image && settings.showBackground) {
            prepareImage(settings);
            drawImage(rect, projection, depth);
        }
    }

    void FramebufferPreview::draw(Engine& engine, Archive& archive, const FramebufferSettings& settings,
                                 nw4r::ef::DrawInfo info, const Mtx44 projection) {
        if (!settings.active()) {
            engine.draw(info);
            return;
        }

        // GX_TG_POS applies the position matrix first, so this matrix takes view-space
        // positions into homogeneous texture coordinates. Texture T grows downward.
        nw4r::math::MTX34 textureProjection;
        for (unsigned column = 0; column < 4; ++column) {
            textureProjection.m[0][column] = .5f * (projection[0][column] + projection[3][column]);
            textureProjection.m[1][column] = .5f * (-projection[1][column] + projection[3][column]);
            textureProjection.m[2][column] = projection[3][column];
        }
        info.SetProjMtx(textureProjection);

        struct Restore {
            std::vector<std::pair<nw4r::ef::TextureData*, nw4r::ef::TextureData>> textures;
            ~Restore() {
                for (const auto& [target, original] : textures)
                    *target = original;
            }
        } restore;
        for (const auto& name : settings.names) {
            const auto found = archive.textures.find(name);
            if (found == archive.textures.end())
                continue;
            auto& resource = found->second->resource;
            restore.textures.emplace_back(&resource, resource);
            resource.width = uint16_t(width);
            resource.height = uint16_t(height);
            resource.texture = captureAddress.data();
            resource.format = GX_TF_RGB565;
            resource.mipmap = 1;
            resource.hasSamplerSettings = true;
            resource.min_filt = GX_LINEAR;
            resource.mag_filt = GX_LINEAR;
            resource.lod_bias = 0;
            resource.tlut = nullptr;
            resource.tlutEntries = 0;
        }
        engine.draw(info);
    }
}
