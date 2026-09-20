#pragma once
#include "binary.h"
#include <nlohmann/json.hpp>

namespace breff::codec {
using Json = nlohmann::ordered_json;
Json readRecord(const std::string& type, Reader& reader);
void writeRecord(const std::string& type, const Json& value, Writer& writer);
Json publicRecord(const std::string& type, const Json& value);
Json enumName(const std::string& type, uint64_t value);
uint64_t enumValue(const std::string& type, const Json& value);
Json enumChoices(const std::string& type, bool flags = false);
void recordChoices(const std::string& type, const Json& value, const std::string& path, Json& choices);
Json recordForm(const std::string& type, const Json& value, const std::string& path, const std::string& label);
Json scalarForm(const std::string& path, const std::string& label, const Json& fallback, const Json& choices = Json());
Json decodeEmitter(std::span<const uint8_t> bytes);
Bytes encodeEmitter(const Json& value);
Json decodeParticle(std::span<const uint8_t> bytes, bool includeUnboundTextures = false);
Bytes encodeParticle(const Json& value);
}
