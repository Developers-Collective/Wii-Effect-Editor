#pragma once
#include "binary.h"
namespace breff::codec {
bool supportsV7Animation(unsigned family, unsigned kind);
// Adapt disk records, keeping the public JSON target names version independent.
Bytes expandV7Emitter(std::span<const uint8_t> bytes);
Bytes expandV7Animation(std::span<const uint8_t> bytes);
Bytes packV7Emitter(std::span<const uint8_t> bytes);
Bytes packV7Animation(std::span<const uint8_t> bytes);
}
