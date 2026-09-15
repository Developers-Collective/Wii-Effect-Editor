#pragma once
#include <nw4r/ef.h>
#include <span>
namespace breff {
struct FieldContext {
    const nw4r::math::MTX34 &localToWorld,&worldToLocal,&emitterToWorld,&localToEmitter,&emitterToLocal;
    nw4r::math::VEC3 position,velocity,movement;
};
void applyField(std::span<const uint8_t> track,nw4r::ef::Particle& particle,
                uint32_t tick,uint16_t seed,uint32_t life,const FieldContext& context,
                nw4r::math::VEC3& velocity,nw4r::math::VEC3& position);
}
