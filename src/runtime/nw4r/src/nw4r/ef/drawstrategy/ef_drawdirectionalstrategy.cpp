#include <nw4r/ef.h>
#include <cmath>

namespace nw4r::ef {
namespace {
math::VEC3 column(const math::MTX34& m, int n) {
    return {m.m[0][n], m.m[1][n], m.m[2][n]};
}
math::MTX34 rotation(Particle* p, u8 axis) {
    math::VEC3 r;
    p->Draw_GetRotate(&r);
    if (axis < 3) {
        if (axis != 0)
            r.x = 0;
        if (axis != 1)
            r.y = 0;
        if (axis != 2)
            r.z = 0;
    }
    float sx, cx, sy, cy, sz, cz;
    math::SinCosRad(&sx, &cx, r.x);
    math::SinCosRad(&sy, &cy, r.y);
    math::SinCosRad(&sz, &cz, r.z);
    return {cy * cz,
            sx * sy * cz - cx * sz,
            cx * sy * cz + sx * sz,
            0,
            cy * sz,
            sx * sy * sz + cx * cz,
            cx * sy * sz - sx * cz,
            0,
            -sy,
            cy * sx,
            cx * cy,
            0};
}
math::MTX34 localTransform(Particle* p, const EmitterDrawSetting& s, float sx, float sy) {
    const auto r = rotation(p, s.typeAxis);
    const float px = s.pivotX * .01f, py = s.pivotY * .01f;
    float stretch = 1;
    if (s.typeOption0) {
        math::VEC3 move;
        p->GetMoveDir(&move);
        stretch = math::VEC3Len(&move) * .5f / sy + 1;
    }
    math::MTX34 result;
    for (int i = 0; i < 3; ++i) {
        result.m[i][0] = r.m[i][0] * sx;
        if (s.typeOption1) {
            result.m[i][1] = r.m[i][2] * sy;
            result.m[i][2] = r.m[i][1] * sx;
            result.m[i][3] = -r.m[i][0] * (sx * px) - r.m[i][2] * (sy * py);
        } else {
            result.m[i][1] = r.m[i][1] * (sy * stretch);
            result.m[i][2] = r.m[i][2] * sx;
            result.m[i][3] = -r.m[i][0] * (sx * px) - r.m[i][1] * (sy * ((py + stretch) - 1));
        }
    }
    result._03 += px;
    if (s.typeOption1)
        result._23 -= py;
    else
        result._13 += py;
    return result;
}
void quad(const math::MTX34& m, bool cross, bool texture) {
    static constexpr float points[2][4][3] = {{{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}},
                                              {{0, -1, 1}, {0, 1, 1}, {0, 1, -1}, {0, -1, -1}}};
    static constexpr u8 uv[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; ++i) {
        math::VEC3 v(points[cross][i][0], points[cross][i][1], points[cross][i][2]);
        math::VEC3Transform(&v, &m, &v);
        GXPosition3f32(v.x, v.y, v.z);
        if (texture)
            GXTexCoord2u8(uv[i][0], uv[i][1]);
    }
    GXEnd();
}
}
DrawDirectionalStrategy::DrawDirectionalStrategy() = default;
DrawStrategyImpl::CalcAheadFunc DrawDirectionalStrategy::GetCalcAheadFunc(ParticleManager* manager) {
    switch (manager->mResource->GetEmitterDrawSetting()->typeDir) {
    case 1:
        return CalcAhead_EmitterCenter;
    case 2:
        return CalcAhead_EmitterDesign;
    case 3:
        return CalcAhead_Particle;
    case 5:
    case 7:
        return CalcAhead_NoDesign;
    case 6:
        return CalcAhead_ParticleBoth;
    default:
        return CalcAhead_Speed;
    }
}
void DrawDirectionalStrategy::Draw(const DrawInfo& info, ParticleManager* manager) {
    const auto& s = *manager->mResource->GetEmitterDrawSetting();
    InitTexture(s);
    InitTev(s, info);
    InitColor(manager, s, info);
    GXEnableTexOffsets(GX_TEXCOORD0, TRUE, TRUE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (mNumTexmap)
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GXSetCurrentMtx(GX_PNMTX0);
    const auto& view = *info.GetViewMtx();
    AheadContext context(view, manager);
    math::MTX34 modelView, emitterLocal;
    math::MTX34Mult(&modelView, &view, &context.mCommon.mParticleManagerMtx);
    math::MTX34Mult(&emitterLocal, &context.mCommon.mParticleManagerMtxInv, &context.mCommon.mEmitterMtx);
    const bool billboard = (s.typeOption2 & 3) == 1;
    GXLoadPosMtxImm(billboard ? mIdentityMtx : modelView, GX_PNMTX0);
    const auto& axes = s.typeDir == 7 ? context.mCommon.mParticleManagerMtxInv : emitterLocal;
    auto initial = column(axes, 1), fallback = column(axes, 2);
    Normalize(&fallback);
    if (billboard)
        initial = math::VEC3(0, 0, 1);
    for (auto* p = GetYoungestParticle(manager); p && p->mPrevAxis.x > 1; p = GetElderParticle(manager, p))
        p->mPrevAxis = initial;
    auto ahead = GetCalcAheadFunc(manager);
    auto getFirst = GetGetFirstDrawParticleFunc(s.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);
    auto getNext = GetGetNextDrawParticleFunc(s.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);
    bool first = true;
    for (auto* p = getFirst(manager); p; p = getNext(manager, p)) {
        float sx = p->Draw_GetSizeX(), sy = p->Draw_GetSizeY();
        if (sx < NW4R_MATH_FLT_EPSILON || sy < NW4R_MATH_FLT_EPSILON)
            continue;
        SetupGP(p, s, info, first, false);
        first = false;
        math::VEC3 x, y, z, pos = p->mParameter.mPosition;
        ahead(&y, &context, p);
        if (billboard) {
            math::VEC3TransformNormal(&y, &modelView, &y);
            x = math::VEC3(y.y, -y.x, 0);
            if (!Normalize(&x)) {
                math::VEC3Cross(&x, &y, &p->mPrevAxis);
                if (!Normalize(&x))
                    x = math::VEC3(1, 0, 0);
                math::VEC3Cross(&z, &x, &y);
            } else
                z = math::VEC3(-y.x * y.z, -y.y * y.z, y.x * y.x + y.y * y.y);
            Normalize(&z);
            p->mPrevAxis = z;
            Normalize(&y);
            math::VEC3Transform(&pos, &modelView, &pos);
            auto cx = column(modelView, 0), cy = column(modelView, 1), cz = column(modelView, 2);
            x *= math::VEC3Len(&cx);
            y *= math::VEC3Len(&cy);
            z *= math::VEC3Len(&cz);
        } else {
            math::VEC3Cross(&z, &p->mPrevAxis, &y);
            if (!Normalize(&z))
                z = fallback;
            math::VEC3Cross(&x, &y, &z);
            p->mPrevAxis = x;
        }
        math::MTX34 basis(x.x, y.x, z.x, pos.x, x.y, y.y, z.y, pos.y, x.z, y.z, z.z, pos.z);
        auto local = localTransform(p, s, sx, sy);
        math::MTX34 final;
        math::MTX34Mult(&final, &basis, &local);
        quad(final, false, mNumTexmap > 0);
        if (s.typeOption == 1)
            quad(final, true, mNumTexmap > 0);
    }
}
}
