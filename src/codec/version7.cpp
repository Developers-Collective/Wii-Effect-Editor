#include "version7.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace breff::codec {
Bytes packV7Emitter(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    if(bytes.size()!=340 || r.at(4,4)!=332)throw std::runtime_error("Unexpected canonical emitter size");
    constexpr size_t draw=8+0x94;
    Writer w;w.append(r.slice(0,draw+8));w.append(r.slice(draw+12,0x60-12));
    w.append(r.slice(draw+0x60,8));w.append(r.slice(draw+0x70,bytes.size()-draw-0x70));
    w.patch(4,320,4);return std::move(w.bytes);
}
Bytes expandV7Emitter(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    if(bytes.size()!=328 || r.at(4,4)!=320)throw std::runtime_error("Unexpected v7 emitter size");
    // v7 draw settings start at 8+0x94. They omit the four stage
    // texture selectors and the separate eight-byte alpha routing block.
    constexpr size_t draw=8+0x94;
    Writer w;w.append(r.slice(0,draw+8));
    // Stage zero uses texture 1; subsequent stages use texture 2.
    w.integer(0,1);w.integer(1,1);w.integer(1,1);w.integer(1,1);
    w.append(r.slice(draw+8,0x5c-8));
    w.append(r.slice(draw+0x5c,8));
    w.append(r.slice(draw+0x5c,8));
    w.append(r.slice(draw+0x64,bytes.size()-(draw+0x64)));
    w.patch(4,332,4);return std::move(w.bytes);
}
Bytes expandV7Animation(std::span<const uint8_t> bytes) {
    Reader r(bytes);r.check(0,32);
    const unsigned family=r.at(2,1),kind=r.at(1,1);
    unsigned newFamily=family,newKind=0;
    if(family==0 && kind<10) {
        constexpr unsigned kinds[]={0,3,4,7,8,11,12,15,119,120};newKind=kinds[kind];
    } else if(family==3) {
        switch(kind) {
        case 10:newKind=16;break;case 11:newKind=24;break;
        case 17:newKind=44;break;case 18:newKind=68;break;case 19:newKind=80;break;
        case 20:newKind=52;break;case 21:newKind=72;break;case 22:newKind=88;break;
        case 23:newKind=60;break;case 24:newKind=76;break;case 25:newKind=96;break;
        case 32:case 33:case 34:case 35:case 36:case 38:case 39:
            newFamily=7;newKind=kind-32;break;
        case 64:case 65:case 66:case 67:case 68:case 69:case 70:case 71:case 72:case 73: {
            constexpr unsigned kinds[]={44,124,136,112,72,76,80,84,92,8};
            newFamily=11;newKind=kinds[kind-64];break;
        }
        default:throw std::runtime_error("Unverified v7 F32 target "+std::to_string(kind));
        }
    } else if(family==6 && kind==12)newKind=32;
    else if(family==4 && kind>=14 && kind<=16)newKind=104+4*(kind-14);
    else if(family==5 && kind==26)newKind=0;
    else throw std::runtime_error("Unverified v7 animation target "+std::to_string(family)+":"+std::to_string(kind));
    std::array<Bytes,5> tables;size_t offset=32;
    for(unsigned i=0;i<5;++i){auto size=r.at(12+4*i,4);auto part=r.slice(offset,size);tables[i]=Bytes(part.begin(),part.end());offset+=size;}
    if(offset!=bytes.size())throw std::runtime_error("Invalid v7 animation table sizes");
    if(newFamily==7) {
        Reader info(tables[4]);Writer converted;
        if(kind==33) {
            if(info.size()!=4)throw std::runtime_error("Invalid v7 speed field info");
            converted.integer(2,1);converted.zeros(3);converted.append(tables[4]);
        } else if(kind==39) {
            if(info.size()!=12)throw std::runtime_error("Invalid v7 random field info");
            converted.integer(2,1);converted.zeros(2);converted.integer(info.at(10,1),1);
            converted.append(info.slice(0,10));converted.zeros(2);
        } else {
            const unsigned floats=kind==35?5:kind==36?6:4;
            if(info.size()!=floats*4+4)throw std::runtime_error("Invalid v7 field info size");
            converted.append(info.slice(floats*4,4));converted.append(info.slice(0,floats*4));
            if(kind==32) {
                // v7 stores a direction vector, while v11 stores Euler angles.
                // Brawl animates only the power scalar of this field.
                if(r.at(3,1)&~1u)throw std::runtime_error("Invalid v7 gravity component mask");
                const float x=std::bit_cast<float>(uint32_t(info.at(4,4))),y=std::bit_cast<float>(uint32_t(info.at(8,4))),z=std::bit_cast<float>(uint32_t(info.at(12,4)));
                const float length=std::sqrt(x*x+y*y+z*z);
                auto realPatch=[](Writer& w,size_t p,float v){w.patch(p,std::bit_cast<uint32_t>(v),4);};
                realPatch(converted,4,std::bit_cast<float>(uint32_t(info.at(0,4)))*length);
                realPatch(converted,8,length?std::asin(std::clamp(z/length,-1.f,1.f)):0.f);
                realPatch(converted,12,0.f);realPatch(converted,16,std::atan2(-x,y));
                if(length!=1.f && r.at(3,1))for(unsigned table=0;table<3;++table) {
                    if(tables[table].empty())continue;
                    Reader old(tables[table]);Writer scaled;scaled.append(tables[table]);
                    auto scale=[&](size_t p){realPatch(scaled,p,std::bit_cast<float>(uint32_t(old.at(p,4)))*length);};
                    if(table==0 && r.at(0,1)==0xab)for(size_t i=0;i<r.at(8,2);++i)scale(i*4);
                    else if(table==0)for(size_t i=0;i<old.at(0,2);++i){const size_t p=4+i*16;if(old.at(p+2,1)==0)scale(p+12);}
                    else for(size_t i=0;i<old.at(0,2);++i){scale(4+i*8);scale(8+i*8);}
                    tables[table]=std::move(scaled.bytes);
                }
            }
        }
        tables[4]=std::move(converted.bytes);
    }
    if(family==5) {
        // Brawl's child creator indexes ten-byte parameters: eight inheritance
        // bytes followed by the name index. v11 inserts two alpha source bytes.
        for(unsigned table:{0u,2u}) {
            if(tables[table].empty())continue;
            Reader source(tables[table]);Writer result;const auto count=source.integer(2);
            source.skip(2);result.integer(count,2);result.zeros(2);
            for(size_t i=0;i<count;++i) {
                const bool randomKey=table==0 && source.at(source.position+2,1)==2;
                if(table==0){result.append(source.slice(source.position,12));source.skip(12);}
                result.append(source.slice(source.position,8));source.skip(8);
                result.integer(randomKey?0:1,1);result.integer(randomKey?0:2,1);
                result.append(source.slice(source.position,2));source.skip(2);
            }
            source.align(4);
            if(source.position!=source.size())throw std::runtime_error("Invalid v7 child table stride");
            result.align(4);tables[table]=std::move(result.bytes);
        }
    }
    Writer w;w.append(r.slice(0,12));w.patch(1,newKind,1);w.patch(2,newFamily,1);
    for(const auto& table:tables)w.integer(table.size(),4);
    for(const auto& table:tables)w.append(table);
    return std::move(w.bytes);
}
Bytes packV7Animation(std::span<const uint8_t> bytes) {
    Reader r(bytes);r.check(0,32);
    const unsigned family=r.at(2,1),kind=r.at(1,1);
    unsigned oldFamily=family,oldKind=256;
    if(family==0) {
        constexpr unsigned kinds[]={0,3,4,7,8,11,12,15,119,120};
        for(unsigned i=0;i<10;++i)if(kinds[i]==kind)oldKind=i;
    } else if(family==3) {
        constexpr unsigned kinds[]={16,24,44,68,80,52,72,88,60,76,96};
        constexpr unsigned old[]={10,11,17,18,19,20,21,22,23,24,25};
        for(unsigned i=0;i<11;++i)if(kinds[i]==kind)oldKind=old[i];
    } else if(family==6 && kind==32)oldKind=12;
    else if(family==4 && (kind==104 || kind==108 || kind==112))oldKind=14+(kind-104)/4;
    else if(family==5 && kind==0)oldKind=26;
    else if(family==7 && (kind<=4 || kind==6 || kind==7)){oldFamily=3;oldKind=32+kind;}
    else if(family==11) {
        constexpr unsigned kinds[]={44,124,136,112,72,76,80,84,92,8};
        for(unsigned i=0;i<10;++i)if(kinds[i]==kind){oldFamily=3;oldKind=64+i;}
    }
    if(oldKind==256)throw std::runtime_error("No v7 representation for animation target "+std::to_string(family)+":"+std::to_string(kind));
    std::array<Bytes,5> tables;size_t offset=32;
    for(unsigned i=0;i<5;++i){auto size=r.at(12+4*i,4);auto part=r.slice(offset,size);tables[i]=Bytes(part.begin(),part.end());offset+=size;}
    if(offset!=bytes.size())throw std::runtime_error("Invalid animation table sizes");
    if(family==7) {
        Reader info(tables[4]);Writer result;
        if(kind==1){result.append(info.slice(4,4));}
        else if(kind==7){result.append(info.slice(4,10));result.integer(info.at(3,1),1);result.zeros(1);}
        else {
            const unsigned count=kind==3?5:kind==4?6:4;
            if(kind==0) {
                if(r.at(3,1)&~1u)throw std::runtime_error("v7 gravity cannot animate its direction");
                result.append(info.slice(4,4));
                const float x=std::bit_cast<float>(uint32_t(info.at(8,4))),y=std::bit_cast<float>(uint32_t(info.at(12,4))),z=std::bit_cast<float>(uint32_t(info.at(16,4)));
                const float sx=std::sin(x),cx=std::cos(x),sy=std::sin(y),cy=std::cos(y),sz=std::sin(z),cz=std::cos(z);
                result.real(sx*sy*cz-cx*sz);result.real(sx*sy*sz+cx*cz);result.real(sx*cy);
            } else result.append(info.slice(4,count*4));
            result.append(info.slice(0,4));
        }
        tables[4]=std::move(result.bytes);
    }
    if(family==5)for(unsigned table:{0u,2u}) {
        if(tables[table].empty())continue;
        Reader source(tables[table]);Writer result;const auto count=source.integer(2);
        source.skip(2);result.integer(count,2);result.zeros(2);
        for(size_t i=0;i<count;++i) {
            if(table==0){result.append(source.slice(source.position,12));source.skip(12);}
            result.append(source.slice(source.position,8));source.skip(10);
            result.append(source.slice(source.position,2));source.skip(2);
        }
        source.align(4);if(source.position!=source.size())throw std::runtime_error("Invalid child table stride");
        result.align(4);tables[table]=std::move(result.bytes);
    }
    Writer w;w.append(r.slice(0,12));w.patch(1,oldKind,1);w.patch(2,oldFamily,1);
    for(const auto& table:tables)w.integer(table.size(),4);
    for(const auto& table:tables)w.append(table);
    return std::move(w.bytes);
}
}
