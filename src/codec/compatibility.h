#pragma once
#include "records.h"
namespace breff::codec {
// The retained model is never narrowed by choosing an output version.
struct EffectProjection {
    Json value;
    std::vector<size_t> animationIndices;
};
EffectProjection projectEffect(const Json& retained, unsigned version);
Json mergeEffectEdit(const Json& retained, const Json& edited, unsigned version);
}
