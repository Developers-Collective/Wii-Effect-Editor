#pragma once
#include "records.h"

namespace breff::codec {
    std::vector<Json> translateLegacyAnimation(const Json& track, const Json& effect, unsigned version,
                                               std::vector<std::string>& warnings);
}
