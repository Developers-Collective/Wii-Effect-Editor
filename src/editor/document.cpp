#include "document.h"
#include "../codec/effect.h"
#include "../codec/animation.h"
#include "../codec/texture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <cctype>
#include <png.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace breff {
using codec::Json;using codec::Bytes;namespace fs=std::filesystem;
namespace {
Bytes readFile(const fs::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open "+path.string());
    auto length=input.tellg();if(length<0 || length>256*1024*1024)throw std::runtime_error("File exceeds 256 MiB");
    Bytes bytes(static_cast<size_t>(length));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),length))throw std::runtime_error("Could not read "+path.string());return bytes;
}
std::string utf8(const fs::path& path) {const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
void atomicWrite(const fs::path& path,std::span<const uint8_t> bytes) {
    static std::atomic<unsigned> serial=0;
    auto temporary=path;temporary+=std::string(".")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(serial++)+".tmp";
    try {
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
         if(!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()) || !stream.flush())throw std::runtime_error("Could not write "+path.string());}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace "+path.string());
#else
        fs::rename(temporary,path);
#endif
    } catch(...) {std::error_code ec;fs::remove(temporary,ec);throw;}
}
void writePair(const fs::path& effect,const Bytes& effectBytes,const fs::path& texture,const Bytes& textureBytes) {
    if(fs::absolute(effect).lexically_normal()==fs::absolute(texture).lexically_normal())throw std::runtime_error("BREFF and BREFT need different paths");
    const bool existed=fs::exists(texture);const auto before=existed?readFile(texture):Bytes();
    atomicWrite(texture,textureBytes);
    try {atomicWrite(effect,effectBytes);}catch(...) {if(existed)atomicWrite(texture,before);else fs::remove(texture);throw;}
}
void validateName(const std::string& name,const codec::ResourceFile& archive) {
    if(name.empty() || name.size()>65534 || name.find('\0')!=std::string::npos)throw std::runtime_error("Enter a nonempty resource name");
    for(const auto& entry:archive.entries)if(entry.name==name)throw std::runtime_error("Resource name already exists");
}
template<class F>void visit(Json& value,const std::string& path,F&& fn) {
    if(value.is_object()) {
        fn(value,path);
        for(auto it=value.begin();it!=value.end();++it)visit(it.value(),path+"/"+it.key(),fn);
    } else if(value.is_array())for(size_t i=0;i<value.size();++i)visit(value[i],path+"/"+std::to_string(i),fn);
}
bool textureSlot(const std::string& path) {return path.ends_with("/texture1") || path.ends_with("/texture2") || path.ends_with("/textureInd");}
std::string textureKey(const Json& value,const std::string& path) {
    if(value.contains("textureName"))return "textureName";
    if(textureSlot(path) && value.contains("name"))return "name";
    return "";
}
Bytes importImage(const fs::path& path) {
    auto extension=path.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension==".png") {
        const auto bytes=readFile(path);png_image image{};image.version=PNG_IMAGE_VERSION;
        struct Release {png_image* image;~Release(){png_image_free(image);}} release{&image};
        if(!png_image_begin_read_from_memory(&image,bytes.data(),bytes.size()))throw std::runtime_error("Could not read PNG: "+std::string(image.message));
        if(!image.width || !image.height || image.width>1024 || image.height>1024)throw std::runtime_error("GX images must be between 1 and 1024 pixels per dimension");
        image.format=PNG_FORMAT_RGBA;Bytes pixels(PNG_IMAGE_SIZE(image));
        if(!png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr))throw std::runtime_error("Could not decode PNG: "+std::string(image.message));
        return codec::encodeTexture(image.width,image.height,pixels);
    }
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    struct Uninit {bool active;~Uninit(){if(active)CoUninitialize();}} guard{SUCCEEDED(initialized)};
    auto check=[](HRESULT result){if(FAILED(result))throw std::runtime_error("Could not decode image");};
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame));UINT width=0,height=0;check(frame->GetSize(&width,&height));
    if(!width || !height || width>1024 || height>1024)throw std::runtime_error("GX images must be between 1 and 1024 pixels per dimension");
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    Bytes pixels(width*height*4);check(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()));return codec::encodeTexture(width,height,pixels);
#else
    throw std::runtime_error("Choose a PNG image");
#endif
}
}
DocumentService::DocumentService() {
    const auto root=fs::temp_directory_path();
    for(unsigned i=0;i<100;++i) {
        directory=root/("breff-editor-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
        if(fs::create_directory(directory))return;
    }
    throw std::runtime_error("Could not create editor session directory");
}
DocumentService::~DocumentService() {std::error_code ec;fs::remove_all(directory,ec);}
codec::ResourceFile DocumentService::effectArchive(const State& state,unsigned version) const {
    auto archive=state.effects;if(version)archive.version=uint16_t(version);
    for(auto& entry:archive.entries) {
        if(state.values.contains(entry.name)) {
            const auto& value=state.values.at(entry.name);
            const bool unchanged=state.originalValues.contains(entry.name) && state.originalValues.at(entry.name)==value;
            const bool compatibleLayout=archive.version==state.effects.version || (archive.version>=8 && state.effects.version>=8);
            if(!unchanged || !compatibleLayout)entry.data=compatibleLayout?
                codec::encodeEffectPreserving(value,archive.version,entry.data):codec::encodeEffect(value,archive.version);
        }
        else if(archive.version!=state.effects.version)throw std::runtime_error("Cannot convert unreadable effect "+entry.name);
    }
    return archive;
}
void DocumentService::publish(const State& candidate,const std::map<std::string,Bytes>* overrides) {
    auto effects=effectArchive(candidate,11);auto textures=candidate.textures;textures.version=11;
    Json metadata=Json::array();
    Json dependencies=Json::object();auto values=candidate.values;
    for(auto it=values.begin();it!=values.end();++it)visit(it.value(),"",[&](Json& value,const std::string& path) {
        const auto key=textureKey(value,path);
        if(!key.empty() && value.at(key).is_string()) {
            const auto name=value.at(key).get<std::string>();
            if(!dependencies.contains(name))dependencies[name]=Json::array();
            dependencies[name].push_back({{"effect",it.key()},{"field",path+"/"+key}});
        }
    });
    for(size_t i=0;i<textures.entries.size();++i) {
        const auto& entry=textures.entries[i];
        auto cached=imageCache.find(entry.data);
        if(cached==imageCache.end()) {
            auto pixels=codec::decodeTexture(entry.data);
            const auto path=directory/("texture-"+std::to_string(imageCache.size())+".rgba");atomicWrite(path,pixels.rgba);
            cached=imageCache.emplace(entry.data,Json{{"width",pixels.width},{"height",pixels.height},{"format",pixels.format},{"image",utf8(path)},{"error",""}}).first;
        }
        auto item=cached->second;item["name"]=entry.name;item["dependencies"]=dependencies.value(entry.name,Json::array());metadata.push_back(item);
    }
    const auto& replacements=overrides?*overrides:previewTextures;
    Json missing=Json::array();std::set<std::string> available;
    for(const auto& entry:textures.entries)available.insert(entry.name);
    Bytes black(4*4*4,0);for(size_t i=3;i<black.size();i+=4)black[i]=255;
    const auto blackTexture=codec::encodeTexture(4,4,black);
    for(const auto& [name,users]:dependencies.items())if(!name.empty() && !available.contains(name)) {
        if(auto image=replacements.find(name);image!=replacements.end())textures.entries.push_back({name,image->second});
        else {missing.push_back(name);textures.entries.push_back({name,blackTexture});}
    }
    writePair(directory/"preview.breff",effects.encode(),directory/"preview.breft",textures.encode());
    textureMetadata=std::move(metadata);missingTextures=std::move(missing);++generation;
}
Json DocumentService::state() const {
    if(!document.loaded)return {{"loaded",false}};
    Json names=Json::array();for(const auto& entry:document.effects.entries)names.push_back(entry.name);
    Json effect=document.values.value(document.selected,Json());
    return {{"loaded",true},{"path",utf8(effectPath)},{"dirty",effectArchive(document).encode()!=savedEffects || document.textures.encode()!=savedTextures},
        {"project",document.effects.projectName},{"version",document.effects.version},{"names",names},{"errors",document.errors},{"selected",document.selected},
        {"effect",effect},{"texturePath",utf8(directory/"preview.breft")},{"textureSourcePath",utf8(texturePath)},
        {"textures",textureMetadata},{"textureCount",textureMetadata.size()},{"missingTextures",missingTextures},
        {"snapshot",utf8(directory/"preview.breff")},{"generation",generation},{"canUndo",!undo.empty()},{"canRedo",!redo.empty()}};
}
Json DocumentService::dispatch(const Json& request) {
    const std::string op=request.at("op");if(op=="state")return state();
    if(op=="open") {
        State candidate;const auto path=fs::absolute(fs::u8path(request.at("breff").get<std::string>()));auto tex=path;tex.replace_extension(".breft");
        if(request.contains("breft") && request["breft"].is_string() && !request["breft"].get<std::string>().empty())tex=fs::absolute(fs::u8path(request["breft"].get<std::string>()));
        candidate.effects=codec::ResourceFile::decode(readFile(path));candidate.textures=codec::ResourceFile::decode(readFile(tex));
        if(candidate.effects.magic!="REFF" || candidate.textures.magic!="REFT")throw std::runtime_error("Choose a BREFF and a BREFT");
        for(const auto& entry:candidate.effects.entries) {
            try {candidate.values[entry.name]=codec::decodeEffect(entry.data,candidate.effects.version,true);}catch(const std::exception& e){candidate.errors[entry.name]=e.what();}
        }
        candidate.originalValues=candidate.values;
        if(!candidate.effects.entries.empty())candidate.selected=candidate.effects.entries.front().name;candidate.loaded=true;
        const std::map<std::string,Bytes> emptyOverrides;
        publish(candidate,&emptyOverrides);previewTextures.clear();document=std::move(candidate);effectPath=path;texturePath=tex;
        savedEffects=effectArchive(document).encode();savedTextures=document.textures.encode();undo.clear();redo.clear();return state();
    }
    if(!document.loaded)throw std::runtime_error("Open a BREFF/BREFT pair first");
    if(op=="preview_texture_folder") {
        const auto folder=fs::u8path(request.at("path").get<std::string>());
        if(!fs::is_directory(folder))throw std::runtime_error("Choose a folder containing PNG textures");
        std::map<std::string,fs::path> images;
        for(const auto& entry:fs::directory_iterator(folder))if(entry.is_regular_file()) {
            auto extension=entry.path().extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(extension==".png")images.emplace(utf8(entry.path().stem()),entry.path());
        }
        auto replacements=previewTextures;size_t matched=0;
        for(const auto& item:missingTextures) {
            const auto name=item.get<std::string>();
            if(auto image=images.find(name);image!=images.end()){replacements[name]=importImage(image->second);++matched;}
        }
        if(matched){publish(document,&replacements);previewTextures=std::move(replacements);}
        auto result=state();result["previewTexturesMatched"]=matched;return result;
    }
    if(op=="select") {
        auto name=request.at("name").get<std::string>();
        if(std::none_of(document.effects.entries.begin(),document.effects.entries.end(),[&](const auto& e){return e.name==name;}))throw std::runtime_error("Unknown effect");
        document.selected=name;return state();
    }
    if(op=="undo" || op=="redo") {
        auto& source=op=="undo"?undo:redo;auto& target=op=="undo"?redo:undo;
        if(!source.empty()) {publish(source.back());target.push_back(document);document=std::move(source.back());source.pop_back();}return state();
    }
    if(op=="export") {
        auto text=document.values.at(document.selected).dump(2)+"\n";atomicWrite(fs::u8path(request.at("path").get<std::string>()),{reinterpret_cast<const uint8_t*>(text.data()),text.size()});return state();
    }
    if(op=="save") {
        auto path=effectPath,tex=texturePath;
        if(request.contains("path") && request["path"].is_string() && !request["path"].get<std::string>().empty()){path=fs::absolute(fs::u8path(request["path"].get<std::string>()));tex=path;tex.replace_extension(".breft");}
        auto effects=effectArchive(document).encode(),textures=document.textures.encode();writePair(path,effects,tex,textures);
        effectPath=path;texturePath=tex;savedEffects=std::move(effects);savedTextures=std::move(textures);return state();
    }
    auto candidate=document;
    if(op=="replace" || op=="import") {
        Json value;
        if(op=="replace") {if(request.at("name")!=document.selected)throw std::runtime_error("Selection changed");value=request.at("effect");}
        else {auto bytes=readFile(fs::u8path(request.at("path").get<std::string>()));value=Json::parse(bytes.begin(),bytes.end());}
        if(op=="replace" && document.values.contains(document.selected)) {
            const auto& previous=document.values.at(document.selected);
            if(value.at("animations").size()==previous.at("animations").size())
                for(size_t i=0;i<value.at("animations").size();++i)
                    codec::prepareAnimationEdit(value["animations"][i],previous["animations"][i],value.at("emitter"));
        }
        value=codec::decodeEffect(codec::encodeEffect(value,document.effects.version),document.effects.version,true);
        candidate.values[document.selected]=value;candidate.errors.erase(document.selected);
    } else if(op=="effect_add") {
        const std::string name=request.at("name");validateName(name,candidate.effects);
        // The standard effect is compiled into the application.
        const Json standard=Json::parse(
#include "standard_effect.inc"
        );
        auto bytes=codec::encodeEffect(standard,candidate.effects.version);
        candidate.values[name]=codec::decodeEffect(bytes,candidate.effects.version,true);
        candidate.effects.entries.push_back({name,std::move(bytes)});candidate.selected=name;
    } else if(op=="effect_rename") {
        const std::string name=request.at("name");if(name==document.selected)return state();validateName(name,candidate.effects);
        for(auto& entry:candidate.effects.entries)if(entry.name==document.selected)entry.name=name;
        if(candidate.values.contains(document.selected)){candidate.values[name]=candidate.values.at(document.selected);candidate.values.erase(document.selected);}
        if(candidate.originalValues.contains(document.selected)){candidate.originalValues[name]=candidate.originalValues.at(document.selected);candidate.originalValues.erase(document.selected);}
        if(candidate.errors.contains(document.selected)){candidate.errors[name]=candidate.errors.at(document.selected);candidate.errors.erase(document.selected);}
        for(auto& value:candidate.values)visit(value,"",[&](Json& object,const std::string&){if(object.contains("childType") && object.value("name",std::string())==document.selected)object["name"]=name;});candidate.selected=name;
    } else if(op=="effect_delete") {
        std::erase_if(candidate.effects.entries,[&](const auto& e){return e.name==document.selected;});candidate.values.erase(document.selected);candidate.errors.erase(document.selected);
        candidate.selected=candidate.effects.entries.empty()?"":candidate.effects.entries.front().name;
    } else if(op.starts_with("texture_")) {
        const std::string name=request.at("name");auto& entries=candidate.textures.entries;
        auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.name==name;});
        if(op=="texture_add") {validateName(name,candidate.textures);entries.push_back({name,importImage(fs::u8path(request.at("path").get<std::string>()))});}
        else {
            if(entry==entries.end())throw std::runtime_error("Unknown texture");
            if(op=="texture_replace")entry->data=importImage(fs::u8path(request.at("path").get<std::string>()));
            else {
                const std::string replacement=op=="texture_rename"?request.at("newName").get<std::string>():request.value("replacement",std::string());
                if(op=="texture_rename"){if(replacement==name)return state();validateName(replacement,candidate.textures);entry->name=replacement;}
                else if(op=="texture_delete") {
                    if(!replacement.empty() && (replacement==name || std::none_of(entries.begin(),entries.end(),[&](const auto& e){return e.name==replacement;})))throw std::runtime_error("Choose an available replacement texture");
                    entries.erase(entry);
                } else throw std::runtime_error("Unknown texture operation");
                for(auto& effect:candidate.values)visit(effect,"",[&](Json& object,const std::string& path){
                    auto key=textureKey(object,path);
                    if(!key.empty() && object[key]==name) {
                        object[key]=replacement;
                    }
                });
            }
        }
    } else throw std::runtime_error("Unknown editor operation: "+op);
    publish(candidate);undo.push_back(document);if(undo.size()>100)undo.erase(undo.begin());redo.clear();document=std::move(candidate);return state();
}
Json DocumentService::handle(const Json& request) {
    try{return {{"ok",true},{"data",dispatch(request)}};}catch(const std::exception& e){return {{"ok",false},{"error",e.what()}};}
}
}
