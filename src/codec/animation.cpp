#include "animation.h"
#include <array>
#include <map>

namespace breff::codec {
namespace {
struct Target { std::string name; unsigned family,kind; std::string components; };
#include "targets.inc"
const Target& targetFor(unsigned family,unsigned kind) {
    for(const auto& t:targets) if(t.family==family && t.kind==kind) return t;
    throw std::runtime_error("Unknown animation target "+std::to_string(family)+":"+std::to_string(kind));
}
const Target& targetFor(const std::string& name) {
    for(const auto& t:targets) if(t.name==name) return t;
    throw std::runtime_error("Unknown animation target "+name);
}
std::vector<std::string> componentNames(const Target& t,const Json& emitter) {
    if(t.name=="EmitterSpeedNormal")return {"t","diffusion"};
    auto key=t.components;
    if(key.empty()) key=shapeComponents.at(emitter.at("shape").get<std::string>());
    return key.empty()?std::vector<std::string>():components.at(key);
}
Json flags(unsigned value) {
    Json result=Json::object();
    const char* names[]={"syncRand","stop","emitterTiming","loopInfinitely","loopByRepeating","fitting"};
    for(unsigned i=0;i<6;++i) result[names[i]]=(value&(4<<i))!=0;
    return result;
}
unsigned flags(const Json& value) {
    unsigned result=0;
    const char* names[]={"syncRand","stop","emitterTiming","loopInfinitely","loopByRepeating","fitting"};
    for(unsigned i=0;i<6;++i) if(value.value(names[i],false)) result|=4<<i;
    return result;
}
Json curve(unsigned value) {
    Json result={{"interpolation",enumName("KeyCurveType",value&3)}};
    if((value&3)==1) result["slopeAdjust"]={{"startSlopeAdjust",(value&4)!=0},{"endSlopeAdjust",(value&8)!=0}};
    return result;
}
unsigned curve(const Json& value) {
    unsigned result=unsigned(enumValue("KeyCurveType",value.at("interpolation")));
    auto slope=value.value("slopeAdjust",Json::object());
    if(slope.value("startSlopeAdjust",false)) result|=4;
    if(slope.value("endSlopeAdjust",false)) result|=8;
    return result;
}
Json number(Reader& r,bool byte) { return byte?Json(r.integer(1)):Json(r.real()); }
void number(Writer& w,const Json& value,bool byte) { if(byte) w.integer(value.get<uint64_t>(),1);else w.real(value.get<float>()); }
struct Tables {
    std::array<Bytes,5> bytes;
    explicit Tables(Reader& reader) {
        size_t offset=32;
        for(unsigned i=0;i<5;++i) {
            auto size=reader.at(12+4*i,4); auto part=reader.slice(offset,size);
            bytes[i].assign(part.begin(),part.end());offset+=size;
        }
        if(offset!=reader.size()) throw std::runtime_error("Animation table sizes do not match resource length");
    }
    Tables()=default;
};
struct Names {
    std::vector<std::string> values;
    Names()=default;
    explicit Names(const Bytes& bytes) {
        if(bytes.empty())return;
        Reader r(bytes);auto count=r.integer(2);r.skip(2+4*count);
        for(size_t i=0;i<count;++i)values.push_back(r.name());
    }
    const std::string& get(size_t i) const {
        if(i>=values.size())throw std::runtime_error("Animation name index out of bounds");return values[i];
    }
    size_t add(const std::string& name) {
        for(size_t i=0;i<values.size();++i)if(values[i]==name)return i;
        values.push_back(name);return values.size()-1;
    }
    Bytes encode() const {
        Writer w;w.integer(values.size(),2);w.zeros(2+4*values.size());for(const auto& name:values)w.name(name);w.align(4);return std::move(w.bytes);
    }
};
Json childParam(Reader& r,const Names& names) {
    auto raw=readRecord("AnimationChildParam",r);auto value=publicRecord("AnimationChildParam",raw);
    value["name"]=names.get(raw.at("nameIdx"));return value;
}
void childParam(Writer& w,const Json& value,Names& names) {
    auto raw=value;raw["nameIdx"]=names.add(value.value("name",std::string()));writeRecord("AnimationChildParam",raw,w);
}
Json textureParam(Reader& r,const Names& names) {
    auto wrap=r.integer(1),reverse=r.integer(1),index=r.integer(2);
    return {{"textureName",names.get(index)},{"wrapS",enumName("GXTexWrapMode",wrap&3)},
        {"wrapT",enumName("GXTexWrapMode",(wrap>>2)&3)},{"reverseMode",enumName("ReverseMode",reverse)}};
}
void textureParam(Writer& w,const Json& value,Names& names) {
    w.integer(enumValue("GXTexWrapMode",value.at("wrapS"))|(enumValue("GXTexWrapMode",value.at("wrapT"))<<2),1);
    w.integer(enumValue("ReverseMode",value.at("reverseMode")),1);w.integer(names.add(value.at("textureName")),2);
}
Json readReferences(const Tables& tables,bool child) {
    Names names(tables.bytes[3]);Json result={{"frames",Json::array()},{"randomPool",Json::array()}};
    Json ranges=Json::array();
    if(!child && !tables.bytes[1].empty()) {
        Reader r(tables.bytes[1]);auto count=r.integer(2);r.skip(2);
        for(size_t i=0;i<count;++i) {
            auto entry=textureParam(r,names);auto flip=r.integer(1);r.skip(3);
            entry["flipRandom"]={{"flipHorizontal",(flip&1)!=0},{"flipVertical",(flip&2)!=0}};ranges.push_back(entry);
        }
    }
    Reader r(tables.bytes[0]);auto count=r.integer(2);r.skip(2);
    for(size_t i=0;i<count;++i) {
        Json frame={{"frame",r.integer(2)}};auto type=r.integer(1);r.skip(9);frame["valueType"]=enumName("KeyType",type);
        if(type==2)r.skip(child?12:4);
        else if(child)frame.update(childParam(r,names));
        else if(type==0)frame.update(textureParam(r,names));
        else {auto index=r.integer(2);r.skip(2);if(index>=ranges.size())throw std::runtime_error("Texture range index out of bounds");frame.update(ranges[index]);}
        result["frames"].push_back(frame);
    }
    if(!tables.bytes[2].empty()) {
        Reader pool(tables.bytes[2]);count=pool.integer(2);pool.skip(2);
        for(size_t i=0;i<count;++i) result["randomPool"].push_back(child?childParam(pool,names):textureParam(pool,names));
    }
    return result;
}
Tables writeReferences(const Json& value,bool child) {
    Tables tables;Names names;Writer key,range,pool;const auto& frames=value.at("frames");
    key.integer(frames.size(),2);key.zeros(2);range.zeros(4);size_t rangeCount=0,randomIndex=0;
    for(size_t i=0;i<frames.size();++i) {
        const auto& frame=frames[i];auto type=enumValue("KeyType",frame.at("valueType"));
        key.integer(frame.at("frame"),2);key.integer(type,1);key.zeros(9);
        if(type==2) {key.integer(child?i:randomIndex++,2);key.zeros(child?10:2);}
        else if(child)childParam(key,frame,names);
        else if(type==0)textureParam(key,frame,names);
        else {
            key.integer(rangeCount++,2);key.zeros(2);textureParam(range,frame,names);
            const auto flip=frame.value("flipRandom",Json::object());range.integer((flip.value("flipHorizontal",false)?1:0)|(flip.value("flipVertical",false)?2:0),1);range.zeros(3);
        }
    }
    const auto random=value.value("randomPool",Json::array());
    if(!random.empty()) {
        pool.integer(random.size(),2);pool.zeros(2);
        for(const auto& entry:random) {if(child)childParam(pool,entry,names);else textureParam(pool,entry,names);}
        tables.bytes[2]=std::move(pool.bytes);
    }
    if(rangeCount) {range.patch(0,rangeCount,2);tables.bytes[1]=std::move(range.bytes);}
    tables.bytes[0]=std::move(key.bytes);tables.bytes[3]=names.encode();return tables;
}
Json readNumeric(const Tables& tables,const std::vector<std::string>& names,unsigned mask,bool byte,bool rotate,bool baked,unsigned frames) {
    std::vector<unsigned> enabled;for(unsigned i=0;i<names.size();++i) if(mask&(1<<i)) enabled.push_back(i);
    auto ranges=[&](const Bytes& bytes) {
        Json result=Json::array(); if(bytes.empty()) return result;
        Reader r(bytes); auto count=r.integer(2); r.skip(2);
        for(size_t i=0;i<count;++i) {
            Json entry=Json::object();
            for(auto index:enabled) entry[names[index]]=Json::array({number(r,byte),number(r,byte)});
            if(rotate) { entry["randomRotationDirection"]=r.integer(1)!=0;r.skip(3); }
            result.push_back(entry);
        }
        return result;
    };
    Reader key(tables.bytes[0]);Json result=Json::object();
    if(baked) {
        result["frames"]=Json::array();
        for(unsigned i=0;i<frames;++i) {
            Json frame=Json::object();for(auto index:enabled) frame[names[index]]=number(key,byte);
            result["frames"].push_back(frame);
        }
        return result;
    }
    auto range=ranges(tables.bytes[1]);result["keyFrames"]=Json::array();result["randomPool"]=ranges(tables.bytes[2]);
    // The original JSON includes empty lists for inactive random-pool components.
    if(!rotate) for(auto& entry:result["randomPool"]) for(const auto& name:byte?bytePoolFields:floatPoolFields)
        if(!entry.contains(name))entry[name]=Json::array();
    const auto count=key.integer(2);key.skip(2);
    for(size_t i=0;i<count;++i) {
        const size_t start=key.position; Json frame={{"frame",key.integer(2)}};
        auto kind=key.integer(1);key.skip(1);frame["valueType"]=enumName("KeyType",kind);
        uint8_t curves[8];for(auto& c:curves)c=uint8_t(key.integer(1));
        const auto index=kind?key.at(key.position,2):0;
        if(kind==1 && index>=range.size()) throw std::runtime_error("Animation range index out of bounds");
        for(auto component:enabled) {
            auto value=curve(curves[component]);
            if(kind==0) value["value"]=number(key,byte);
            else if(kind==1) value["range"]=range[index].at(names[component]);
            if(names[component]=="t") frame.update(value);else frame[names[component]]=value;
        }
        if(rotate && kind==1) frame["randomRotationDirection"]=range[index].at("randomRotationDirection");
        const size_t payload=byte?(enabled.size()+1)/2*2:enabled.size()*4;
        key.position=start+12+payload;key.check(key.position,0);
        result["keyFrames"].push_back(frame);
    }
    return result;
}
Tables writeNumeric(const Json& value,const std::vector<std::string>& names,unsigned mask,bool byte,bool rotate,bool baked) {
    std::vector<unsigned> enabled;for(unsigned i=0;i<names.size();++i)if(mask&(1<<i))enabled.push_back(i);
    Writer keys,range,random;Tables result;
    if(baked) {
        for(const auto& frame:value.at("frames")) for(auto i:enabled) number(keys,frame.at(names[i]),byte);
        keys.align(4);result.bytes[0]=std::move(keys.bytes);return result;
    }
    const auto& frames=value.at("keyFrames");keys.integer(frames.size(),2);keys.zeros(2);
    size_t rangeCount=0,randomIndex=0;range.zeros(4);
    for(const auto& frame:frames) {
        const size_t start=keys.bytes.size();keys.integer(frame.at("frame"),2);
        const auto kind=enumValue("KeyType",frame.at("valueType"));keys.integer(kind,1);keys.zeros(1);
        std::array<uint8_t,8> curves{};
        auto component=[&](unsigned i)->const Json& { return names[i]=="t"?frame:frame.at(names[i]); };
        for(auto i:enabled) curves[i]=uint8_t(curve(component(i)));keys.append(curves);
        if(kind==0) for(auto i:enabled) number(keys,component(i).at("value"),byte);
        else if(kind==1) {
            keys.integer(rangeCount++,2);
            for(auto i:enabled) {
                const auto& pair=component(i).at("range");
                if(!pair.is_array() || pair.size()!=2)throw std::runtime_error("An animation range needs two values");
                number(range,pair[0],byte);number(range,pair[1],byte);
            }
            if(rotate) {range.integer(frame.value("randomRotationDirection",false),1);range.zeros(3);}
        } else keys.integer(randomIndex++,2);
        const size_t payload=byte?(enabled.size()+1)/2*2:enabled.size()*4;
        if(keys.bytes.size()>start+12+payload) throw std::runtime_error("Animation has no enabled components");
        keys.zeros(start+12+payload-keys.bytes.size());
    }
    keys.align(4);result.bytes[0]=std::move(keys.bytes);
    if(rangeCount) { range.patch(0,rangeCount,2);range.align(4);result.bytes[1]=std::move(range.bytes); }
    const auto pool=value.value("randomPool",Json::array());
    if(!pool.empty()) {
        random.integer(pool.size(),2);random.zeros(2);
        for(const auto& entry:pool) {
            for(auto i:enabled) { const auto& pair=entry.at(names[i]); if(pair.size()!=2)throw std::runtime_error("Random range needs two values");number(random,pair[0],byte);number(random,pair[1],byte); }
            if(rotate) {random.integer(entry.value("randomRotationDirection",false),1);random.zeros(3);}
        }
        random.align(4);result.bytes[2]=std::move(random.bytes);
    }
    return result;
}
}
Json decodeAnimation(std::span<const uint8_t> bytes,const Json& emitter,bool init) {
    Reader r(bytes); r.check(0,32);
    const auto magic=r.at(0,1),kind=r.at(1,1),family=r.at(2,1),mask=r.at(3,1),process=r.at(4,1);
    if(magic!=0xab && magic!=0xac) throw std::runtime_error("Invalid animation magic");
    const bool baked=magic==0xab;const auto& target=targetFor(unsigned(family),unsigned(kind));
    const auto names=componentNames(target,emitter);
    Json result={{"target",target.name}};
    if(!(names.size()==1 && names[0]=="t") && !(target.name=="EmitterSpeedNormal" && mask==1)) { result["subTargets"]=Json::object();for(unsigned i=0;i<names.size();++i)result["subTargets"][names[i]]=(mask&(1<<i))!=0; }
    result["isInit"]=init;result["isBaked"]=baked;result["processFlag"]=flags(unsigned(process));
    if(!(process&32))result["loopCount"]=r.at(5,1);
    result["randomSeed"]=r.at(6,2);if(!baked)result["frameCount"]=r.at(8,2);
    Tables tables(r);
    if(family==0 || family==3 || family==6 || family==11)
        result.update(readNumeric(tables,names,unsigned(mask),family==0,family==6,baked,unsigned(r.at(8,2))));
    else if(family==4 || family==5) result.update(readReferences(tables,family==5));
    else if(family==7) {
        if(mask) result.update(readNumeric(tables,names,unsigned(mask),false,false,baked,unsigned(r.at(8,2))));
        Reader info(tables.bytes[4]);Json object={{"space",enumName("Space",info.integer(1))},{"addTarget",enumName("AddTarget",info.integer(1))}};
        info.skip(1);auto options=info.integer(1);
        object["option"]={{"randomSaveVelocity",(options&1)!=0},{"randomAllDirection",(options&2)!=0},{"randomEnableX",(options&4)!=0},{"randomEnableY",(options&8)!=0},{"randomEnableZ",(options&16)!=0}};
        object.update(readRecord("Info"+target.name,info));result["info"]=object;
    } else if(family==2) {
        if(mask)result.update(readNumeric(tables,names,unsigned(mask),false,false,false,unsigned(r.at(8,2))));
        Reader info(tables.bytes[4]);auto raw=readRecord("AnimationPostFieldInfo",info);auto object=publicRecord("AnimationPostFieldInfo",raw);
        Names refs(tables.bytes[3]);
        if(refs.values.empty())object["childParams"]=Json::object();else object["childParams"]["name"]=refs.get(raw.at("childParams").at("nameIdx"));
        result["info"]=object;
    }
    else throw std::runtime_error("Animation family not implemented in C++ codec: "+std::to_string(family));
    return result;
}
Bytes encodeAnimation(const Json& value,const Json& emitter) {
    const auto& target=targetFor(value.at("target").get<std::string>());const auto names=componentNames(target,emitter);
    unsigned mask=0;
    if(names.size()==1 && names[0]=="t")mask=1;
    else for(unsigned i=0;i<names.size();++i) if(value.value("subTargets",Json{{"t",true}}).value(names[i],false))mask|=1<<i;
    bool baked=value.value("isBaked",false);Tables tables;
    if(target.family==0 || target.family==3 || target.family==6 || target.family==11)
        tables=writeNumeric(value,names,mask,target.family==0,target.family==6,baked);
    else if(target.family==4 || target.family==5) tables=writeReferences(value,target.family==5);
    else if(target.family==7) {
        if(mask)tables=writeNumeric(value,names,mask,false,false,baked);
        const auto& object=value.at("info");Writer info;info.integer(enumValue("Space",object.at("space")),1);info.integer(enumValue("AddTarget",object.at("addTarget")),1);info.zeros(1);
        unsigned options=0;const auto flags=object.value("option",Json::object());
        const char* keys[]={"randomSaveVelocity","randomAllDirection","randomEnableX","randomEnableY","randomEnableZ"};
        for(unsigned i=0;i<5;++i)if(flags.value(keys[i],false))options|=1<<i;
        info.integer(options,1);writeRecord("Info"+target.name,object,info);tables.bytes[4]=std::move(info.bytes);
    } else if(target.family==2) {
        if(mask)tables=writeNumeric(value,names,mask,false,false,false);
        auto object=value.at("info");Names refs;const auto name=object.at("childParams").value("name",std::string());
        object["childParams"]["nameIdx"]=name.empty()?0:refs.add(name);
        Writer info;writeRecord("AnimationPostFieldInfo",object,info);tables.bytes[4]=std::move(info.bytes);
        if(!refs.values.empty())tables.bytes[3]=refs.encode();
    }
    else throw std::runtime_error("Animation family not implemented in C++ codec: "+std::to_string(target.family));
    Writer w;w.integer(baked?0xab:0xac,1);w.integer(target.kind,1);w.integer(target.family,1);w.integer(mask,1);
    w.integer(flags(value.value("processFlag",Json::object())),1);w.integer(value.value("loopCount",0u),1);
    w.integer(value.value("randomSeed",0u),2);w.integer(baked?value.at("frames").size():value.value("frameCount",1u),2);w.zeros(2);
    for(const auto& table:tables.bytes)w.integer(table.size(),4);for(const auto& table:tables.bytes)w.append(table);
    return std::move(w.bytes);
}
Json defaultAnimation(const std::string& name,const Json& emitter) {
    const auto& target=targetFor(name);const auto names=componentNames(target,emitter);
    Json result={{"target",name},{"isInit",false},{"isBaked",false},{"processFlag",flags(0u)},{"loopCount",0},{"randomSeed",0},{"frameCount",1}};
    if(!(names.size()==1 && names[0]=="t") && name!="EmitterSpeedNormal") {result["subTargets"]=Json::object();for(const auto& component:names)result["subTargets"][component]=true;}
    if(target.family==4 || target.family==5) {
        Json frame={{"frame",0},{"valueType","Fixed"}};
        if(target.family==4)frame.update({{"textureName",""},{"wrapS","Clamp"},{"wrapT","Clamp"},{"reverseMode","NoReverse"}});
        else {
            Writer w;writeRecord("AnimationChildParam",Json::object(),w);Reader r(w.bytes);frame.update(publicRecord("AnimationChildParam",readRecord("AnimationChildParam",r)));frame["name"]="";
        }
        result["frames"]=Json::array({frame});
    } else {
        Json frame={{"frame",0},{"valueType","Fixed"}};
        for(const auto& component:names) {
            if(name=="EmitterSpeedNormal" && component=="diffusion")continue;
            Json value={{"interpolation","Linear"},{"value",name.find("Size")!=std::string::npos || name.find("Scale")!=std::string::npos?1.f:0.f}};
            if(component=="t")frame.update(value);else frame[component]=value;
        }
        result["keyFrames"]=Json::array({frame});
        if(target.family==7) {
            Writer w;writeRecord("Info"+name,Json::object(),w);Reader r(w.bytes);
            result["info"]={{"space","Global"},{"addTarget","Velocity"},{"option",{{"randomSaveVelocity",false},{"randomAllDirection",false},{"randomEnableX",false},{"randomEnableY",false},{"randomEnableZ",false}}}};
            result["info"].update(readRecord("Info"+name,r));
        } else if(target.family==2) {
            Json raw={{"collisionShape","Plane"},{"collisionShapeOptions","XZ"},{"childParams",Json::object()}};
            Writer w;writeRecord("AnimationPostFieldInfo",raw,w);Reader r(w.bytes);
            result["info"]=publicRecord("AnimationPostFieldInfo",readRecord("AnimationPostFieldInfo",r));result["info"]["childParams"]=Json::object();
        }
    }
    result["randomPool"]=Json::array();return result;
}
void animationChoices(const Json& value,const Json& emitter,const std::string& path,Json& choices) {
    const auto& target=targetFor(value.at("target").get<std::string>());const auto names=componentNames(target,emitter);
    Json options=Json::array();for(const auto& t:targets)options.push_back(t.name);choices[path+"/target"]=options;
    Json process=Json::array();const auto processFlags=flags(0u);for(const auto& [key,v]:processFlags.items())process.push_back({{"key",key},{"label",key}});
    choices[path+"/processFlag"]={{"kind","flags"},{"options",process}};
    if(!(names.size()==1 && names[0]=="t")) {
        options=Json::array();for(const auto& name:names)options.push_back({{"key",name},{"label",name}});
        choices[path+"/subTargets"]={{"kind","flags"},{"options",options}};
    }
    auto defaults=defaultAnimation(target.name,emitter);
    for(auto key:{"keyFrames","frames","randomPool"}) {
        const bool baked=value.value("isBaked",false);
        if(std::string(key)=="randomPool" ? baked : std::string(key)!=(baked || target.family==4 || target.family==5?"frames":"keyFrames"))continue;
        Json item=Json::object();
        if(std::string(key)=="randomPool") {
            if(target.family==4 || target.family==5){item=defaults.at("frames")[0];item.erase("frame");item.erase("valueType");}
            else for(const auto& name:names)item[name]=Json::array({0.f,0.f});
            if(target.family==6)item["randomRotationDirection"]=false;
        } else if(value.value("isBaked",false))for(const auto& name:names)item[name]=0;
        else item=defaults.at(key)[0];
        choices[path+"/"+key]={{"kind","list"},{"fixedLength",nullptr},{"maxLength",nullptr},{"default",item}};
    }
    if(target.family==7) {
        choices[path+"/info/space"]=enumChoices("Space");choices[path+"/info/addTarget"]=enumChoices("AddTarget");choices[path+"/info/option"]=enumChoices("OptionRandom",true);
        recordChoices("Info"+target.name,value.value("info",defaults.at("info")),path+"/info",choices);
    } else if(target.family==2) {
        recordChoices("AnimationPostFieldInfo",value.value("info",defaults.at("info")),path+"/info",choices);
        auto child=defaultAnimation("Child",emitter).at("frames")[0];child.erase("frame");child.erase("valueType");
        choices[path+"/info/childParams"]={{"kind","optional"},{"default",child}};
    }
}
void prepareAnimationEdit(Json& value,const Json& previous,const Json& emitter) {
    if(value.at("target")!=previous.at("target")) {
        auto replacement=defaultAnimation(value.at("target"),emitter);
        for(auto key:{"isInit","processFlag","loopCount","randomSeed","frameCount"})if(value.contains(key))replacement[key]=value[key];
        value=std::move(replacement);return;
    }
    const auto& target=targetFor(value.at("target").get<std::string>());const auto names=componentNames(target,emitter);
    const bool baked=value.value("isBaked",false);
    if(baked!=previous.value("isBaked",false)) {
        if(target.family==4 || target.family==5 || target.family==6 || target.family==2) {
            value["isBaked"]=false;return;
        }
        if(baked) {
            Json frame=Json::object();for(const auto& name:names)frame[name]=0;
            value["frames"]=Json::array({frame});value.erase("keyFrames");value.erase("randomPool");
        } else {
            const auto replacement=defaultAnimation(target.name,emitter);
            value["keyFrames"]=replacement.at("keyFrames");value["randomPool"]=Json::array();value.erase("frames");value["frameCount"]=1;
        }
    }
    if(target.family==4 || target.family==5) {
        const auto defaults=defaultAnimation(target.name,emitter).at("frames")[0];
        for(auto& frame:value["frames"])if(frame.value("valueType",std::string("Fixed"))!="Random")
            for(auto it=defaults.begin();it!=defaults.end();++it)if(!frame.contains(it.key()))frame[it.key()]=it.value();
        return;
    }
    const auto selected=value.value("subTargets",Json{{"t",true}});
    const std::string key=baked?"frames":"keyFrames";
    if(!value.contains(key))return;
    for(auto& frame:value[key])for(const auto& name:names) {
        const bool enabled=selected.value(name,false),single=name=="t";
        if(!enabled) {frame.erase(name);continue;}
        if(baked) {if(!frame.contains(name))frame[name]=0;continue;}
        if(!single && !frame.contains(name))frame[name]={{"interpolation","Linear"},{"value",0}};
        auto& component=single?frame:frame[name];
        if(!component.contains("interpolation"))component["interpolation"]="Linear";
        const auto type=frame.value("valueType",std::string("Fixed"));
        if(type=="Fixed") {if(!component.contains("value"))component["value"]=component.contains("range")?component["range"][0]:Json(0);component.erase("range");}
        else if(type=="Range") {if(!component.contains("range"))component["range"]=Json::array({component.value("value",Json(0)),0});component.erase("value");}
        else {component.erase("value");component.erase("range");}
        if(component["interpolation"]=="Hermite" && !component.contains("slopeAdjust"))component["slopeAdjust"]={{"startSlopeAdjust",false},{"endSlopeAdjust",false}};
        else if(component["interpolation"]!="Hermite")component.erase("slopeAdjust");
    }
    if(value.contains("randomPool"))for(auto& entry:value["randomPool"])for(const auto& name:names)
        if(selected.value(name,false) && (!entry.contains(name) || entry[name].empty()))entry[name]=Json::array({0,0});
}
Json animationForm(const Json& value,const Json& emitter,const std::string& path,const std::string& label) {
    const auto& target=targetFor(value.at("target").get<std::string>());const auto names=componentNames(target,emitter);
    Json node={{"kind","record"},{"path",path},{"label",label},{"children",Json::array()}}, choices=Json::object();
    animationChoices(value,emitter,path,choices);
    auto& children=node["children"];
    for(auto key:{"target","isInit","isBaked","processFlag","loopCount","randomSeed","frameCount"}) {
        Json fallback=key==std::string("target")?Json(target.name):key==std::string("isInit") || key==std::string("isBaked")?Json(false):key==std::string("processFlag")?flags(0u):Json(0);
        if(key==std::string("isBaked") && (target.family==4 || target.family==5 || target.family==6 || target.family==2))continue;
        if(key==std::string("frameCount") && value.value("isBaked",false))continue;
        children.push_back(scalarForm(path+"/"+key,key,fallback,choices.value(path+"/"+key,Json())));
    }
    const bool single=names.size()==1 && names[0]=="t",baked=value.value("isBaked",false);
    auto selected=value.value("subTargets",Json{{"t",true}});
    if(!single)children.push_back(scalarForm(path+"/subTargets","subTargets",selected,choices.at(path+"/subTargets")));
    auto childRecord=[&](const Json& object,const std::string& p,const std::string& label) {
        auto result=recordForm("AnimationChildParam",object,p,label);
        result["children"].insert(result["children"].begin(),scalarForm(p+"/name","name",""));return result;
    };
    auto textureRecord=[&](const std::string& p,const std::string& label) {
        Json result={{"kind","record"},{"path",p},{"label",label},{"children",Json::array()}};
        result["children"].push_back(scalarForm(p+"/textureName","textureName",""));
        result["children"].push_back(scalarForm(p+"/wrapS","wrapS","Clamp",enumChoices("GXTexWrapMode")));
        result["children"].push_back(scalarForm(p+"/wrapT","wrapT","Clamp",enumChoices("GXTexWrapMode")));
        result["children"].push_back(scalarForm(p+"/reverseMode","reverseMode","NoReverse",enumChoices("ReverseMode")));return result;
    };
    const auto defaults=defaultAnimation(target.name,emitter);
    const Json numericDefault=target.family==0?Json(uint64_t(0)):Json(0.f);
    const bool any=single || std::any_of(names.begin(),names.end(),[&](const auto& name){return selected.value(name,false);});
    if(any || target.family==4 || target.family==5)for(unsigned table=0;table<(baked?1u:2u);++table) {
        const std::string key=table==1?"randomPool":baked || target.family==4 || target.family==5?"frames":"keyFrames";
        const auto entries=value.value(key,Json::array());
        Json list={{"kind","list"},{"path",path+"/"+key},{"label",key},{"fixedLength",nullptr},{"maxLength",65535},{"children",Json::array()}};
        if(choices.contains(path+"/"+key))list["default"]=choices[path+"/"+key].at("default");
        else list["default"]=defaults.contains(key) && !defaults[key].empty()?defaults[key][0]:Json::object();
        for(size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i];const std::string p=path+"/"+key+"/"+std::to_string(i);
            Json item={{"kind","record"},{"path",p},{"label","["+std::to_string(i)+"]"},{"children",Json::array()}};
            const std::string valueType=entry.value("valueType",std::string("Fixed"));
            auto& fields=item["children"];
            if(!table && !baked){fields.push_back(scalarForm(p+"/frame","frame",0));fields.push_back(scalarForm(p+"/valueType","valueType","Fixed",enumChoices("KeyType")));}
            if(target.family==4 || target.family==5) {
                if(table || valueType!="Random") {
                    auto data=target.family==5?childRecord(entry,p,""):textureRecord(p,"");for(const auto& f:data["children"])fields.push_back(f);
                    if(target.family==4 && !table && valueType=="Range")fields.push_back(scalarForm(p+"/flipRandom","flipRandom",Json::object(),enumChoices("FlipRandom",true)));
                }
            } else {
                if(target.family==6 && (table || valueType=="Range"))fields.push_back(scalarForm(p+"/randomRotationDirection","randomRotationDirection",false));
                for(const auto& name:names)if(selected.value(name,false)) {
                    const auto cp=name=="t" && !table && !baked?p:p+"/"+name;
                    Json component={{"kind","record"},{"path",cp},{"label",name},{"children",Json::array()}};auto& sub=component["children"];
                    if(baked) {fields.push_back(scalarForm(cp,name,numericDefault));continue;}
                    if(!table) {
                        sub.push_back(scalarForm(cp+"/interpolation","interpolation","Linear",enumChoices("KeyCurveType")));
                        const auto current=name=="t"?entry:entry.value(name,Json::object());
                        if(current.value("interpolation",std::string("Linear"))=="Hermite")sub.push_back(scalarForm(cp+"/slopeAdjust","slopeAdjust",Json::object(),enumChoices("KeyCurveFlag",true)));
                    }
                    if(table || valueType=="Range") {
                        const auto rp=table?cp:cp+"/range";
                        sub.push_back({{"kind","list"},{"path",rp},{"label",table?name:"range"},{"fixedLength",2},{"maxLength",2},{"children",Json::array({scalarForm(rp+"/0","Base",numericDefault),scalarForm(rp+"/1","Range",numericDefault)})}});
                    } else if(valueType=="Fixed")sub.push_back(scalarForm(cp+"/value","value",numericDefault));
                    if(name=="t")for(const auto& f:sub)fields.push_back(f);else fields.push_back(component);
                }
            }
            list["children"].push_back(item);
        }
        children.push_back(list);
    }
    if(target.family==7) {
        Json info=recordForm("Info"+target.name,value.value("info",Json::object()),path+"/info","info");
        info["children"].insert(info["children"].begin(),scalarForm(path+"/info/option","option",Json::object(),enumChoices("OptionRandom",true)));
        info["children"].insert(info["children"].begin(),scalarForm(path+"/info/addTarget","addTarget","Velocity",enumChoices("AddTarget")));
        info["children"].insert(info["children"].begin(),scalarForm(path+"/info/space","space","Global",enumChoices("Space")));children.push_back(info);
    } else if(target.family==2) {
        const auto infoValue=value.value("info",defaults.at("info"));
        auto info=recordForm("AnimationPostFieldInfo",infoValue,path+"/info","info");
        for(auto& field:info["children"])if(field["path"]==path+"/info/childParams") {
            field=childRecord(infoValue.value("childParams",Json::object()),path+"/info/childParams","childParams");field["kind"]="optional";
            field["default"]=choices.at(path+"/info/childParams").at("default");
        }
        children.push_back(info);
    }
    return node;
}
}
