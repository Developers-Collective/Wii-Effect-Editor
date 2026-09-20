#pragma once
#include <dolphin/gx.h>
#define GX_MTX_2x4 GX_MTX2x4
#define GX_MTX_3x4 GX_MTX3x4
inline void GXNormal3u8(u8 x, u8 y, u8 z) {
    GXNormal3s8(static_cast<s8>(x), static_cast<s8>(y), static_cast<s8>(z));
}
inline void GXNormal3u16(u16 x, u16 y, u16 z) {
    GXNormal3s16(static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(z));
}
