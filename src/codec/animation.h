#pragma once
#include "records.h"
namespace breff::codec {
Json decodeAnimation(std::span<const uint8_t> bytes,const Json& emitter,bool init);
Bytes encodeAnimation(const Json& value,const Json& emitter);
Json defaultAnimation(const std::string& target,const Json& emitter);
void animationChoices(const Json& value,const Json& emitter,const std::string& path,Json& choices);
void prepareAnimationEdit(Json& value,const Json& previous,const Json& emitter);
Json animationForm(const Json& value,const Json& emitter,const std::string& path,const std::string& label);
}
