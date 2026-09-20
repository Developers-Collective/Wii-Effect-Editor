#include "engine.h"
#include <nw4r/ef/ef_memorymanagerimpl.h>
#include <stdexcept>

namespace breff {
struct Engine::State {
    std::vector<unsigned char> heap;
    nw4r::ef::MemoryManager memory;
    nw4r::ef::EffectSystem system;
    State() : heap(32 * 1024 * 1024), memory(heap.data(), heap.size(), 16, 256, 256, 16384) {
        system.SetMemoryManager(&memory, 1);
        nw4r::math::MTX34Identity(&system.mProcessCameraMtx);
        system.mProcessCameraPos = nw4r::math::VEC3(0, 0, 200);
        system.mProcessCameraNear = .001f;
        system.mProcessCameraFar = 1000000.f;
    }
};
Engine::Engine() = default;
Engine::~Engine() = default;
void Engine::reset() {
    state.reset();
}
void Engine::start(Archive& archive, const std::string& name, std::optional<uint16_t> seed) {
    reset();
    auto* registry = nw4r::ef::Resource::GetInstance();
    registry->Initialize();
    for (auto& [key, effect] : archive.effects)
        registry->Register(key, &effect->resource);
    for (auto& [key, texture] : archive.textures)
        registry->Register(key, &texture->resource);
    auto next = std::make_unique<State>();
    next->system.mPreviewSeed = seed;
    if (!next->system.CreateEffect(name.c_str(), 0, 0))
        throw std::runtime_error("Could not create effect: " + name);
    state = std::move(next);
}
void Engine::step() {
    if (state)
        state->system.Calc(0, false);
}
void Engine::draw(const nw4r::ef::DrawInfo& info) {
    if (!state)
        return;
    state->system.mProcessCameraMtx = *info.GetViewMtx();
    nw4r::math::MTX34 inverse;
    nw4r::math::MTX34Inv(&inverse, info.GetViewMtx());
    state->system.mProcessCameraPos = nw4r::math::VEC3(inverse._03, inverse._13, inverse._23);
    state->system.Draw(info, 0);
}
unsigned Engine::particles() const {
    return state ? state->memory.GetNumActiveParticle() : 0;
}
bool Engine::finished() const {
    return state && state->memory.GetNumActiveEffect() == 0;
}
}
