#include "resource_file.h"
#include <set>

namespace breff::codec {
ResourceFile ResourceFile::decode(std::span<const uint8_t> bytes) {
    Reader r(bytes); r.check(0,40);
    ResourceFile result;
    result.magic=std::string(reinterpret_cast<const char*>(bytes.data()),4);
    if ((result.magic!="REFF" && result.magic!="REFT") || r.at(4,2)!=0xfeff ||
        r.at(8,4)!=bytes.size() || r.at(12,2)!=16 || r.at(14,2)!=1 || r.at(16,4)!=r.at(0,4))
        throw std::runtime_error("Invalid BREFF/BREFT archive header");
    result.version=uint16_t(r.at(6,2));
    if (result.version<7 || result.version>11) throw std::runtime_error("Supported resource versions are 7 through 11");
    const auto projectSize=r.at(20,4), headerSize=r.at(24,4);
    if (projectSize!=bytes.size()-24 || headerSize<16 || headerSize>projectSize)
        throw std::runtime_error("Invalid resource project size");
    const size_t table=24+headerSize;
    r.position=36; const auto length=r.integer(2); r.skip(2);
    const auto name=r.slice(r.position,length);
    if (!length || name.back()!=0 || length>table-r.position) throw std::runtime_error("Invalid project name");
    result.projectName=std::string(reinterpret_cast<const char*>(name.data()),length-1);
    const size_t tableSize=r.at(table,4), count=r.at(table+4,2);
    if (tableSize<8) throw std::runtime_error("Invalid resource table size");
    r.check(table,tableSize); r.position=table+8;
    std::set<std::string> names;
    for(size_t i=0;i<count;++i) {
        ResourceEntry entry; entry.name=r.name();
        const size_t offset=r.integer(4); size_t size=r.integer(4);
        if(r.position>table+tableSize || offset<tableSize || offset>bytes.size()-table)
            throw std::runtime_error("Resource entry points outside archive");
        if(!names.insert(entry.name).second) throw std::runtime_error("Duplicate resource name: "+entry.name);
        if(result.magic=="REFT") {
            // REFT table sizes exclude the texture header. Obtain the complete
            // image and palette lengths from the record itself.
            r.check(table+offset,32);
            size=32+r.at(table+offset+8,4)+r.at(table+offset+16,4);
        }
        const auto data=r.slice(table+offset,size); entry.data.assign(data.begin(),data.end());
        result.entries.push_back(std::move(entry));
    }
    return result;
}
Bytes ResourceFile::encode() const {
    if ((magic!="REFF" && magic!="REFT") || version<7 || version>11)
        throw std::runtime_error("Invalid destination resource format");
    Writer w; w.append({reinterpret_cast<const uint8_t*>(magic.data()),4});
    w.integer(0xfeff,2); w.integer(version,2); w.zeros(4); w.integer(16,2); w.integer(1,2);
    w.append({reinterpret_cast<const uint8_t*>(magic.data()),4}); w.zeros(16);
    // The project name's length and string are separated by two reserved bytes.
    if(projectName.size()>=65535 || projectName.find('\0')!=std::string::npos) throw std::runtime_error("Invalid project name");
    w.integer(projectName.size()+1,2); w.zeros(2);
    w.append({reinterpret_cast<const uint8_t*>(projectName.data()),projectName.size()}); w.zeros(1); w.align(4);
    const size_t table=w.bytes.size(); w.patch(24,table-24,4);
    w.zeros(4); w.integer(entries.size(),2); w.zeros(2);
    std::vector<size_t> patches; std::set<std::string> names;
    for(const auto& entry:entries) {
        if(!names.insert(entry.name).second) throw std::runtime_error("Duplicate resource name: "+entry.name);
        if(magic=="REFT" && entry.data.size()<32)throw std::runtime_error("Truncated texture record");
        w.name(entry.name); patches.push_back(w.bytes.size()); w.zeros(4); w.integer(entry.data.size()-(magic=="REFT"?32:0),4);
    }
    w.align(4); w.patch(table,w.bytes.size()-table,4);
    for(size_t i=0;i<entries.size();++i) {
        // GX texture data must stay aligned in the archive, including after renames.
        if(magic=="REFT") w.align(32);
        w.patch(patches[i],w.bytes.size()-table,4); w.append(entries[i].data);
    }
    w.patch(8,w.bytes.size(),4); w.patch(20,w.bytes.size()-24,4);
    return std::move(w.bytes);
}
}
