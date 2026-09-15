#include "records.h"
#include <cmath>
#include <map>

namespace breff::codec {
namespace {
enum class Kind { Unsigned, Signed, Float, Bool, Enum, Flags, Padding, Record, Union, String };
struct Field {
    const char* name; Kind kind; unsigned width, before, after, count;
    const char* type; bool unroll, hidden; Json fallback;
};
using EnumValues=std::vector<std::pair<std::string,uint64_t>>;
#include "layouts.inc"

std::string unionType(const std::string& name,const Json& context) {
    if(name=="shapeParams") {
        const auto shape=context.at("shape").get<std::string>();
        if(shape=="Point") return "";
        if(shape=="Disc") return "DiscParams";
        if(shape=="Line") return "LineParams";
        if(shape=="Cube") return "CubeParams";
        return "CylinderSphereTorusParams";
    }
    if(name=="particleOptions") {
        const auto type=context.at("particleType").get<std::string>();
        if(type=="Point" || type=="Line" || type=="Free") return "PointLineFreeOptions";
        return type+"Options";
    }
    if(name=="childParams") return "AnimationChildParam";
    if(name=="collisionShapeOptions") {
        const auto shape=context.at("collisionShape").get<std::string>();
        if(shape=="Plane" || shape=="Rectangle" || shape=="Circle") return "enum:CollisionShapeOptionsPlane";
        if(shape=="Sphere")return "enum:CollisionShapeOptionsSphere";
        return "byte";
    }
    throw std::runtime_error("Unresolved record variant: "+name);
}
Json readField(const Field& field,Reader& r,const Json& context) {
    r.skip(field.before); Json value;
    switch(field.kind) {
    case Kind::Padding: value=0; break;
    case Kind::Record: value=readRecord(field.type,r); break;
    case Kind::Union: {
        auto type=unionType(field.name,context);
        if(type.empty()) { r.skip(24); value=Json::object(); }
        else if(type.starts_with("enum:")) value=enumName(type.substr(5),r.integer(1));
        else if(type=="byte") value=r.integer(1);
        else value=readRecord(type,r);
        break;
    }
    case Kind::String: throw std::runtime_error("String requires a length-delimited record");
    case Kind::Float: {
        auto v=r.real(); if(!std::isfinite(v)) throw std::runtime_error("Non-finite floating point field"); value=v; break;
    }
    case Kind::Signed: {
        auto v=r.integer(field.width); const auto shift=64-field.width*8;
        value=int64_t(v<<shift)>>shift; break;
    }
    case Kind::Bool: value=r.integer(field.width)!=0; break;
    case Kind::Enum: value=enumName(field.type,r.integer(field.width)); break;
    case Kind::Flags: {
        auto v=r.integer(field.width); value=Json::object();
        for(const auto& [name,mask]:enumValues.at(field.type)) value[name]=(v&mask)!=0;
        break;
    }
    case Kind::Unsigned: value=r.integer(field.width); break;
    }
    r.skip(field.after); return value;
}
void writeField(const Field& field,const Json& value,Writer& w,const Json& context) {
    w.zeros(field.before);
    const Json v=value.is_null()?field.fallback:value;
    switch(field.kind) {
    case Kind::Padding: break;
    case Kind::Record: writeRecord(field.type,v.is_null()?Json::object():v,w); break;
    case Kind::Union: {
        auto type=unionType(field.name,context);
        if(type.empty()) w.zeros(24);
        else if(type.starts_with("enum:")) w.integer(enumValue(type.substr(5),v),1);
        else if(type=="byte") w.integer(v.is_null()?0:v.get<unsigned>(),1);
        else writeRecord(type,v,w); break;
    }
    case Kind::String: throw std::runtime_error("String requires a length-delimited record");
    case Kind::Float: {
        float number=v.is_null()?0.f:v.get<float>();
        if(!std::isfinite(number)) throw std::runtime_error("Floating point value out of range");
        w.real(number); break;
    }
    case Kind::Signed: {
        const int64_t n=v.is_null()?0:v.get<int64_t>();
        const auto bits=field.width*8;
        if(bits<64 && (n<-(int64_t(1)<<(bits-1)) || n>((int64_t(1)<<(bits-1))-1)))
            throw std::runtime_error("Signed value out of range: "+std::string(field.name));
        w.integer(bits==64?uint64_t(n):uint64_t(n)&((uint64_t(1)<<bits)-1),field.width); break;
    }
    case Kind::Enum: w.integer(v.is_null()?0:enumValue(field.type,v),field.width); break;
    case Kind::Flags: {
        uint64_t flags=0;
        if(v.is_object()) { for(const auto& [name,mask]:enumValues.at(field.type)) if(v.value(name,false)) flags|=mask; }
        else if(!v.is_null()) flags=v.get<uint64_t>();
        w.integer(flags,field.width); break;
    }
    case Kind::Bool: w.integer(!v.is_null() && v.get<bool>(),field.width); break;
    case Kind::Unsigned:
        if(v.is_number_integer() && !v.is_number_unsigned() && v.get<int64_t>()<0) throw std::runtime_error("Negative unsigned field");
        w.integer(v.is_null()?0:v.get<uint64_t>(),field.width); break;
    }
    w.zeros(field.after);
}
}
Json enumName(const std::string& type,uint64_t value) {
    if(!enumValues.contains(type)) throw std::logic_error("Missing enum layout: "+type);
    for(const auto& [name,number]:enumValues.at(type)) if(number==value) return name;
    throw std::runtime_error("Unknown "+type+" value "+std::to_string(value));
}
uint64_t enumValue(const std::string& type,const Json& value) {
    for(const auto& [name,number]:enumValues.at(type)) if(value==name) return number;
    throw std::runtime_error("Unknown "+type+" name: "+value.dump());
}
Json enumChoices(const std::string& type,bool flags) {
    Json options=Json::array();
    for(const auto& [name,number]:enumValues.at(type)) {
        if(flags)options.push_back({{"key",name},{"label",name}});else options.push_back(name);
    }
    return flags?Json{{"kind","flags"},{"options",options}}:options;
}
Json scalarForm(const std::string& path,const std::string& label,const Json& fallback,const Json& choices) {
    const char* widget=fallback.is_boolean()?"bool":fallback.is_number_float()?"float":fallback.is_number_unsigned()?"uint":
        fallback.is_number_integer()?"int":fallback.is_string()?"string":fallback.contains("r")?"color":"flags";
    return {{"kind","value"},{"widget",widget},{"path",path},{"label",label},{"default",fallback},{"choices",choices}};
}
Json recordForm(const std::string& type,const Json& model,const std::string& path,const std::string& label) {
    Json value=model.is_object()?model:Json::object();
    // Resolve tagged unions from the format's defaults even in a sparse import.
    const auto fillTags=[&](auto&& self,const std::string& record) -> void {
        for(const auto& field:records.at(record)) {
            if(field.kind==Kind::Enum && !value.contains(field.name))
                value[field.name]=field.fallback.is_null()?enumChoices(field.type)[0]:field.fallback;
            if(field.kind==Kind::Record && field.unroll)self(self,field.type);
        }
    };
    fillTags(fillTags,type);
    Json node={{"kind","record"},{"path",path},{"label",label},{"children",Json::array()}};
    auto& children=node["children"];
    if(type=="GXColor")return scalarForm(path,label,{{"r",0},{"g",0},{"b",0},{"a",255}});
    for(const auto& field:records.at(type)) {
        if(field.hidden)continue;
        const std::string childPath=field.unroll?path:path+"/"+field.name;
        const Json current=field.unroll?value:value.value(field.name,Json());
        auto item=[&](const Json& itemValue,const std::string& itemPath,const std::string& itemLabel) -> Json {
            if(field.kind==Kind::Record)return recordForm(field.type,itemValue.is_object()?itemValue:Json::object(),itemPath,itemLabel);
            if(field.kind==Kind::Union) {
                const auto variant=unionType(field.name,value);
                if(variant.empty() || variant=="byte")return Json();
                if(variant.starts_with("enum:")) {auto options=enumChoices(variant.substr(5));return scalarForm(itemPath,itemLabel,options[0],options);}
                return recordForm(variant,itemValue.is_object()?itemValue:Json::object(),itemPath,itemLabel);
            }
            Json defaults=field.fallback;
            if(field.kind==Kind::Enum) {
                auto options=enumChoices(field.type);return scalarForm(itemPath,itemLabel,defaults.is_null()?options[0]:defaults,options);
            }
            if(field.kind==Kind::Flags) {
                defaults=Json::object();auto options=enumChoices(field.type,true);
                for(const auto& option:options["options"])defaults[option.at("key").get<std::string>()]=false;
                if(std::string(field.name)=="typeSpecificFlags") {
                    const auto shape=value.at("shape").get<std::string>();
                    auto& items=options["options"];
                    for(auto it=items.begin();it!=items.end();) {
                        const std::string key=it->at("key");
                        if(shape=="Point" || (shape=="Line")!=(key=="lineCenter"))it=items.erase(it);else ++it;
                    }
                }
                return scalarForm(itemPath,itemLabel,defaults,options);
            }
            if(defaults.is_null())defaults=field.kind==Kind::Float?Json(0.f):field.kind==Kind::Bool?Json(false):field.kind==Kind::String?Json(""):Json(0);
            auto leaf=scalarForm(itemPath,itemLabel,defaults);
            if(field.kind==Kind::Float)leaf["widget"]="float";
            if(field.kind==Kind::Unsigned || field.kind==Kind::Signed) {
                const unsigned bits=8*field.width;
                leaf["widget"]=field.kind==Kind::Unsigned?"uint":"int";
                if(field.kind==Kind::Unsigned){leaf["minimum"]=uint64_t(0);leaf["maximum"]=bits==64?UINT64_MAX:(uint64_t(1)<<bits)-1;}
                else {leaf["minimum"]=bits==64?INT64_MIN:-(int64_t(1)<<(bits-1));leaf["maximum"]=bits==64?INT64_MAX:(int64_t(1)<<(bits-1))-1;}
            }
            return leaf;
        };
        if(field.count) {
            Json list={{"kind","list"},{"path",childPath},{"label",field.name},{"fixedLength",field.count},{"maxLength",field.count},{"children",Json::array()}};
            for(unsigned i=0;i<field.count;++i)list["children"].push_back(item(current.is_array() && i<current.size()?current[i]:Json(),childPath+"/"+std::to_string(i),"["+std::to_string(i)+"]"));
            children.push_back(list);
        } else {
            auto child=item(current,childPath,field.name);if(child.is_null())continue;
            if(field.unroll)for(const auto& entry:child["children"])children.push_back(entry);else children.push_back(child);
        }
    }
    if(type=="EmitterData") {
        Json list={{"kind","list"},{"path",path+"/tevStages"},{"label","tevStages"},{"fixedLength",nullptr},{"maxLength",4},{"children",Json::array()}};
        Writer w;writeRecord("TEVStage",Json::object(),w);Reader r(w.bytes);list["default"]=readRecord("TEVStage",r);
        const auto stages=value.value("tevStages",Json::array());
        for(size_t i=0;i<stages.size();++i)list["children"].push_back(recordForm("TEVStage",stages[i],path+"/tevStages/"+std::to_string(i),"["+std::to_string(i)+"]"));children.push_back(list);
    }
    if(type=="StripeOptions" || type=="SmoothStripeOptions") {
        for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"connect","StripeConnect"},{"initialPrevAxis","StripeInitialPrevAxis"},{"texmapType","StripeTexmapType"}}) {
            const auto options=enumChoices(pair.second);children.push_back(scalarForm(path+"/"+pair.first,pair.first,options[0],options));
        }
    }
    return node;
}
void recordChoices(const std::string& type,const Json& value,const std::string& path,Json& choices) {
    for(const auto& field:records.at(type)) {
        if(field.hidden)continue;
        const auto child=field.unroll?path:path+"/"+field.name;
        const auto v=field.unroll?value:value.value(field.name,Json());
        if(field.kind==Kind::Enum || field.kind==Kind::Flags)choices[child]=enumChoices(field.type,field.kind==Kind::Flags);
        else if(field.kind==Kind::Record) {
            if(field.count) {
                choices[child]={{"kind","list"},{"fixedLength",field.count},{"maxLength",field.count},{"default",Json()}};
                for(size_t i=0;i<v.size();++i)recordChoices(field.type,v[i],child+"/"+std::to_string(i),choices);
            } else if(v.is_object())recordChoices(field.type,v,child,choices);
        } else if(field.kind==Kind::Union) {
            const auto variant=unionType(field.name,value);
            if(variant.starts_with("enum:"))choices[child]=enumChoices(variant.substr(5));
            else if(!variant.empty() && variant!="byte" && v.is_object())recordChoices(variant,v,child,choices);
        } else if(field.count)choices[child]={{"kind","list"},{"fixedLength",field.count},{"maxLength",field.count},{"default",0}};
    }
    if(type=="EmitterData") {
        const auto stages=value.value("tevStages",Json::array());
        Writer w;writeRecord("TEVStage",Json::object(),w);Reader r(w.bytes);auto defaults=readRecord("TEVStage",r);
        choices[path+"/tevStages"]={{"kind","list"},{"fixedLength",nullptr},{"maxLength",4},{"default",defaults}};
        for(size_t i=0;i<stages.size();++i)recordChoices("TEVStage",stages[i],path+"/tevStages/"+std::to_string(i),choices);
    }
    if(type=="StripeOptions" || type=="SmoothStripeOptions") {
        choices[path+"/connect"]=enumChoices("StripeConnect");choices[path+"/initialPrevAxis"]=enumChoices("StripeInitialPrevAxis");choices[path+"/texmapType"]=enumChoices("StripeTexmapType");
    }
}
Json readRecord(const std::string& type,Reader& r) {
    if(!records.contains(type)) throw std::logic_error("Missing record layout: "+type);
    Json result=Json::object();
    for(const auto& field:records.at(type)) {
        Json value;
        if(field.count) {
            value=Json::array();
            for(unsigned i=0;i<field.count;++i) value.push_back(readField(field,r,result));
        } else value=readField(field,r,result);
        if(field.unroll) result.update(value); else result[field.name]=value;
    }
    return result;
}
void writeRecord(const std::string& type,const Json& value,Writer& w) {
    for(const auto& field:records.at(type)) {
        auto it=value.find(field.name);
        const Json v=field.unroll?value:it==value.end()?Json():*it;
        if(field.count) {
            if(!v.is_null() && (!v.is_array() || v.size()!=field.count))
                throw std::runtime_error("Invalid array length: "+std::string(field.name));
            for(unsigned i=0;i<field.count;++i) writeField(field,v.is_null()?Json():v[i],w,value);
        } else writeField(field,v,w,value);
    }
}
Json publicRecord(const std::string& type,const Json& value) {
    Json result=Json::object();
    for(const auto& field:records.at(type)) {
        if(field.hidden) continue;
        auto it=value.find(field.name);
        Json v=field.unroll?value:it==value.end()?Json():*it;
        if(field.kind==Kind::Record) {
            if(field.count) for(auto& item:v) item=publicRecord(field.type,item);
            else v=publicRecord(field.type,v);
        } else if(field.kind==Kind::Union) {
            const auto variant=unionType(field.name,value);
            if(variant.empty()) v=0;
            else if(variant=="byte") continue;
            else if(!variant.starts_with("enum:")) v=publicRecord(variant,v);
        }
        if(field.unroll) result.update(v); else result[field.name]=v;
    }
    return result;
}
Json decodeEmitter(std::span<const uint8_t> bytes) {
    Reader r(bytes); auto raw=readRecord("EmitterData",r);
    if(raw.at("dataSize")!=332 || r.position!=340) throw std::runtime_error("Unexpected emitter layout");
    auto result=publicRecord("EmitterData",raw);
    result["tevStages"]=Json::array();
    const size_t count=raw.at("numTevStages");
    if(count>4) throw std::runtime_error("More than four TEV stages");
    for(size_t i=0;i<count;++i) {
        Json stage={{"texture",raw["textures"][i]}};
        for(auto key:{"colors","colorops","alphas","alphaops"}) stage.update(raw[key][i]);
        stage["constantColorSelection"]=raw["kcolors"][i]; stage["constantAlphaSelection"]=raw["kalphas"][i];
        result["tevStages"].push_back(stage);
    }
    auto& flags=result["typeSpecificFlags"];
    if(result["shape"]!="Line") flags.erase("lineCenter");
    if(result["shape"]=="Line" || result["shape"]=="Point") { flags.erase("flatDensity");flags.erase("linkedSize"); }
    if(result["particleType"]=="Stripe" || result["particleType"]=="SmoothStripe") {
        const unsigned packed=raw["particleOptions"]["type2"];
        auto& options=result["particleOptions"];
        options["connect"]=enumName("StripeConnect",packed&3);
        options["initialPrevAxis"]=enumName("StripeInitialPrevAxis",packed&24);
        options["texmapType"]=enumName("StripeTexmapType",packed&64);
    }
    return result;
}
Bytes encodeEmitter(const Json& value) {
    Json raw=value; raw["dataSize"]=332;
    const auto& stages=value.at("tevStages");
    if(stages.size()>4) throw std::runtime_error("More than four TEV stages");
    raw["numTevStages"]=stages.size();
    for(auto key:{"textures","colors","colorops","alphas","alphaops","kcolors","kalphas"}) raw[key]=Json::array();
    for(unsigned i=0;i<4;++i) {
        const Json stage=i<stages.size()?stages[i]:Json::object();
        raw["textures"].push_back(stage.value("texture",0));
        for(auto key:{"colors","colorops","alphas","alphaops"}) raw[key].push_back(stage);
        raw["kcolors"].push_back(stage.value("constantColorSelection",std::string("Constant1_1")));
        raw["kalphas"].push_back(stage.value("constantAlphaSelection",std::string("Constant1_1")));
    }
    if(raw["particleType"]=="Stripe" || raw["particleType"]=="SmoothStripe") {
        auto& options=raw["particleOptions"];
        options["type2"]=enumValue("StripeConnect",options.at("connect"))|
            enumValue("StripeInitialPrevAxis",options.at("initialPrevAxis"))|enumValue("StripeTexmapType",options.at("texmapType"));
    }
    Writer w; writeRecord("EmitterData",raw,w);
    if(w.bytes.size()!=340) throw std::logic_error("Incorrect encoded emitter size");
    return std::move(w.bytes);
}
Json decodeParticle(std::span<const uint8_t> bytes,bool includeUnboundTextures) {
    Reader r(bytes); const auto size=r.integer(4); r.check(4,size);
    Json result=Json::object();
    for(auto name:{"color1Primary","color1Secondary","color2Primary","color2Secondary"}) result[name]=readRecord("GXColor",r);
    result["particleSize"]=readRecord("VEC2",r); result["particleScale"]=readRecord("VEC2",r); result["particleRotation"]=readRecord("VEC3",r);
    Json scales=Json::array(),rotations=Json::array(),translations=Json::array();
    for(int i=0;i<3;++i) scales.push_back(readRecord("VEC2",r));
    for(int i=0;i<3;++i) rotations.push_back(r.real());
    for(int i=0;i<3;++i) translations.push_back(readRecord("VEC2",r));
    r.skip(12); const auto wrap=r.integer(2),reverse=r.integer(1);
    result["alphaCompareValue0"]=r.integer(1); result["alphaCompareValue1"]=r.integer(1);
    uint64_t random[3]; float offset[3];
    for(auto& v:random) v=r.integer(1); for(auto& v:offset) v=r.real();
    const char* keys[]={"texture1","texture2","textureInd"};
    for(int i=0;i<3;++i) {
        auto name=r.name(); Json t=Json::object();
        if(includeUnboundTextures || !name.empty() || offset[i]!=0.f || random[i]!=0) t={{"name",name},{"scale",scales[i]},{"rotation",rotations[i]},{"translation",translations[i]},
            {"wrapS",enumName("GXTexWrapMode",(wrap>>(i*4))&3)},{"wrapT",enumName("GXTexWrapMode",(wrap>>(i*4+2))&3)},
            {"reverseMode",enumName("ReverseMode",(reverse>>(i*2))&3)},{"rotationOffsetRandom",random[i]},{"rotationOffset",offset[i]}};
        result[keys[i]]=t;
    }
    r.align(4); if(r.position!=size+4) throw std::runtime_error("Unexpected particle size");
    return result;
}
Bytes encodeParticle(const Json& value) {
    Writer w; w.zeros(4);
    for(auto name:{"color1Primary","color1Secondary","color2Primary","color2Secondary"}) writeRecord("GXColor",value.at(name),w);
    writeRecord("VEC2",value.at("particleSize"),w);writeRecord("VEC2",value.at("particleScale"),w);writeRecord("VEC3",value.at("particleRotation"),w);
    Json textures=Json::array();
    for(auto key:{"texture1","texture2","textureInd"}) textures.push_back(value.value(key,Json::object()));
    for(const auto& t:textures) writeRecord("VEC2",t.value("scale",Json{{"x",1.f},{"y",1.f}}),w);
    for(const auto& t:textures) w.real(t.value("rotation",0.f));
    for(const auto& t:textures) writeRecord("VEC2",t.value("translation",Json::object()),w);
    unsigned wrap=0,reverse=0;
    for(unsigned i=0;i<3;++i) {
        const auto& t=textures[i]; wrap|=unsigned(enumValue("GXTexWrapMode",t.value("wrapS",Json("Clamp"))))<<(i*4);
        wrap|=unsigned(enumValue("GXTexWrapMode",t.value("wrapT",Json("Clamp"))))<<(i*4+2);
        reverse|=unsigned(enumValue("ReverseMode",t.value("reverseMode",Json("NoReverse"))))<<(i*2);
    }
    w.zeros(12); w.integer(wrap,2);w.integer(reverse,1);
    w.integer(value.at("alphaCompareValue0"),1);w.integer(value.at("alphaCompareValue1"),1);
    for(const auto& t:textures) w.integer(t.value("rotationOffsetRandom",0u),1);
    for(const auto& t:textures) w.real(t.value("rotationOffset",0.f));
    for(const auto& t:textures) w.name(t.value("name",std::string()));
    w.align(4);w.patch(0,w.bytes.size()-4,4);return std::move(w.bytes);
}
}
