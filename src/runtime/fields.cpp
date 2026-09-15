#include "fields.h"
#include "curves.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace breff {
using namespace nw4r;
void applyField(std::span<const uint8_t> track,ef::Particle& particle,uint32_t tick,uint16_t seed,uint32_t life,
                const FieldContext& ctx,math::VEC3& addVelocity,math::VEC3& addPosition) {
    auto byte=[&](size_t p) { if (p>=track.size()) throw std::runtime_error("Truncated field animation"); return track[p]; };
    auto word=[&](size_t p) { return (uint16_t(byte(p))<<8)|byte(p+1); };
    auto dword=[&](size_t p) { return (uint32_t(word(p))<<16)|word(p+2); };
    const size_t info=32+size_t(dword(12))+dword(16)+dword(20)+dword(24);
    unsigned kind=byte(1),space=byte(info),target=byte(info+1),options=byte(info+3);
    unsigned count=0;
    switch (kind) { case 0:case 2:case 6:count=4;break;case 1:case 8:count=1;break;case 3:count=5;break;case 4:count=6;break;case 7:count=2;break;default:throw std::runtime_error("Unknown force field"); }
    std::array<float,6> values{};
    for (unsigned i=0;i<count;++i) values[i]=std::bit_cast<float>(dword(info+4+i*4));
    if (dword(12)) evaluateF32(track,{values.data(),count},tick,seed,life);
    auto& p=values;
    auto transform=[](const math::MTX34& matrix,math::VEC3 v) { math::VEC3 out; math::VEC3Transform(&out,&matrix,&v); return out; };
    auto normal=[](const math::MTX34& matrix,math::VEC3 v) { math::VEC3 out; math::VEC3TransformNormal(&out,&matrix,&v); return out; };
    auto length=[](const math::VEC3& v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); };
    auto normalize=[&](math::VEC3& v) { float size=length(v); if (size<=std::numeric_limits<float>::epsilon()) { v=math::VEC3(0,0,0); return false; } v*=1.f/size; return true; };
    auto dot=[](const math::VEC3& a,const math::VEC3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
    auto cross=[](const math::VEC3& a,const math::VEC3& b) { return math::VEC3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); };
    math::VEC3 force(0,0,0),position=ctx.position;
    if (kind==2 || kind==3 || kind==4 || kind==6) {
        if (space==1) position=transform(ctx.localToEmitter,position);
        else position=transform(ctx.localToWorld,position)-math::VEC3(ctx.emitterToWorld._03,ctx.emitterToWorld._13,ctx.emitterToWorld._23);
    }
    switch (kind) {
    case 0: ef::Rotation2VecY(math::VEC3(p[1],p[2],p[3]),&force); force*=p[0]; break;
    case 1: force=ctx.velocity*(p[0]-1.f); break;
    case 2: force=math::VEC3(p[1],p[2],p[3])-position; normalize(force); force*=p[0]; break;
    case 3: {
        force=math::VEC3(p[2],p[3],p[4])-position;
        float distance=dot(force,force); normalize(force); force*=p[0];
        if (p[1]*p[1]<distance) force*=p[1]*p[1]/distance;
        break;
    }
    case 4: {
        math::VEC3 axis; ef::Rotation2VecY(math::VEC3(p[3],p[4],p[5]),&axis);
        auto radial=position-axis*dot(axis,position); float distance=dot(radial,radial);
        if (distance) {
            float speed=p[1],radius=p[2]*p[2];
            if (distance<radius) speed=(1.f-distance/radius)*p[0]+distance/radius*p[1];
            normalize(radial); force=cross(radial,axis)*speed;
        }
        break;
    }
    case 6: {
        math::VEC3 axis; ef::Rotation2VecY(math::VEC3(p[1],p[2],p[3]),&axis);
        float sine,cosine; math::SinCosRad(&sine,&cosine,p[0]);
        force=position*cosine+cross(axis,position)*sine+axis*(dot(axis,position)*(1.f-cosine))-position;
        break;
    }
    case 7: {
        const unsigned interval=word(info+12)+1;
        if (!(byte(4)&16) && !particle.mTick) break;
        if (particle.mTick && tick%interval) break;
        uint32_t random=seed*0x3f81f635U+word(6)*0x30a74193U+(tick&65535)*0x371097e7U+0x4bf53U;
        random^=random<<8; random^=random<<16;
        auto advance=[&]() { random=random*0x343fdU+0x269ec3U; };
        float power=p[0];
        if (options&1) {
            auto velocity=ctx.velocity;
            if (target==1 && particle.mTick) velocity=ctx.movement*(1.f/particle.mParameter.mMomentum);
            power=length(normal(ctx.localToEmitter,velocity));
        }
        if (options&2) {
            force.x=float(int16_t(random>>16))/32768.f; advance();
            force.y=float(int16_t(random>>16))/32768.f; advance();
            force.z=float(int16_t(random>>16))/32768.f;
            if ((options&1) && !normalize(force)) force.y=1;
            force*=power;
        } else {
            auto axis=normal(ctx.localToEmitter,particle.mTick ? ctx.movement : ctx.velocity);
            if (!normalize(axis)) axis.y=1;
            math::MTX34 direction; ef::GetDirMtxY(&direction,axis);
            if (p[1]==0) force=math::VEC3(0,(options&1) ? power : float(int16_t(random>>16))/32768.f*power,0);
            else {
                float sine,cosine,azimuthS,azimuthC;
                math::SinCosRad(&sine,&cosine,float(random>>16)/65535.f*p[1]); advance();
                math::SinCosRad(&azimuthS,&azimuthC,float(random>>16)/65535.f*6.283185307179586f); advance();
                if (!(options&1)) power*=float(random>>16)/65535.f;
                force=math::VEC3(sine*azimuthS*power,cosine*power,sine*azimuthC*power);
            }
            force=transform(direction,force);
        }
        if (!(options&4)) force.x=0;
        if (!(options&8)) force.y=0;
        if (!(options&16)) force.z=0;
        break;
    }
    case 8: {
        auto previous=particle.mParticleManager->mManagerEM->mPreviousGlobalPosition;
        if (!std::isnan(previous.x)) force=(math::VEC3(ctx.emitterToWorld._03,ctx.emitterToWorld._13,ctx.emitterToWorld._23)-previous)*p[0];
        break;
    }
    }
    if (space==1) force=normal(ctx.emitterToLocal,force);
    else if (space==0 || space==3) {
        if (space==3) {
            math::VEC3 sum(ctx.emitterToWorld._00+ctx.emitterToWorld._01+ctx.emitterToWorld._02,
                           ctx.emitterToWorld._10+ctx.emitterToWorld._11+ctx.emitterToWorld._12,
                           ctx.emitterToWorld._20+ctx.emitterToWorld._21+ctx.emitterToWorld._22);
            force*=length(sum)*.577350269f;
        }
        force=normal(ctx.worldToLocal,force);
    }
    if (target==0) addVelocity+=force;
    else if (target==1) addPosition+=force;
}
}
