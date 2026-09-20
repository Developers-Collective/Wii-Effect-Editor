#ifndef NW4R_EF_RES_EMITTER_H
#define NW4R_EF_RES_EMITTER_H
#include <nw4r/types_nw4r.h>

#include <nw4r/ef/ef_res_animcurve.h>
#include <nw4r/ef/ef_types.h>
#include <nw4r/ef/emform/ef_emform.h>
#include <nw4r/math.h>

#include <revolution/GX.h>
#include <string>
#include <vector>

namespace nw4r {
namespace ef {

// Forward declarations
struct ParticleParameterDesc;

struct TevStageColor {
    u8 mA; // at 0x0
    u8 mB; // at 0x1
    u8 mC; // at 0x2
    u8 mD; // at 0x3
};

struct TevStageColorOp {
    u8 mOp;     // at 0x0
    u8 mBias;   // at 0x1
    u8 mScale;  // at 0x2
    u8 mClamp;  // at 0x3
    u8 mOutReg; // at 0x4
};

struct TevStageAlpha {
    u8 mA; // at 0x0
    u8 mB; // at 0x1
    u8 mC; // at 0x2
    u8 mD; // at 0x3
};

struct BlendMode {
    u8 mType;      // at 0x0
    u8 mSrcFactor; // at 0x1
    u8 mDstFactor; // at 0x2
    u8 mOp;        // at 0x3
};

struct ColorInput {
    enum TevColor { TEVCOLOR_NULL, TEVCOLOR1_1, TEVCOLOR1_2, TEVCOLOR2_3, TEVCOLOR2_2, TEVCOLOR1_MULT, TEVCOLOR2_MULT };

    enum RasColor {
        RASCOLOR_NULL,
        RASCOLOR_LIGHTING,
    };

    u8 mRasColor;                    // at 0x0
    u8 mTevColor[GX_MAX_TEVREG - 1]; // at 0x1
    u8 mTevKColor[GX_MAX_KCOLOR];    // at 0x4
};

// TODO(kiwi) Why?
#pragma pack(push, 1)
struct Lighting {
    enum Mode { LIGHTING_MODE_OFF, LIGHTING_MODE_SIMPLE, LIGHTING_MODE_HARDWARE };

    enum Type { LIGHTING_TYPE_NONE, LIGHTING_TYPE_AMBIENT, LIGHTING_TYPE_POINT };

    u8 mMode;             // at 0x0
    u8 mType;             // at 0x1
    GXColor mAmbient;     // at 0x2
    GXColor mDiffuse;     // at 0x6
    f32 mRadius;          // at 0xA
    math::VEC3 mPosition; // at 0xE
};
#pragma pack(pop)

struct EmitterDrawSetting {
    enum Flag {
        FLAG_ZCOMP_ENABLE_TEST = (1 << 0),
        FLAG_ZCOMP_ENABLE_UPDATE = (1 << 1),
        FLAG_ZCOMP_BEFORE_TEX = (1 << 2),
        FLAG_CLIP_DISABLE = (1 << 3),
        FLAG_TEX1_ENABLE = (1 << 4),
        FLAG_TEX2_ENABLE = (1 << 5),
        FLAG_TEXIND_ENABLE = (1 << 6),
        FLAG_TEX1_PROJ = (1 << 7),
        FLAG_TEX2_PROJ = (1 << 8),
        FLAG_TEXIND_PROJ = (1 << 9),
        FLAG_HIDDEN = (1 << 10),
        FLAG_DRAW_ORDER = (1 << 11),
        FLAG_FOG_ENABLE = (1 << 12),
        FLAG_XY_SAME_SIZE = (1 << 13),
        FLAG_XY_SAME_SCALE = (1 << 14),
    };

    enum AlphaFlickType {
        ALPHAFLICK_NONE,
        ALPHAFLICK_TRIANGLE,
        ALPHAFLICK_SAWTOOTH1, // Inverse sawtooth wave
        ALPHAFLICK_SAWTOOTH2,
        ALPHAFLICK_SQUARE,
        ALPHAFLICK_SINE
    };

    enum Assist {
        // Common
        ASSIST_CMN_NORMAL = 0,
        ASSIST_CMN_CROSS,

        // Billboard
        ASSIST_BB_NORMAL = 0,
        ASSIST_BB_Y,
        ASSIST_BB_DIRECTIONAL,
        ASSIST_BB_NOROLL,

        // Stripe
        ASSIST_ST_NORMAL = 0,
        ASSIST_ST_CROSS,
        ASSIST_ST_BILLBOARD,
        ASSIST_ST_TUBE,
    };

    enum Ahead {
        // Common
        AHEAD_CMN_NODESIGN = 5,

        // Billboard
        AHEAD_BB_SPEED = 0,
        AHEAD_BB_EMITTER_CENTER,
        AHEAD_BB_EMITTER_DESIGN,
        AHEAD_BB_PARTICLE,
        AHEAD_BB_PARTICLE_BOTH,
    };

    enum OptionTypeCommon { TYPE_CMN_NORMAL, TYPE_CMN_CROSS };

    enum DirType {
        DIR_NO_DESIGN = 5,
    };

    u16 mFlags;                     // at 0x0
    u8 mACmpComp0;                  // at 0x2
    u8 mACmpComp1;                  // at 0x3
    u8 mACmpOp;                     // at 0x4
    u8 mNumTevs;                    // at 0x5
    u8 mFlagClamp;                  // at 0x6
    u8 mIndirectTargetStage;        // at 0x7
    u8 mTevTexture[4];              // v11 stage texture selection
    TevStageColor mTevColor[4];     // at 0x8
    TevStageColorOp mTevColorOp[4]; // at 0x18
    TevStageAlpha mTevAlpha[4];     // at 0x2C
    TevStageColorOp mTevAlphaOp[4]; // at 0x3C
    u8 mTevKColorSel[4];            // at 0x50
    u8 mTevKAlphaSel[4];            // at 0x54
    BlendMode mBlendMode;           // at 0x58
    ColorInput mColorInput;         // at 0x5C
    ColorInput mAlphaInput;         // v11 separates color and alpha sources
    u8 mZCompareFunc;               // at 0x64
    u8 mAlphaFlickType;             // at 0x65
    u16 mAlphaFlickCycle;           // at 0x66
    u8 mAlphaFlickRandom;           // at 0x68
    u8 mAlphaFlickAmplitude;        // at 0x69
    Lighting mLighting;             // at 0x6A
    f32 mIndTexOffsetMtx[2][3];     // at 0x84
    s8 mIndTexScaleExp;             // at 0x9C
    s8 pivotX;                      // at 0x9D
    s8 pivotY;                      // at 0x9E
    u8 PADDING_0xA3;                // at 0x9F
    u8 ptcltype;                    // at 0xA0
    u8 typeOption;                  // at 0xA1
    u8 typeDir;                     // at 0xA2
    u8 typeAxis;                    // at 0xA3
    u8 typeOption0;                 // at 0xA4
    u8 typeOption1;                 // at 0xA5
    u8 typeOption2;                 // at 0xA6
    u8 PADDING_0xAB;                // at 0xA7
    f32 zOffset;                    // at 0xA8
};

struct EmitterDesc {
    enum CommonFlag {
        CMN_FLAG_SYNC_LIFE = (1 << 0),
        CMN_FLAG_DISABLE_DRAW = (1 << 1),
        CMN_FLAG_MAX_LIFE = (1 << 2),

        CMN_FLAG_INHERIT_PTCL_SCALE = (1 << 5),
        CMN_FLAG_INHERIT_PTCL_ROT = (1 << 6),

        CMN_FLAG_EMIT_INHERIT_SCALE = (1 << 7),
        CMN_FLAG_EMIT_INHERIT_ROT = (1 << 8),

        CMN_FLAG_DISABLE_CALC = (1 << 9),

        CMN_FLAG_RELOCATED = (1 << 30),
    };

    enum EmitFlag {
        EMIT_FLAG_8 = (1 << 8),
        EMIT_FLAG_15 = (1 << 15),
        EMIT_FLAG_16 = (1 << 16),
        EMIT_FLAG_17 = (1 << 17),
        EMIT_FLAG_18 = (1 << 18),
        EMIT_FLAG_24 = (1 << 24),
        EMIT_FLAG_XYZ_SAME_SIZE = (1 << 25),
        EMIT_FLAG_26 = (1 << 26),
    };

    u32 commonFlag;                 // at 0x0
    u32 emitFlag;                   // at 0x4
    u16 emitLife;                   // at 0x8
    u16 ptclLife;                   // at 0xA
    s8 ptclLifeRandom;              // at 0xC
    s8 inheritChildPtclTranslate;   // at 0xD
    s8 emitEmitIntarvalRandom;      // at 0xE
    s8 emitEmitRandom;              // at 0xF
    f32 emitEmit;                   // at 0x10
    u16 emitEmitStart;              // at 0x14
    u16 emitEmitPast;               // at 0x16
    u16 emitEmitInterval;           // at 0x18
    s8 inheritPtclTranslate;        // at 0x1A
    s8 inheritChildEmitTranslate;   // at 0x1B
    f32 commonParam[NUM_PARAMS];    // at 0x1C
    u16 emitEmitDiv;                // at 0x34
    s8 velInitVelocityRandom;       // at 0x36
    s8 velMomentumRandom;           // at 0x37
    f32 velPowerRadiationDir;       // at 0x38
    f32 velPowerYAxis;              // at 0x3C
    f32 velPowerRandomDir;          // at 0x40
    f32 velPowerNormalDir;          // at 0x44
    f32 velDiffusionEmitterNormal;  // at 0x48
    f32 velPowerSpecDir;            // at 0x4C
    f32 velDiffusionSpecDir;        // at 0x50
    math::VEC3 velSpecDir;          // at 0x54
    math::VEC3 scale;               // at 0x60
    math::VEC3 rotate;              // at 0x6C
    math::VEC3 translate;           // at 0x78
    u8 lodNear;                     // at 0x84
    u8 lodFar;                      // at 0x85
    u8 lodMinEmit;                  // at 0x86
    u8 lodAlpha;                    // at 0x87
    u32 randomSeed;                 // at 0x88
    u8 userdata[8];                 // at 0x8C
    EmitterDrawSetting drawSetting; // at 0x94

    EmitFormType GetFormType() {
        return static_cast<EmitFormType>(emitFlag & 0xFF);
    }
};

// Native resources own host pointers; on-disk offsets are decoded separately.
struct EmitterResource {
    std::string name;
    EmitterDesc emitter{};
    ParticleParameterDesc* particle = nullptr;
    std::vector<std::vector<u8>> particleTracks, emitterTracks;
    u16 particleInitTracks = 0, emitterInitTracks = 0;
    EmitterDesc* GetEmitterDesc() {
        return &emitter;
    }
    EmitterDrawSetting* GetEmitterDrawSetting() {
        return &emitter.drawSetting;
    }
    const char* GetName() const {
        return name.c_str();
    }
    ParticleParameterDesc* GetParticleParameterDesc() {
        return particle;
    }
    u16 NumPtclTrack() {
        return static_cast<u16>(particleTracks.size());
    }
    u16 NumPtclInitTrack() {
        return particleInitTracks;
    }
    u16 NumEmitTrack() {
        return static_cast<u16>(emitterTracks.size());
    }
    u16 NumEmitInitTrack() {
        return emitterInitTracks;
    }
    u8* GetPtclTrack(u16 i) {
        return particleTracks.at(i).data();
    }
    u8* GetEmitTrack(u16 i) {
        return emitterTracks.at(i).data();
    }
};

class ResEmitter {
  private:
    EmitterResource* mData; // at 0x0

  public:
    explicit ResEmitter(EmitterResource* pData = NULL) : mData(pData) {
    }
    ResEmitter(const ResEmitter& rOther) : mData(rOther.mData) {
    }

    bool IsValid() const {
        return mData != NULL;
    }

    EmitterResource* ptr() {
        return mData;
    }

    ResAnimCurve GetPtclTrack(u16 idx);
};

} // namespace ef
} // namespace nw4r

#endif
