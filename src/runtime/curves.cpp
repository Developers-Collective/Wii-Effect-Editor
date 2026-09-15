#include "curves.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace breff {
namespace {
struct Bytes {
    std::span<const uint8_t> data;
    void check(size_t p, size_t n) const {
        if (p > data.size() || n > data.size()-p) throw std::runtime_error("Truncated animation curve");
    }
    uint8_t u8(size_t p) const { check(p,1); return data[p]; }
    uint16_t u16(size_t p) const { check(p,2); return uint16_t(data[p])<<8 | data[p+1]; }
    uint32_t u32(size_t p) const { return uint32_t(u16(p))<<16 | u16(p+2); }
    float f32(size_t p) const { return std::bit_cast<float>(u32(p)); }
};
struct Time { float frame; uint32_t loop; };
Time curveTime(const Bytes& b, uint32_t tick, uint32_t life) {
    const auto length=b.u16(8);
    if (length<2) return {0,tick};
    const uint32_t last=length-1, loops=b.u8(5), flags=b.u8(4);
    const bool infinite=flags&32, turn=flags&64, fit=flags&128;
    if (!infinite && loops<2) {
        if (!fit) return {float(std::min(tick,last)),0};
        return {life==1 ? float(last) : std::min(float(last),float(tick)*(float(last)/float(life-1))),0};
    }
    if (fit) {
        if (tick>=life-1) return {turn && !(loops&1) ? 0.f : float(last),uint8_t(loops-1)};
        float frame=float(tick)*(float(loops)*(float(last)/float(life-1)));
        uint32_t loop=uint32_t(frame/float(last));
        frame-=float(loop*last);
        if (turn && (loop&1)) frame=float(last)-frame;
        return {frame,loop};
    }
    uint32_t loop=tick/last;
    if (!infinite && loop>=loops) return {turn && !(loops&1) ? 0.f : float(last),uint8_t(loops-1)};
    return {float(turn && (loop&1) ? last*(loop+1)-tick : tick-loop*last),loop};
}
uint32_t nextRandom(uint32_t state) { return state*0x343fdU+0x269ec3U; }
// Discrete texture keys use length/life, unlike numeric keys' length-1/life-1.
Time discreteTime(const Bytes& b,uint32_t tick,uint32_t life) {
    const uint32_t length=b.u16(8),loops=b.u8(5),flags=b.u8(4);
    if (!length || !life) throw std::runtime_error("Zero texture animation duration");
    const bool infinite=flags&32,turn=flags&64,fit=flags&128;
    if (!infinite && loops<2)
        return {float(uint16_t(fit ? (length*tick)/life : std::min(tick,length))),0};
    if (fit) {
        if (tick>=life) return {turn && !(loops&1) ? 0.f : float(length),uint8_t(loops-1)};
        if (!turn || length<2) {
            float frame=float(tick)*(float(loops)*(float(length)/float(life)));
            uint32_t loop=uint32_t(frame/float(length));
            return {float(uint16_t(frame-float(loop*length))),loop};
        }
        uint32_t last=length-1,frame=uint32_t(float(tick)*((float(last)*float(loops)+1.f)/float(life)));
        uint32_t loop=frame/last;
        if (loop>=loops) return {loops&1 ? float(last) : 0.f,uint8_t(loops-1)};
        return {float(uint16_t(loop&1 ? last*(loop+1)-frame : frame-loop*last)),loop};
    }
    const uint32_t period=turn && length>=2 ? length-1 : length,loop=tick/period;
    if (!infinite && loop>=loops)
        return {turn && length>=2 && !(loops&1) ? 0.f : float(period),uint8_t(loops-1)};
    return {float(uint16_t(turn && length>=2 && (loop&1) ? period*(loop+1)-tick : tick-loop*period)),loop};
}
uint32_t keySeed(uint16_t seed,uint16_t curveSeed,uint32_t loop,uint16_t index) {
    uint32_t h=seed*0x3f81f635U+curveSeed*0x30a74193U+loop*0x7b929U+index*0x371097e7U+0x4bf53U;
    h^=h<<8; h^=h<<16; return h;
}
float interpolate(float a,float b,float t,uint8_t code) {
    if (a==b) return a;
    switch (code&3) {
    case 0: return a+t*(b-a);
    case 1:
        if (code==1) return a+t*(t*((3.f-2.f*t)*(b-a)));
        else {
            float s=(code&8)?1.5f:0.f,e=(code&4)?1.5f:0.f;
            float h=s+(t*(t*((s+e)-2.f))+t*(3.f+(-2.f*s-e)));
            return a+(t*(b-a))*h;
        }
    case 2:return a;
    default:return 0;
    }
}
}

template<class T, bool rotate=false> void evaluateNumeric(std::span<const uint8_t> curve,std::span<T> output,
                 uint32_t tick,uint16_t seed,uint32_t life) {
    constexpr bool bytes=std::is_same_v<T,uint8_t>;
    Bytes b{curve}; b.check(0,32);
    uint8_t mask=b.u8(3);
    if (!mask) return;
    if (output.size()<std::bit_width(unsigned(mask))) throw std::runtime_error("Animation target is too small");
    const auto time=curveTime(b,tick,life);
    const size_t components=std::popcount(mask);
    if (b.u8(0)==0xAB) {
        size_t value=32+size_t(uint16_t(time.frame))*components*sizeof(T);
        for (unsigned c=0;c<8;++c) if (mask&(1<<c)) { output[c]=bytes ? T(b.u8(value)) : T(b.f32(value)); value+=sizeof(T); }
        return;
    }
    if (b.u8(0)!=0xAC) throw std::runtime_error("Unknown animation magic");
    const size_t count=b.u16(32),stride=bytes ? ((13+components)&~size_t(1)) : 12+components*4;
    if (!count) throw std::runtime_error("Animation has no keys");
    b.check(36,count*stride);
    size_t left=0;
    // Keys are ordered by integer frame. Fractional fitted times interpolate.
    while (left+1<count && float(b.u16(36+(left+1)*stride))<=time.frame) ++left;
    bool exact=time.frame<=float(b.u16(36+left*stride)) || left+1==count;
    uint32_t leftLoop=time.loop,rightLoop=time.loop;
    const auto flags=b.u8(4),loops=b.u8(5);
    if (!exact && (flags&64) && ((flags&32)||loops>1)) {
        if (!(time.loop&1) && left+2>=count && ((flags&32)||time.loop<uint32_t(loops-1))) ++rightLoop;
        if ((time.loop&1) && !left && time.loop) ++leftLoop;
    }
    auto values=[&](size_t index,uint32_t loop) {
        std::array<float,8> out{};
        size_t key=36+index*stride,value=key+12;
        uint8_t type=b.u8(key+2);
        uint32_t random=0;
        bool negate=false;
        if (type) {
            const auto rangeIndex=b.u16(value);
            random=keySeed(seed,b.u16(6),loop,rangeIndex);
            size_t table=32+size_t(b.u32(12));
            if (type&2) {
                table+=b.u32(16);
                const auto n=b.u16(table);
                if (!n) throw std::runtime_error("Empty animation random table");
                value=table+4+((random>>16)%n)*(components*2*sizeof(T)+(rotate?4:0));
                random=nextRandom(random);
            } else value=table+4+size_t(rangeIndex)*(components*2*sizeof(T)+(rotate?4:0));
            if constexpr (rotate) {
                if (b.u8(value+components*8)) {
                    negate=(random&0x10000)==0;
                    random=nextRandom(random);
                }
            }
        }
        for (unsigned c=0;c<std::bit_width(unsigned(mask));++c) {
            if (mask&(1<<c)) {
                if (!type) { out[c]=bytes ? float(b.u8(value)) : b.f32(value); value+=sizeof(T); }
                else if constexpr (bytes) {
                    out[c]=float(std::clamp(int(b.u8(value))+int(b.u8(value+1))*int(int16_t(random>>16))/32768,0,255)); value+=2;
                } else { out[c]=b.f32(value)+b.f32(value+4)*float(random>>16); if (negate) out[c]=-out[c]; value+=8; }
            }
            if (type && !(flags&4) && (bytes || rotate || (mask&(1<<c)))) random=nextRandom(random);
        }
        return out;
    };
    auto a=values(left,exact?time.loop:leftLoop);
    if (exact) { for (unsigned c=0;c<8;++c) if (mask&(1<<c)) output[c]=T(a[c]); return; }
    auto z=values(left+1,rightLoop);
    float delta=time.frame-float(b.u16(36+left*stride));
    uint32_t interval=b.u16(36+(left+1)*stride)-b.u16(36+left*stride);
    uint32_t fixed=bytes ? uint32_t(delta*65536.f)/interval : 0;
    float t=bytes ? float(fixed)/65536.f : delta/float(interval);
    for (unsigned c=0;c<8;++c) if (mask&(1<<c)) {
        auto code=b.u8(40+left*stride+c);
        if constexpr(bytes) {
            if ((code&3)==0) output[c]=uint8_t(int(a[c])+int(std::floor(double(int64_t(fixed)*int(z[c]-a[c]))/65536.)));
            else output[c]=uint8_t(std::clamp(interpolate(a[c],z[c],t,code),0.f,255.f));
        } else output[c]=interpolate(a[c],z[c],t,code);
    }
}
void evaluateF32(std::span<const uint8_t> b,std::span<float> out,uint32_t tick,uint16_t seed,uint32_t life) { evaluateNumeric(b,out,tick,seed,life); }
void evaluateU8(std::span<const uint8_t> b,std::span<uint8_t> out,uint32_t tick,uint16_t seed,uint32_t life) { evaluateNumeric(b,out,tick,seed,life); }
void evaluateRotate(std::span<const uint8_t> b,std::span<float> out,uint32_t tick,uint16_t seed,uint32_t life) { evaluateNumeric<float,true>(b,out,tick,seed,life); }
TextureSelection evaluateTexture(std::span<const uint8_t> curve,uint32_t tick,uint16_t seed,uint32_t life) {
    Bytes b{curve}; b.check(0,36);
    const auto time=discreteTime(b,tick,life);
    const size_t count=b.u16(32);
    if (!count) throw std::runtime_error("Texture animation has no keys");
    b.check(36,count*16);
    size_t index=0;
    while (index+1<count && b.u16(36+(index+1)*16)<=time.frame) ++index;
    size_t key=36+index*16,value=key+12;
    const auto type=b.u8(key+2);
    uint16_t random=0;
    if (type) {
        random=keySeed(seed,b.u16(6),time.loop,b.u16(value))>>16;
        const size_t ranges=32+size_t(b.u32(12));
        if (type&2) {
            size_t table=ranges+b.u32(16);
            if (!b.u16(table)) throw std::runtime_error("Empty texture random table");
            value=table+4+(random%b.u16(table))*4;
        } else value=ranges+4+size_t(b.u16(value))*8;
    }
    TextureSelection out{b.u8(value),b.u8(value+1),b.u16(value+2)};
    if (type && !(type&2)) {
        const auto reverse=b.u8(value+4);
        if (reverse==1) out.reverse=(out.reverse&2)|(random&1);
        else if (reverse==2) out.reverse=(out.reverse&1)|(random&2);
        else if (reverse==3) out.reverse=random&3;
    }
    return out;
}
std::string curveName(std::span<const uint8_t> curve,uint16_t index) {
    Bytes b{curve};
    size_t table=32+size_t(b.u32(12))+b.u32(16)+b.u32(20);
    const auto count=b.u16(table);
    if (index>=count) throw std::runtime_error("Animation name index out of range");
    size_t pos=table+4+size_t(count)*4;
    for (unsigned i=0;i<=index;++i) {
        const size_t length=b.u16(pos); pos+=2; b.check(pos,length);
        if (!length || b.u8(pos+length-1)!=0) throw std::runtime_error("Invalid animation name");
        if (i==index) return {reinterpret_cast<const char*>(curve.data()+pos),length-1};
        pos+=length;
    }
    throw std::runtime_error("Animation name not found");
}
std::vector<std::array<uint8_t,12>> evaluateChild(std::span<const uint8_t> curve,uint32_t tick,uint16_t seed,uint32_t life) {
    Bytes b{curve}; b.check(0,36);
    auto current=discreteTime(b,tick,life),next=discreteTime(b,tick+1,life);
    const unsigned flags=b.u8(4),loops=b.u8(5),length=b.u16(8);
    std::vector<std::array<uint8_t,12>> result;
    const size_t count=b.u16(32); b.check(36,count*24);
    auto create=[&](size_t index) {
        size_t key=36+index*24,value=key+12;
        const unsigned type=b.u8(key+2);
        if (type) {
            if (!(type&2)) throw std::runtime_error("Invalid range child key");
            const size_t table=32+size_t(b.u32(12))+b.u32(16);
            const unsigned entries=b.u16(table);
            if (!entries) return;
            const uint16_t random=keySeed(seed,b.u16(6),current.loop,b.u16(value))>>16;
            value=table+4+size_t(random%entries)*12;
        }
        b.check(value,12);
        std::array<uint8_t,12> data; std::copy_n(curve.data()+value,12,data.begin());
        result.push_back(data);
    };
    auto forward=[&](float first,float end) {
        for (size_t i=0;i<count;++i) { const auto frame=b.u16(36+i*24); if (frame>=first && frame<end) create(i); }
    };
    auto backward=[&](float first,float end) {
        for (size_t i=count;i>0;--i) { const auto frame=b.u16(36+(i-1)*24); if (frame<=first && frame>end) create(i-1); }
    };
    if (current.loop==next.loop) {
        if (current.frame==next.frame) {
            // The last zero endpoint of an even ping-pong sequence fires once.
            bool endpoint=false;
            if (length>=2 && (flags&64) && !(flags&32) && loops>=2 && !(loops&1)) {
                if (flags&128) endpoint=tick==life-1;
                else endpoint=tick==loops*(length-1);
            }
            if (endpoint) forward(0,1);
        } else if (current.frame<next.frame) forward(current.frame,next.frame);
        else backward(current.frame,next.frame);
    } else if (!(flags&64)) {
        forward(current.frame,65536);
        for (uint32_t loop=current.loop+1;loop<next.loop;++loop) forward(0,65536);
        forward(0,next.frame);
    } else {
        if (current.loop&1) backward(current.frame,0);
        else forward(current.frame,float(length-1));
        // The original evaluator keeps the starting direction and random seed
        // for complete loops crossed by a single fitted-time update.
        for (uint32_t loop=current.loop+1;loop<next.loop;++loop) {
            if (current.loop&1) backward(65535,0);
            else forward(0,float(length-1));
        }
        if (next.loop&1) backward(65535,next.frame);
        else forward(0,next.frame);
    }
    return result;
}
}
