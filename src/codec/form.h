#pragma once
#include "records.h"
namespace breff::codec {
// Form structure is defined by BREFF records and tagged variants, not JSON keys.
Json effectForm(const Json& model);
}
