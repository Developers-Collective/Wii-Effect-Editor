#pragma once
#include "records.h"
namespace breff::codec {
Json decodeEffect(std::span<const uint8_t> bytes, unsigned version, bool includeUnboundTextures = false);
Bytes encodeEffect(const Json& value, unsigned version);
Bytes encodeEffectPreserving(const Json& value, unsigned version, std::span<const uint8_t> original);
}
