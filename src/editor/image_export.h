#pragma once

#include "../codec/texture.h"
#include <string>
#include <filesystem>

namespace breff {
    codec::TextureImage loadImageFile(const std::filesystem::path& path, unsigned maxDimension = 8192);
    codec::Bytes encodeImageFile(const codec::TextureImage& image, const std::string& extension);
}
