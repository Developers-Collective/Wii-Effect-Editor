#pragma once
#include "fields.h"
#include <array>
namespace breff {
struct PostField {
    std::span<const uint8_t> track;
    std::array<float,9> transform{};
    void evaluate(std::span<const uint8_t>,uint32_t,uint16_t,uint32_t);
    // Integrates movement, collisions and wrapping. False retires the particle.
    bool apply(nw4r::ef::Particle&,const FieldContext&,nw4r::math::VEC3 velocity,nw4r::math::VEC3 displacement) const;
};
}
