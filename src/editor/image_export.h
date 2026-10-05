#pragma once

#include "../codec/texture.h"
#include <string>

namespace breff {
    codec::Bytes encodeImageFile(const codec::TextureImage& image, const std::string& extension);
}
