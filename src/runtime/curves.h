#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <array>
#include <vector>
namespace breff {
struct TextureSelection { uint8_t wrap,reverse; uint16_t name; };
std::string curveName(std::span<const uint8_t> curve,uint16_t index);
TextureSelection evaluateTexture(std::span<const uint8_t> curve,
                                 uint32_t tick,uint16_t seed,uint32_t life);
std::vector<std::array<uint8_t,12>> evaluateChild(std::span<const uint8_t> curve,
                                                uint32_t tick,uint16_t seed,uint32_t life);
// Curves stay in their serialized big-endian representation.
void evaluateF32(std::span<const uint8_t> curve, std::span<float> output,
                 uint32_t tick, uint16_t seed, uint32_t life);
void evaluateU8(std::span<const uint8_t> curve, std::span<uint8_t> output,
                uint32_t tick, uint16_t seed, uint32_t life);
void evaluateRotate(std::span<const uint8_t> curve, std::span<float> output,
                    uint32_t tick, uint16_t seed, uint32_t life);
}
