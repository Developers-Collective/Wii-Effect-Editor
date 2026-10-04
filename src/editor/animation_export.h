#pragma once
#include <optional>

// Returns no value during an ordinary interactive launch.
std::optional<int> exportAnimations(int argc, char** argv);
