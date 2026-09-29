#pragma once
#include "binary.h"

namespace breff::codec {
    enum class TextureFormat : unsigned {
        GX_I4 = 0,
        GX_I8 = 1,
        GX_IA4 = 2,
        GX_IA8 = 3,
        GX_RGB565 = 4,
        GX_RGB5A3 = 5,
        GX_RGBA8 = 6,
        GX_C4 = 8,
        GX_C8 = 9,
        GX_C14X2 = 10,
        GX_CMPR = 14
    };
    enum class PaletteFormat : unsigned { GX_TL_IA8 = 0, GX_TL_RGB565 = 1, GX_TL_RGB5A3 = 2 };

    struct TextureImage {
        unsigned width, height;
        TextureFormat format;
        Bytes rgba;
    };

    TextureImage decodeTexture(std::span<const uint8_t> record);
    Bytes encodeTexture(unsigned width, unsigned height, std::span<const uint8_t> rgba,
                        TextureFormat format = TextureFormat::GX_RGBA8,
                        PaletteFormat paletteFormat = PaletteFormat::GX_TL_RGB5A3);
}
