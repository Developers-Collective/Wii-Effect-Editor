#include <nw4r/ef.h>
#include <algorithm>
#include <vector>

namespace nw4r::ef {
namespace {
struct StripeVertex { math::VEC3 center,x,z; float t; };
math::VEC3 column(const math::MTX34& m,int n) { return {m.m[0][n],m.m[1][n],m.m[2][n]}; }
StripeVertex interpolate(const StripeVertex& a,const StripeVertex& b,const StripeVertex& c,float t) {
    const float wc=t*t*.5f,wa=(wc-t)+.5f,wb=-t*t+t+.5f;
    return {a.center*wa+b.center*wb+c.center*wc,a.x*wa+b.x*wb+c.x*wc,
            a.z*wa+b.z*wb+c.z*wc,c.t*wc+a.t*wa+b.t*wb};
}
}
DrawStripeStrategy::DrawStripeStrategy() = default;
DrawSmoothStripeStrategy::DrawSmoothStripeStrategy() = default;
void DrawStripeStrategy::Draw(const DrawInfo& info,ParticleManager* manager) { DrawStripes(info,manager,false); }
void DrawSmoothStripeStrategy::Draw(const DrawInfo& info,ParticleManager* manager) { DrawStripes(info,manager,true); }
DrawStrategyImpl::GetFirstDrawParticleFunc DrawSmoothStripeStrategy::GetGetFirstDrawParticleFunc(int order) {
    return DrawStrategyImpl::GetGetFirstDrawParticleFunc(order);
}
DrawStrategyImpl::GetNextDrawParticleFunc DrawSmoothStripeStrategy::GetGetNextDrawParticleFunc(int order) {
    return DrawStrategyImpl::GetGetNextDrawParticleFunc(order);
}
DrawStrategyImpl::CalcAheadFunc DrawStripeStrategy::GetCalcAheadFunc(ParticleManager*) { return CalcAhead_Stripe; }
DrawStrategyImpl::CalcAheadFunc DrawSmoothStripeStrategy::GetCalcAheadFunc(ParticleManager*) { return CalcAhead_Stripe; }
void DrawStrategyImpl::CalcAhead_Stripe(math::VEC3* result,AheadContext* context,Particle* p) {
    auto* manager=context->mCommon.mParticleManager;
    const auto& s=*manager->mResource->GetEmitterDrawSetting();
    switch (s.typeDir) {
    case 1: CalcAhead_EmitterCenter(result,context,p); return;
    case 2: CalcAhead_EmitterDesign(result,context,p); return;
    case 5:case 7: CalcAhead_NoDesign(result,context,p); return;
    case 3: {
        auto* next=GetYoungerDrawParticle(manager,p);
        *result=p->mParameter.mPosition-(next ? next->mParameter.mPosition : context->mCommon.mEmitterCenter);
        break;
    }
    case 6: {
        auto* elder=GetElderDrawParticle(manager,p); auto* younger=GetYoungerDrawParticle(manager,p);
        const int connection=s.typeOption2&7;
        if (connection==1) { if (!elder) elder=GetYoungestDrawParticle(manager); if (!younger) younger=GetOldestDrawParticle(manager); }
        math::VEC3 a(0,0,0),b(0,0,0);
        if (elder) { a=elder->mParameter.mPosition-p->mParameter.mPosition; Normalize(&a); }
        if (younger) { b=younger->mParameter.mPosition-p->mParameter.mPosition; Normalize(&b); }
        else if (connection==2) { b=context->mCommon.mEmitterCenter-p->mParameter.mPosition; Normalize(&b); }
        *result=a-b; break;
    }
    default: CalcAhead_Speed(result,context,p); return;
    }
    if (!Normalize(result)) *result=context->mCommon.mEmitterAxisY;
}
void DrawStrategyImpl::DrawStripes(const DrawInfo& info,ParticleManager* manager,bool smooth) {
    const auto& s=*manager->mResource->GetEmitterDrawSetting();
    const bool reverse=(s.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER)!=0;
    auto getFirst=GetGetFirstDrawParticleFunc(s.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    auto getNext=GetGetNextDrawParticleFunc(s.mFlags&EmitterDrawSetting::FLAG_DRAW_ORDER);
    std::vector<Particle*> particles;
    for (auto* p=getFirst(manager);p;p=getNext(manager,p)) particles.push_back(p);
    if (particles.empty()) return;
    const int connection=s.typeOption2&7;
    const bool tube=s.typeOption==3,billboard=s.typeOption==2;
    AheadContext context(*info.GetViewMtx(),manager);
    math::MTX34 modelView,emitterLocal,inverseView;
    math::MTX34Mult(&modelView,info.GetViewMtx(),&context.mCommon.mParticleManagerMtx);
    math::MTX34Mult(&emitterLocal,&context.mCommon.mParticleManagerMtxInv,&context.mCommon.mEmitterMtx);
    auto fallback=column(emitterLocal,0);
    const auto& axes=s.typeDir==7 ? context.mCommon.mParticleManagerMtxInv : emitterLocal;
    math::VEC3 initial;
    switch (s.typeOption2&0x38) {
    case 8:initial=column(axes,0);break;
    case 16:initial=column(axes,2);break;
    case 24:initial=column(axes,0)+column(axes,1)+column(axes,2);break;
    default:initial=column(axes,1);break;
    }
    if (!Normalize(&initial)) initial=context.mCommon.mEmitterAxisY;
    for (auto* p=GetYoungestParticle(manager);p && p->mPrevAxis.x>1;p=GetElderParticle(manager,p)) p->mPrevAxis=initial;
    math::VEC3 screenZ(0,0,1);
    if (math::MTX34Inv(&inverseView,&modelView)) screenZ=column(inverseView,2);
    auto makeVertex=[&](Particle* p,math::VEC3 pos,float t) {
        math::VEC3 y,x,z; CalcAhead_Stripe(&y,&context,p);
        auto previous=billboard ? screenZ : p->mPrevAxis;
        math::VEC3Cross(&x,&y,&previous); if (!Normalize(&x)) x=fallback;
        math::VEC3Cross(&z,&x,&y); Normalize(&z); p->mPrevAxis=z;
        math::VEC3 rot; p->Draw_GetRotate(&rot); float sn,cs; math::SinCosRad(&sn,&cs,rot.y);
        const float sx=p->Draw_GetSizeX(),sz=tube ? p->Draw_GetSizeY() : sx;
        const float px=s.pivotX*.01f,pz=tube ? s.pivotY*.01f : 0;
        auto axisX=x*(cs*sx)+z*(sn*sx),axisZ=x*(-sn*sz)+z*(cs*sz);
        auto center=pos+x*px+z*pz-axisX*px-axisZ*pz;
        return StripeVertex{center,axisX,axisZ,t};
    };
    // Even a stripe with too few particles updates its transported axes.
    const size_t minimum=connection==1 ? 3 : connection==2 ? 1 : 2;
    if (particles.size()<minimum || (tube && s.typeOption0<3)) {
        for (auto* p:particles) makeVertex(p,p->mParameter.mPosition,0);
        return;
    }
    InitTexture(s); InitTev(s,info); InitColor(manager,s,info);
    GXEnableTexOffsets(GX_TEXCOORD0,TRUE,TRUE);
    GXClearVtxDesc(); GXSetVtxDesc(GX_VA_POS,GX_DIRECT);
    if (mNumTexmap) GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GXSetCurrentMtx(GX_PNMTX0); GXLoadPosMtxImm(modelView,GX_PNMTX0);
    // NW4R binds the leading particle's material for the entire stripe.
    SetupGP(particles.front(),s,info,true,false);
    const bool repeat=(s.typeOption2&0xc0)==0x40;
    const int n=int(particles.size()),total=n+(connection ? 1 : 0);
    const float dt=repeat ? 1.f : 1.f/float(total-1);
    std::vector<StripeVertex> control;
    auto* youngest=GetYoungestDrawParticle(manager);
    if (connection==2 && !reverse) control.push_back(makeVertex(youngest,context.mCommon.mEmitterCenter,0));
    for (int i=0;i<n;++i) {
        float t=dt*float(reverse ? total-1-i : i+(connection==2));
        if (smooth && connection==1) t=dt*(reverse ? n+.5f-i : i-.5f);
        control.push_back(makeVertex(particles[i],particles[i]->mParameter.mPosition,t));
    }
    if (connection==2 && reverse) control.push_back(makeVertex(youngest,context.mCommon.mEmitterCenter,0));
    if (connection==1) {
        auto closing=control.front(); closing.t=dt*(reverse ? 0 : n);
        if (smooth) closing.t=dt*(reverse ? .5f : n-.5f);
        control.push_back(closing);
        if (smooth) { closing=control[1]; closing.t=dt*(reverse ? -.5f : n+.5f); control.push_back(closing); }
    }
    std::vector<StripeVertex> vertices;
    if (smooth) {
        const int subdivisions=std::max(1,int(s.typeOption1));
        if (connection!=1) { control.insert(control.begin(),control.front()); control.push_back(control.back()); }
        for (size_t i=0;i+2<control.size();++i) {
            for (int j=0;j<subdivisions;++j) vertices.push_back(interpolate(control[i],control[i+1],control[i+2],float(j)/subdivisions));
        }
        const size_t end=control.size(); vertices.push_back(interpolate(control[end-3],control[end-2],control[end-1],1));
    } else vertices=std::move(control);
    auto emit=[&](math::VEC3 pos,float u,float v) { GXPosition3f32(pos.x,pos.y,pos.z); if (mNumTexmap) GXTexCoord2f32(u,v); };
    if (tube) {
        const int sides=s.typeOption0;
        std::vector<math::VEC2> circle;
        for (int j=0;j<=sides;++j) {
            float sn=0,cs=1; if (j && j<sides) math::SinCosFIdx(&sn,&cs,256.f*float(j)/sides);
            circle.emplace_back(cs,sn);
        }
        for (auto cull:{GX_CULL_FRONT,GX_CULL_BACK}) {
            GXSetCullMode(cull);
            for (size_t i=1;i<vertices.size();++i) {
                GXBegin(GX_TRIANGLESTRIP,GX_VTXFMT0,u16((sides+1)*2));
                for (int j=0;j<=sides;++j) {
                    const auto& a=vertices[reverse ? i-1 : i]; const auto& b=vertices[reverse ? i : i-1];
                    emit(a.center+a.x*circle[j].x+a.z*circle[j].y,float(j)/sides,a.t);
                    emit(b.center+b.x*circle[j].x+b.z*circle[j].y,float(j)/sides,b.t);
                }
                GXEnd();
            }
        }
        GXSetCullMode(GX_CULL_NONE);
    } else {
        for (int plane=0;plane<(s.typeOption==1 ? 2 : 1);++plane) {
            // Split long strips at a shared pair to stay within GX's u16 count.
            for (size_t first=0;first+1<vertices.size();) {
                size_t end=std::min(vertices.size(),first+32766);
                GXBegin(GX_TRIANGLESTRIP,GX_VTXFMT0,u16((end-first)*2));
                for (size_t i=first;i<end;++i) { const auto& v=vertices[i]; auto axis=plane ? v.z : v.x;
                    if (plane && !smooth) axis*=-1;
                    emit(v.center+axis,1,v.t); emit(v.center-axis,0,v.t);
                }
                GXEnd(); if (end==vertices.size()) break; first=end-1;
            }
        }
    }
}
}
