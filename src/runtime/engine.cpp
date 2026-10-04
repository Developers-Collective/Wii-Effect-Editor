#include "engine.h"
#include <nw4r/ef/ef_memorymanagerimpl.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <bit>
#include <nw4r/ef/ef_particle.h>
#include <nw4r/ef/ef_emitter.h>

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

    void Engine::setView(const nw4r::ef::DrawInfo& info) {
        if (!state)
            return;
        state->system.mProcessCameraMtx = *info.GetViewMtx();
        nw4r::math::MTX34 inverse;
        nw4r::math::MTX34Inv(&inverse, info.GetViewMtx());
        state->system.mProcessCameraPos = nw4r::math::VEC3(inverse._03, inverse._13, inverse._23);
    }

    void Engine::draw(const nw4r::ef::DrawInfo& info) {
        if (!state)
            return;
        setView(info);
        state->system.Draw(info, 0);
    }

    unsigned Engine::particles() const {
        return state ? state->memory.GetNumActiveParticle() : 0;
    }

    bool Engine::finished() const {
        return state && state->memory.GetNumActiveEffect() == 0;
    }

    void Engine::Bounds::include(const Bounds& other) {
        if (other.empty)
            return;
        if (empty) {
            *this = other;
            return;
        }
        for (unsigned axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], other.minimum[axis]);
            maximum[axis] = std::max(maximum[axis], other.maximum[axis]);
        }
    }

    Engine::Bounds Engine::bounds() const {
        Bounds result;
        if (!state)
            return result;
        using namespace nw4r;
        auto& effects = state->system.mActivityList[0].mActiveList;
        for (auto* effect = static_cast<ef::Effect*>(ut::List_GetNext(&effects, nullptr)); effect;
             effect = static_cast<ef::Effect*>(ut::List_GetNext(&effects, effect))) {
            for (unsigned i = 0; i < effect->GetNumEmitter(); ++i) {
                auto* emitter = effect->GetEmitter(i);
                for (unsigned j = 0; j < emitter->GetNumParticleManager(); ++j) {
                    auto* manager = emitter->GetParticleManager(j);
                    if (manager->mManagerEM->mParameter.mComFlags & ef::EmitterDesc::CMN_FLAG_DISABLE_DRAW)
                        continue;
                    const auto& setting = *manager->mResource->GetEmitterDrawSetting();
                    if (setting.mFlags & ef::EmitterDrawSetting::FLAG_HIDDEN)
                        continue;
                    math::MTX34 transform;
                    manager->CalcGlobalMtx(&transform);
                    // The Frobenius norm bounds expansion under scale, rotation and shear.
                    float scale = 0;
                    for (unsigned row = 0; row < 3; ++row)
                        for (unsigned column = 0; column < 3; ++column)
                            scale += transform.m[row][column] * transform.m[row][column];
                    scale = std::sqrt(scale);
                    auto& particles = manager->mActivityList.mActiveList;
                    for (auto* particle = static_cast<ef::Particle*>(ut::List_GetNext(&particles, nullptr)); particle;
                         particle = static_cast<ef::Particle*>(ut::List_GetNext(&particles, particle))) {
                        math::VEC3 center;
                        math::VEC3Transform(&center, &transform, &particle->mParameter.mPosition);
                        const float x = std::abs(particle->Draw_GetSizeX()) * (1 + std::abs(setting.pivotX) * .01f);
                        const float y = std::abs(particle->Draw_GetSizeY()) * (1 + std::abs(setting.pivotY) * .01f);
                        const float radius = std::hypot(x, y) * scale;
                        if (!std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y) ||
                            !std::isfinite(center.z))
                            continue;
                        result.include({{center.x - radius, center.y - radius, center.z - radius},
                                        {center.x + radius, center.y + radius, center.z + radius},
                                        false});
                    }
                }
            }
        }
        return result;
    }

    uint64_t Engine::visualSignature() const {
        uint64_t hash = 0xCBF29CE484222325;
        auto add = [&](uint64_t value) {
            hash ^= value;
            hash *= 0x100000001B3;
        };
        auto number = [&](float value) {
            add(std::bit_cast<uint32_t>(value));
        };
        auto vector = [&](const nw4r::math::VEC3& value) {
            number(value.x);
            number(value.y);
            number(value.z);
        };
        if (!state)
            return hash;
        using namespace nw4r;
        auto& effects = state->system.mActivityList[0].mActiveList;
        for (auto* effect = static_cast<ef::Effect*>(ut::List_GetNext(&effects, nullptr)); effect;
             effect = static_cast<ef::Effect*>(ut::List_GetNext(&effects, effect))) {
            for (unsigned i = 0; i < effect->GetNumEmitter(); ++i) {
                auto* emitter = effect->GetEmitter(i);
                for (unsigned j = 0; j < emitter->GetNumParticleManager(); ++j) {
                    auto* manager = emitter->GetParticleManager(j);
                    add(reinterpret_cast<uintptr_t>(manager->mResource));
                    math::MTX34 transform;
                    manager->CalcGlobalMtx(&transform);
                    for (const auto& row : transform.m)
                        for (float value : row)
                            number(value);
                    auto& particles = manager->mActivityList.mActiveList;
                    for (auto* particle = static_cast<ef::Particle*>(ut::List_GetNext(&particles, nullptr)); particle;
                         particle = static_cast<ef::Particle*>(ut::List_GetNext(&particles, particle))) {
                        const auto& parameter = particle->mParameter;
                        vector(parameter.mPosition);
                        vector(parameter.mPrevPosition);
                        vector(parameter.mVelocity);
                        math::VEC3 rotation;
                        particle->Draw_GetRotate(&rotation);
                        vector(rotation);
                        number(particle->Draw_GetSizeX());
                        number(particle->Draw_GetSizeY());
                        for (unsigned layer = 0; layer < 2; ++layer) {
                            GXColor primary, secondary;
                            particle->Draw_GetColor(layer, &primary, &secondary);
                            add(std::bit_cast<uint32_t>(primary));
                            add(std::bit_cast<uint32_t>(secondary));
                        }
                        for (unsigned layer = 0; layer < 3; ++layer) {
                            add(reinterpret_cast<uintptr_t>(parameter.mTexture[layer]));
                            number(parameter.mTextureScale[layer].x);
                            number(parameter.mTextureScale[layer].y);
                            number(parameter.mTextureTranslate[layer].x);
                            number(parameter.mTextureTranslate[layer].y);
                            number(parameter.mTextureRotate[layer]);
                        }
                        add(parameter.mTextureWrap);
                        add(parameter.mTextureReverse);
                        add(parameter.mACmpRef0);
                        add(parameter.mACmpRef1);
                    }
                    add(0xFFFFFFFFFFFFFFFF);
                }
            }
        }
        return hash;
    }
}
