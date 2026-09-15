#include "effect.h"
#include "resource_file.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace breff::codec;
namespace fs=std::filesystem;
namespace {
Bytes readFile(const fs::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open input file: "+path.string());
    auto length=input.tellg();if(length<0 || length>256*1024*1024)throw std::runtime_error("Input file exceeds 256 MiB");
    Bytes bytes(static_cast<size_t>(length));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),length))throw std::runtime_error("Could not read input file");return bytes;
}
Json readJson(const fs::path& path) {
    auto bytes=readFile(path);return Json::parse(bytes.begin(),bytes.end());
}
void writeFile(const fs::path& path,std::span<const uint8_t> bytes,bool overwrite) {
    if(!overwrite && fs::exists(path))throw std::runtime_error("Output exists: "+path.string());
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if(!output || !output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()))throw std::runtime_error("Could not write output: "+path.string());
}
void writeJson(const fs::path& path,const Json& value,bool overwrite) {
    auto text=value.dump(2)+"\n";writeFile(path,{reinterpret_cast<const uint8_t*>(text.data()),text.size()},overwrite);
}
bool validFilename(const std::string& name) {
    return !name.empty() && name!="." && name!=".." && name.find_first_of("/\\:*?\"<>|\0",0,10)==std::string::npos;
}
}
int main(int argc,char** argv) {
    try {
        if(argc<3) {
            std::cout<<"breff_converter decode|encode|decode_raw|encode_raw SOURCE [-d DESTINATION] [--version 7..11] [-o]\n";
            return argc==1?0:1;
        }
        const std::string operation=argv[1];const fs::path source=fs::u8path(argv[2]);fs::path destination;
        bool overwrite=false;unsigned version=0;
        for(int i=3;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="-o" || option=="--overwrite")overwrite=true;
            else if((option=="-d" || option=="--dests") && i+1<argc)destination=fs::u8path(argv[++i]);
            else if(option=="--version" && i+1<argc)version=unsigned(std::stoul(argv[++i]));
            else throw std::runtime_error("Unknown or incomplete option: "+option);
        }
        if(version && (version<7 || version>11))throw std::runtime_error("Destination version must be 7 through 11");
        if(destination.empty()) {
            destination=source;
            if(operation=="decode")destination.replace_extension(".breff.d");
            else if(operation=="encode") {destination.replace_extension();destination.replace_extension(".breff");}
            else if(operation=="decode_raw")destination.replace_extension(".json");
            else if(operation=="encode_raw")destination.replace_extension();
        }
        if(operation=="decode") {
            auto archive=ResourceFile::decode(readFile(source));
            if(archive.magic!="REFF")throw std::runtime_error("Expected a BREFF archive");
            if(fs::exists(destination) && !overwrite)throw std::runtime_error("Output directory exists");
            std::vector<std::pair<std::string,Json>> effects;
            for(const auto& entry:archive.entries) {
                if(!validFilename(entry.name) || entry.name=="meta")throw std::runtime_error("Effect name cannot be exported as a JSON filename: "+entry.name);
                effects.emplace_back(entry.name,decodeEffect(entry.data,archive.version));
            }
            fs::create_directories(destination);
            writeJson(destination/"meta.json",{{"version",archive.version},{"projectName",archive.projectName}},overwrite);
            for(const auto& [name,value]:effects)writeJson(destination/(name+".json"),value,overwrite);
        } else if(operation=="encode") {
            const auto meta=readJson(source/"meta.json");ResourceFile archive;
            archive.version=uint16_t(version?version:meta.at("version").get<unsigned>());archive.projectName=meta.at("projectName");
            std::vector<fs::path> files;
            for(const auto& file:fs::directory_iterator(source))if(file.is_regular_file() && file.path().extension()==".json" && file.path().filename()!="meta.json")files.push_back(file.path());
            std::sort(files.begin(),files.end());
            for(const auto& file:files)archive.entries.push_back({file.stem().string(),encodeEffect(readJson(file),archive.version)});
            writeFile(destination,archive.encode(),overwrite);
        } else if(operation=="decode_raw")writeJson(destination,decodeEffect(readFile(source),version?version:11),overwrite);
        else if(operation=="encode_raw")writeFile(destination,encodeEffect(readJson(source),version?version:11),overwrite);
        else throw std::runtime_error("Unknown operation: "+operation);
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
