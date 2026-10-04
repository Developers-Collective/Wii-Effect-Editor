#pragma once
#include "binary.h"

namespace breff::codec {
    Bytes expandV5Emitter(std::span<const uint8_t> bytes);
    Bytes packV5Emitter(std::span<const uint8_t> bytes);
    Bytes expandLegacyParticle(std::span<const uint8_t> bytes);
    Bytes packLegacyParticle(std::span<const uint8_t> bytes);
    Bytes expandV5Animation(std::span<const uint8_t> bytes);
    Bytes packV5Animation(std::span<const uint8_t> bytes);
}
