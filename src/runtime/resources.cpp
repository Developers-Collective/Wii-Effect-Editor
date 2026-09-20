#include <nw4r/ef/ef_resource.h>

namespace nw4r::ef {
Resource Resource::mResource;
Resource* Resource::GetInstance() {
    return &mResource;
}
Resource::Resource() {
    Initialize();
}
void Resource::Initialize() {
    ut::List_Init(&mBREFFList, offsetof(EffectProject, projectlink));
    ut::List_Init(&mBREFTList, offsetof(TextureProject, projectlink));
    nativeEmitters.clear();
    nativeTextures.clear();
    mNumEmitter = mNumTexture = 0;
}
EmitterResource* Resource::_FindEmitter(const char* name, EffectProject*) const {
    auto i = nativeEmitters.find(name ? name : "");
    return i == nativeEmitters.end() ? nullptr : i->second;
}
TextureData* Resource::_FindTexture(const char* name, TextureProject*) const {
    auto i = nativeTextures.find(name ? name : "");
    return i == nativeTextures.end() ? nullptr : i->second;
}
ResEmitter Resource::FindEmitter(const char* name, EffectProject* project) const {
    return ResEmitter(_FindEmitter(name, project));
}
ResTexture Resource::FindTexture(const char* name, TextureProject* project) const {
    return ResTexture(_FindTexture(name, project));
}
u32 Resource::NumEmitter(EffectProject*) const {
    return nativeEmitters.size();
}
EmitterResource* Resource::_GetEmitterIndexOf(u32 index, EffectProject*) const {
    if (index >= nativeEmitters.size())
        return nullptr;
    auto it = nativeEmitters.begin();
    std::advance(it, index);
    return it->second;
}
}
