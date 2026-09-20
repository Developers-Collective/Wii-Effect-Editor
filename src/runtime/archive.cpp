#include "archive.h"
#include <bit>
#include <fstream>
#include <span>
#include <stdexcept>

namespace breff {
namespace {
struct Reader {
    std::span<const uint8_t> data;
    void check(size_t p, size_t n) const {
        if (p > data.size() || n > data.size() - p)
            throw std::runtime_error("Truncated effect resource");
    }
    uint8_t u8(size_t p) const {
        check(p, 1);
        return data[p];
    }
    uint16_t u16(size_t p) const {
        check(p, 2);
        return uint16_t(data[p]) << 8 | data[p + 1];
    }
    uint32_t u32(size_t p) const {
        return uint32_t(u16(p)) << 16 | u16(p + 2);
    }
    float f32(size_t p) const {
        return std::bit_cast<float>(u32(p));
    }
    void copy(void* dest, size_t p, size_t n) const {
        check(p, n);
        std::memcpy(dest, data.data() + p, n);
    }
    std::string name(size_t& p) const {
        auto size = u16(p);
        p += 2;
        check(p, size);
        if (!size || data[p + size - 1])
            throw std::runtime_error("Invalid resource name");
        std::string result(reinterpret_cast<const char*>(data.data() + p), size - 1);
        p += size;
        return result;
    }
    nw4r::math::VEC2 vec2(size_t p) const {
        return {f32(p), f32(p + 4)};
    }
    nw4r::math::VEC3 vec3(size_t p) const {
        return {f32(p), f32(p + 4), f32(p + 8)};
    }
};
std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot read resource file");
    const auto size = file.tellg();
    if (size < 0 || size > 256 * 1024 * 1024)
        throw std::runtime_error("Resource file exceeds 256 MiB");
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        throw std::runtime_error("Cannot read complete resource file");
    return data;
}
template <class F> void entries(const std::vector<uint8_t>& data, uint32_t magic, F visit) {
    Reader r{data};
    if (r.u32(0) != magic || r.u16(4) != 0xfeff || r.u16(6) != 11 || r.u32(8) != data.size() || r.u16(12) != 16 ||
        r.u16(14) != 1 || r.u32(16) != magic)
        throw std::runtime_error("Expected a v11 resource archive");
    const size_t table = 24 + size_t(r.u32(24)), end = table + size_t(r.u32(table));
    r.check(table, r.u32(table));
    size_t p = table + 8;
    for (unsigned i = 0; i < r.u16(table + 4); ++i) {
        auto name = r.name(p);
        const size_t offset = table + size_t(r.u32(p));
        auto size = r.u32(p + 4);
        p += 8;
        if (p > end)
            throw std::runtime_error("Resource names exceed table");
        r.check(offset, size);
        visit(name, r, offset, size);
    }
}
void decodeEmitter(Reader r, nw4r::ef::EmitterDesc& e) {
    using namespace nw4r::ef;
    r.check(0, 0x14c);
    e.commonFlag = r.u32(0);
    e.emitFlag = r.u32(4);
    e.emitLife = r.u16(8);
    e.ptclLife = r.u16(10);
    e.ptclLifeRandom = r.u8(12);
    e.inheritChildPtclTranslate = r.u8(13);
    e.emitEmitIntarvalRandom = r.u8(14);
    e.emitEmitRandom = r.u8(15);
    e.emitEmit = r.f32(16);
    e.emitEmitStart = r.u16(20);
    e.emitEmitPast = r.u16(22);
    e.emitEmitInterval = r.u16(24);
    e.inheritPtclTranslate = r.u8(26);
    e.inheritChildEmitTranslate = r.u8(27);
    for (int i = 0; i < 6; ++i)
        e.commonParam[i] = r.f32(28 + 4 * i);
    e.emitEmitDiv = r.u16(52);
    e.velInitVelocityRandom = r.u8(54);
    e.velMomentumRandom = r.u8(55);
    e.velPowerRadiationDir = r.f32(56);
    e.velPowerYAxis = r.f32(60);
    e.velPowerRandomDir = r.f32(64);
    e.velPowerNormalDir = r.f32(68);
    e.velDiffusionEmitterNormal = r.f32(72);
    e.velPowerSpecDir = r.f32(76);
    e.velDiffusionSpecDir = r.f32(80);
    e.velSpecDir = r.vec3(84);
    e.scale = r.vec3(96);
    e.rotate = r.vec3(108);
    e.translate = r.vec3(120);
    e.lodNear = r.u8(132);
    e.lodFar = r.u8(133);
    e.lodMinEmit = r.u8(134);
    e.lodAlpha = r.u8(135);
    e.randomSeed = r.u32(136);
    r.copy(e.userdata, 140, 8);
    Reader d{r.data.subspan(148)};
    auto& s = e.drawSetting;
    s.mFlags = d.u16(0);
    s.mACmpComp0 = d.u8(2);
    s.mACmpComp1 = d.u8(3);
    s.mACmpOp = d.u8(4);
    s.mNumTevs = d.u8(5);
    s.mFlagClamp = d.u8(6);
    s.mIndirectTargetStage = d.u8(7);
    if (s.mNumTevs > 4)
        throw std::runtime_error("Effect uses more than four TEV stages");
    d.copy(s.mTevTexture, 8, 4);
    d.copy(s.mTevColor, 12, 16);
    d.copy(s.mTevColorOp, 28, 20);
    d.copy(s.mTevAlpha, 48, 16);
    d.copy(s.mTevAlphaOp, 64, 20);
    d.copy(s.mTevKColorSel, 84, 4);
    d.copy(s.mTevKAlphaSel, 88, 4);
    d.copy(&s.mBlendMode, 92, 4);
    d.copy(&s.mColorInput, 96, 8);
    d.copy(&s.mAlphaInput, 104, 8);
    s.mZCompareFunc = d.u8(112);
    s.mAlphaFlickType = d.u8(113);
    s.mAlphaFlickCycle = d.u16(114);
    s.mAlphaFlickRandom = d.u8(116);
    s.mAlphaFlickAmplitude = d.u8(117);
    s.mLighting.mMode = d.u8(118);
    s.mLighting.mType = d.u8(119);
    d.copy(&s.mLighting.mAmbient, 120, 4);
    d.copy(&s.mLighting.mDiffuse, 124, 4);
    s.mLighting.mRadius = d.f32(128);
    s.mLighting.mPosition = d.vec3(132);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col)
            s.mIndTexOffsetMtx[row][col] = d.f32(144 + (row * 3 + col) * 4);
    s.mIndTexScaleExp = d.u8(168);
    s.pivotX = d.u8(169);
    s.pivotY = d.u8(170);
    s.ptcltype = d.u8(172);
    s.typeOption = d.u8(173);
    s.typeDir = d.u8(174);
    s.typeAxis = d.u8(175);
    s.typeOption0 = d.u8(176);
    s.typeOption1 = d.u8(177);
    s.typeOption2 = d.u8(178);
    s.zOffset = d.f32(180);
}
void decodeEffect(Reader r, EffectResource& e) {
    const auto emitterSize = r.u32(4);
    if (emitterSize != 0x14c)
        throw std::runtime_error("Unsupported v11 emitter size");
    decodeEmitter({r.data.subspan(8, emitterSize)}, e.resource.emitter);
    size_t particleOffset = 8 + emitterSize, particleSize = r.u32(particleOffset);
    r.check(particleOffset + 4, particleSize);
    Reader p{r.data.subspan(particleOffset + 4, particleSize)};
    auto& out = e.particle;
    p.copy(out.mColor, 0, 16);
    out.size = p.vec2(16);
    out.scale = p.vec2(24);
    out.rotate = p.vec3(32);
    for (int i = 0; i < 3; ++i) {
        out.textureScale[i] = p.vec2(44 + 8 * i);
        out.textureRotate[i] = p.f32(68 + 4 * i);
        out.textureTranslate[i] = p.vec2(80 + 8 * i);
        out.rotateOffsetRandom[i] = p.u8(121 + i);
        out.rotateOffset[i] = p.f32(124 + 4 * i);
    }
    out.textureWrap = p.u16(116);
    out.textureReverse = p.u8(118);
    out.mACmpRef0 = p.u8(119);
    out.mACmpRef1 = p.u8(120);
    size_t nameOffset = 136;
    for (auto& name : e.textures)
        name = p.name(nameOffset);
    e.resource.particle = &e.particle;
    size_t a = particleOffset + 4 + particleSize;
    const auto particleCount = r.u16(a);
    e.resource.particleInitTracks = r.u16(a + 2);
    size_t particleSizes = a + 4 + 4 * particleCount;
    a += 4 + 8 * particleCount;
    const auto emitterCount = r.u16(a);
    e.resource.emitterInitTracks = r.u16(a + 2);
    size_t emitterSizes = a + 4 + 4 * emitterCount;
    a += 4 + 8 * emitterCount;
    if (e.resource.particleInitTracks > particleCount || e.resource.emitterInitTracks > emitterCount)
        throw std::runtime_error("Invalid initialization track count");
    auto tracks = [&](auto& result, unsigned count, size_t sizes) {
        for (unsigned i = 0; i < count; ++i) {
            auto size = r.u32(sizes + i * 4);
            r.check(a, size);
            result.emplace_back(r.data.begin() + a, r.data.begin() + a + size);
            a += size;
        }
    };
    tracks(e.resource.particleTracks, particleCount, particleSizes);
    tracks(e.resource.emitterTracks, emitterCount, emitterSizes);
}
}
void Archive::load(const std::filesystem::path& breff, const std::filesystem::path& breft) {
    Archive candidate;
    auto textureBytes = readFile(breft), effectBytes = readFile(breff);
    entries(textureBytes, 0x52454654, [&](const std::string& name, Reader r, size_t offset, size_t) {
        auto t = std::make_unique<TextureResource>();
        t->name = name;
        Reader b{r.data.subspan(offset)};
        auto& v = t->resource;
        v.name = t->name.data();
        v.width = b.u16(4);
        v.height = b.u16(6);
        v.dataSize = b.u32(8);
        v.format = b.u8(12);
        v.tlutFormat = b.u8(13);
        v.tlutEntries = b.u16(14);
        v.tlutSize = b.u32(16);
        v.mipmap = b.u8(20);
        v.min_filt = b.u8(21);
        v.mag_filt = b.u8(22);
        b.check(32, size_t(v.dataSize) + v.tlutSize);
        t->image.assign(b.data.begin() + 32, b.data.begin() + 32 + v.dataSize);
        t->palette.assign(b.data.begin() + 32 + v.dataSize, b.data.begin() + 32 + v.dataSize + v.tlutSize);
        v.texture = t->image.data();
        v.tlut = t->palette.data();
        if (!candidate.textures.emplace(name, std::move(t)).second)
            throw std::runtime_error("Duplicate texture name");
    });
    entries(effectBytes, 0x52454646, [&](const std::string& name, Reader r, size_t offset, size_t size) {
        auto e = std::make_unique<EffectResource>();
        e->resource.name = name;
        decodeEffect({r.data.subspan(offset, size)}, *e);
        for (int i = 0; i < 3; ++i)
            if (auto t = candidate.textures.find(e->textures[i]); t != candidate.textures.end())
                e->particle.mTexture[i] = &t->second->resource;
        if (!candidate.effects.emplace(name, std::move(e)).second)
            throw std::runtime_error("Duplicate effect name");
    });
    *this = std::move(candidate);
}
}
