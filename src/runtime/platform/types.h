#pragma once
#include <dolphin/types.h>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
