#include <nw4r/ef.h>

namespace nw4r {
namespace ef {

static u8 billboard_tex0_u8[] = {0x00, 0x01, 0x00, 0x00,
                                 0x01, 0x00, 0x01, 0x01};

DrawBillboardStrategy::DrawBillboardStrategy() {}

void DrawBillboardStrategy::Draw(const DrawInfo& rInfo,
                                 ParticleManager* pManager) {

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    switch (rSetting.typeOption) {
    case EmitterDrawSetting::ASSIST_BB_NORMAL: {
        DrawNormalBillboard(rInfo, pManager);
        break;
    }

    case EmitterDrawSetting::ASSIST_BB_Y: {
        DrawYBillboard(rInfo, pManager);
        break;
    }

    case EmitterDrawSetting::ASSIST_BB_DIRECTIONAL: {
        DrawDirectionalBillboard(rInfo, pManager);
        break;
    }
    }
}

void DrawBillboardStrategy::DrawNormalBillboard(const DrawInfo& rInfo,
                                                ParticleManager* pManager) {

    InitGraphics(rInfo, pManager);

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    int flags = mNumTexmap > 0 ? 1 : 0;
    const math::MTX34& rInfoMtx = *rInfo.GetViewMtx();

    math::VEC2 pivot;
    pivot.x = rSetting.pivotX / 100.0f;
    pivot.y = rSetting.pivotY / 100.0f;

    math::MTX34 viewMtx;
    pManager->CalcGlobalMtx(&viewMtx);
    math::MTX34Mult(&viewMtx, &rInfoMtx, &viewMtx);

    if (rSetting.zOffset != 0.0f) {
        CalcZOffset(&viewMtx, pManager, rInfo, rSetting.zOffset);
    }

    f32 vx = math::FSqrt(viewMtx._00 * viewMtx._00 + viewMtx._10 * viewMtx._10 +
                         viewMtx._20 * viewMtx._20);

    f32 vy = math::FSqrt(viewMtx._01 * viewMtx._01 + viewMtx._11 * viewMtx._11 +
                         viewMtx._21 * viewMtx._21);

    f32 vz = 0.0f;

    f32 rc, rs;
    f32 mag;

    mag =
        math::FSqrt(rInfoMtx._01 * rInfoMtx._01 + rInfoMtx._11 * rInfoMtx._11);
    if (mag == 0.0f) {
        rs = 0.0f;
        rc = 1.0f;
    } else {
        f32 denom = 1.0f / mag;
        rs = -rInfoMtx._01 * denom;
        rc = rInfoMtx._11 * denom;
    }

    GetFirstDrawParticleFunc pGetFirstFunc = GetGetFirstDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    GetNextDrawParticleFunc pGetNextFunc = GetGetNextDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    bool first = true;

    for (Particle* pIt = pGetFirstFunc(pManager); pIt != NULL;
         pIt = pGetNextFunc(pManager, pIt)) {

        f32 sx = pIt->Draw_GetSizeX();
        if (sx < std::numeric_limits<f32>::epsilon()) {
            continue;
        }

        f32 sy = pIt->Draw_GetSizeY();
        if (sy < std::numeric_limits<f32>::epsilon()) {
            continue;
        }

        SetupGP(pIt, rSetting, rInfo, first, false);
        first = false;

        DispParticle_Normal(pIt, viewMtx, vx, vy, vz, rc, rs, sx, sy, pivot,
                            flags);
    }
}

inline void DrawBillboardStrategy::DispParticle_Normal(
    Particle* pParticle, const math::MTX34& rViewMtx, f32 vx, f32 vy, f32 vz,
    f32 rc, f32 rs, f32 sx, f32 sy, const math::VEC2& rPivot, int flags) {

#pragma unused(vz)

    math::VEC3 rot;
    pParticle->Draw_GetRotate(&rot);

    math::VEC3 p0;  // sp+24
    math::VEC3 d0;  // sp+30
    math::VEC3 d1;  // sp+3c
    math::VEC3 pos; // sp+48

    math::VEC3Transform(&pos, &rViewMtx, &pParticle->mParameter.mPosition);

    f32 px = rPivot.x;
    f32 py = rPivot.y;

    f32 vx_rc = vx * rc;
    f32 vx_rs = vx * rs;
    f32 vy_rc = vy * rc;
    f32 vy_rs = vy * rs;

    if (rot.z != 0.0f) {
        f32 cr, sr;
        math::SinCosRad(&sr, &cr, -rot.z);

        f32 cr_sx = cr * sx;
        f32 sr_sx = sr * sx;
        f32 cr_sy = cr * sy;
        f32 sr_sy = sr * sy;
        f32 rc_sx = rc * sx;
        f32 rs_sy = rs * sy;
        f32 rs_sx = rs * sx;
        f32 rc_sy = rc * sy;

        f32 exp0 = (px - cr_sx * px) - sr_sy * py;
        f32 exp1 = (py + sr_sx * px) - cr_sy * py;

        p0.x = vx_rc * exp0 + vy_rs * exp1 + pos.x;
        p0.y = vx_rs * exp0 - vy_rc * exp1 + pos.y;
        p0.z = pos.z;

        f32 vx_rc_cr_sx = vx_rc * cr_sx;
        f32 vx_rc_sr_sy = vx_rc * sr_sy;
        f32 vy_rs_sr_sx = vy_rs * sr_sx;
        f32 vy_rs_cr_sy = vy_rs * cr_sy;
        f32 vx_rs_cr_sx = vx_rs * cr_sx;
        f32 vx_rs_sr_sy = vx_rs * sr_sy;
        f32 vy_rc_sr_sx = vy_rc * sr_sx;
        f32 vy_rc_cr_sy = vy_rc * cr_sy;

        d0.x = vx_rc_cr_sx - vx_rc_sr_sy - vy_rs_sr_sx - vy_rs_cr_sy;
        d0.y = vx_rs_cr_sx - vx_rs_sr_sy + vy_rc_sr_sx + vy_rc_cr_sy;
        d0.z = 0.0f;

        d1.x = vx_rc_cr_sx + vx_rc_sr_sy - vy_rs_sr_sx + vy_rs_cr_sy;
        d1.y = vx_rs_cr_sx + vx_rs_sr_sy + vy_rc_sr_sx - vy_rc_cr_sy;
        d1.z = 0.0f;
    } else {
        p0.x = vx_rc*(px-sx*px)+vy_rs*(py-sy*py)+pos.x;
        p0.y = vx_rs*(px-sx*px)-vy_rc*(py-sy*py)+pos.y;
        p0.z = pos.z;
        d0 = math::VEC3(vx_rc*sx-vy_rs*sy,vx_rs*sx+vy_rc*sy,0);
        d1 = math::VEC3(vx_rc*sx+vy_rs*sy,vx_rs*sx-vy_rc*sy,0);
    }

    DispPolygon(p0, d0, d1, flags);
}

void DrawBillboardStrategy::DrawYBillboard(const DrawInfo& info,ParticleManager* manager) {
    InitGraphics(info,manager);
    const auto& setting=*manager->mResource->GetEmitterDrawSetting();
    const auto& camera=*info.GetViewMtx();
    math::MTX34 view; manager->CalcGlobalMtx(&view); math::MTX34Mult(&view,&camera,&view);
    if (setting.zOffset) CalcZOffset(&view,manager,info,setting.zOffset);
    const float vx=std::sqrt(view._00*view._00+view._10*view._10+view._20*view._20);
    const float vy=std::sqrt(view._01*view._01+view._11*view._11+view._21*view._21);
    const float length=std::sqrt(camera._11*camera._11+camera._21*camera._21);
    const float rc=length ? camera._11/length : 0.f,rs=length ? camera._21/length : 1.f;
    const float px=setting.pivotX/100.f,py=setting.pivotY/100.f;
    auto firstParticle=GetGetFirstDrawParticleFunc(setting.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    auto nextParticle=GetGetNextDrawParticleFunc(setting.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    bool first=true;
    for (auto* particle=firstParticle(manager);particle;particle=nextParticle(manager,particle)) {
        const float sx=particle->Draw_GetSizeX(),sy=particle->Draw_GetSizeY();
        if (sx<std::numeric_limits<float>::epsilon() || sy<std::numeric_limits<float>::epsilon()) continue;
        SetupGP(particle,setting,info,first,false); first=false;
        math::VEC3 pos,rotation;
        math::VEC3Transform(&pos,&view,&particle->mParameter.mPosition);
        particle->Draw_GetRotate(&rotation);
        float sr=0,cr=1; if (rotation.z) math::SinCosRad(&sr,&cr,-rotation.z);
        const float a=cr*sx,b=sr*sx,c=cr*sy,d=sr*sy;
        const float y=(-py-b*px)+c*py;
        math::VEC3 origin(pos.x+vx*((px-a*px)-d*py),pos.y+vy*rc*y,pos.z+vy*rs*y);
        math::VEC3 d0(vx*(a-d),vy*rc*(b+c),vy*rs*(b+c));
        math::VEC3 d1(vx*(a+d),vy*rc*(b-c),vy*rs*(b-c));
        DispPolygon(origin,d0,d1,mNumTexmap>0);
    }
}

void DrawBillboardStrategy::DrawDirectionalBillboard(const DrawInfo& info,ParticleManager* manager) {
    InitGraphics(info,manager);
    const auto& setting=*manager->mResource->GetEmitterDrawSetting();
    const auto& camera=*info.GetViewMtx();
    math::MTX34 view; manager->CalcGlobalMtx(&view); math::MTX34Mult(&view,&camera,&view);
    if (setting.zOffset) CalcZOffset(&view,manager,info,setting.zOffset);
    const float vx=std::sqrt(view._00*view._00+view._10*view._10+view._20*view._20);
    const float vy=std::sqrt(view._01*view._01+view._11*view._11+view._21*view._21);
    const float px=setting.pivotX/100.f,py=setting.pivotY/100.f;
    AheadContext context(camera,manager); auto ahead=GetCalcAheadFunc(manager);
    auto firstParticle=GetGetFirstDrawParticleFunc(setting.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    auto nextParticle=GetGetNextDrawParticleFunc(setting.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    bool first=true;
    for (auto* particle=firstParticle(manager);particle;particle=nextParticle(manager,particle)) {
        const float sx=particle->Draw_GetSizeX(),sy=particle->Draw_GetSizeY();
        if (sx<std::numeric_limits<float>::epsilon() || sy<std::numeric_limits<float>::epsilon()) continue;
        SetupGP(particle,setting,info,first,false); first=false;
        math::VEC3 axis,pos; ahead(&axis,&context,particle);
        math::VEC3TransformNormal(&axis,&view,&axis);
        const float length=std::sqrt(axis.x*axis.x+axis.y*axis.y);
        const float rc=length ? axis.y/length : 1.f,rs=length ? -axis.x/length : 0.f;
        float stretch=1;
        if (setting.typeOption0) {
            const auto delta=particle->mParameter.mPosition-particle->mParameter.mPrevPosition;
            stretch=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z)*.5f/sy+1.f;
        }
        math::VEC3Transform(&pos,&view,&particle->mParameter.mPosition);
        const float x=px-sx*px,y=sy*((stretch+py)-1.f)-py;
        math::VEC3 origin(pos.x+vx*rc*x+vy*rs*y,pos.y+(vx*rs*x-vy*rc*y),pos.z);
        math::VEC3 d0(vx*rc*sx-stretch*vy*rs*sy,vx*rs*sx+stretch*vy*rc*sy,0);
        math::VEC3 d1(vx*rc*sx+stretch*vy*rs*sy,vx*rs*sx-stretch*vy*rc*sy,0);
        DispPolygon(origin,d0,d1,mNumTexmap>0);
    }
}

void DrawBillboardStrategy::DispPolygon(const math::VEC3& rP,
                                        const math::VEC3& rD1,
                                        const math::VEC3& rD2, int flags) {

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    {
        GXPosition(rP - rD1);
        if (flags != 0) {
            GXTexCoord1x8(0);
        }

        GXPosition(rP - rD2);
        if (flags != 0) {
            GXTexCoord1x8(1);
        }

        GXPosition(rP + rD1);
        if (flags != 0) {
            GXTexCoord1x8(2);
        }

        GXPosition(rP + rD2);
        if (flags != 0) {
            GXTexCoord1x8(3);
        }
    }
    GXEnd();
}

void DrawBillboardStrategy::CalcZOffset(math::MTX34* pMtx,
                                        const ParticleManager* pManager,
                                        const DrawInfo& rInfo, f32 offsetZ) {

    f32 proj[GX_PROJECTION_SZ];
    GXGetProjectionv(proj);

    switch (static_cast<GXProjectionType>(proj[0])) {
    case GX_PERSPECTIVE: {
        math::MTX34 glbMtx;
        pManager->mManagerEM->CalcGlobalMtx(&glbMtx);

        math::VEC3 pos(glbMtx._03, glbMtx._13, glbMtx._23);
        math::VEC3TransformCoord(&pos, rInfo.GetViewMtx(), &pos);

        if (Normalize(&pos)) {
            if (pos.z >= 0.0f) {
                pMtx->_03 += pos.x * offsetZ;
                pMtx->_13 += pos.y * offsetZ;
                pMtx->_23 += pos.z * offsetZ;
            } else {
                pMtx->_03 -= pos.x * offsetZ;
                pMtx->_13 -= pos.y * offsetZ;
                pMtx->_23 -= pos.z * offsetZ;
            }
        }
        break;
    }

    case GX_ORTHOGRAPHIC: {
        pMtx->_23 += offsetZ;
        break;
    }

    default: {
        break;
    }
    }
}

void DrawBillboardStrategy::InitGraphics(const DrawInfo& rInfo,
                                         ParticleManager* pManager) {

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    InitTexture(rSetting);
    InitTev(rSetting, rInfo);
    InitColor(pManager, rSetting, rInfo);

    GXEnableTexOffsets(GX_TEXCOORD0, TRUE, TRUE);

    GXSetArray(GX_VA_TEX0, billboard_tex0_u8, sizeof(billboard_tex0_u8), 2, true);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);

    if (mNumTexmap > 0) {
        GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
    }

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);

    math::MTX34 ident;
    math::MTX34Identity(&ident);
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
}

DrawStrategyImpl::CalcAheadFunc
DrawBillboardStrategy::GetCalcAheadFunc(ParticleManager* pManager) {

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    switch (rSetting.typeDir) {
    case EmitterDrawSetting::AHEAD_BB_SPEED: {
        return CalcAhead_Speed;
    }

    case EmitterDrawSetting::AHEAD_BB_EMITTER_CENTER: {
        return CalcAhead_EmitterCenter;
    }

    case EmitterDrawSetting::AHEAD_BB_EMITTER_DESIGN: {
        return CalcAhead_EmitterDesign;
    }

    case EmitterDrawSetting::AHEAD_BB_PARTICLE: {
        return CalcAhead_Particle;
    }

    case EmitterDrawSetting::AHEAD_BB_PARTICLE_BOTH: {
        return CalcAhead_ParticleBoth;
    }

    default: {
        return CalcAhead_Speed;
    }
    }
}

} // namespace ef
} // namespace nw4r
