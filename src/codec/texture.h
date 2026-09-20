#pragma once
#include "binary.h"
namespace breff::codec {
struct TextureImage {
    unsigned width, height, format;
    Bytes rgba;
};
TextureImage decodeTexture(std::span<const uint8_t> record);
Bytes encodeTexture(unsigned width, unsigned height, std::span<const uint8_t> rgba);
}
