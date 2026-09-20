#ifndef NW4R_MATH_ARITHMETIC_H
#define NW4R_MATH_ARITHMETIC_H
#include <nw4r/types_nw4r.h>

#include <nw4r/math/math_constant.h>

#include <revolution/OS.h>

#include <cmath>

namespace nw4r {
namespace math {

/******************************************************************************
 *
 * Implementation details
 *
 ******************************************************************************/
namespace detail {

f32 FExp(f32 x);
f32 FLog(f32 x);

} // namespace detail

/******************************************************************************
 *
 * Arithmetic functions
 *
 ******************************************************************************/
inline f32 FrSqrt(f32 x) {
    return 1.0f / std::sqrt(x);
}

inline f32 FAbs(f32 x) {
    return std::fabs(x);
}

inline f32 FCeil(f32 x) {
    return std::ceil(x);
}

inline f32 FExp(f32 x) {
    return detail::FExp(x);
}

inline f32 FFloor(f32 x) {
    return std::floor(x);
}

inline f32 FInv(f32 x) {
    return 1.0f / x;
}

inline f32 FMod(f32 x, f32 y) {
    return std::fmod(x, y);
}

inline f32 FModf(f32 x, f32* pY) {
    return std::modf(x, pY);
}

inline f32 FSqrt(f32 x) {
    return x <= 0.0f ? 0.0f : x * FrSqrt(x);
}

inline f32 FLog(f32 x) {
    if (x > 0.0f) {
        return detail::FLog(x);
    }

    return NW4R_MATH_QNAN;
}

inline f32 FSelect(f32 value, f32 ge_zero, f32 lt_zero) {
    return value >= 0.0f ? ge_zero : lt_zero;
}

/******************************************************************************
 *
 * Fastcast functions
 *
 ******************************************************************************/
inline f32 U16ToF32(u16 arg) {
    return static_cast<f32>(arg);
}
inline u16 F32ToU16(f32 arg) {
    return static_cast<u16>(std::clamp(arg, 0.0f, 65535.0f));
}

inline f32 S16ToF32(s16 arg) {
    return static_cast<f32>(arg);
}
inline s16 F32ToS16(f32 arg) {
    return static_cast<s16>(std::clamp(arg, -32768.0f, 32767.0f));
}

inline u32 F32AsU32(f32 arg) {
    return *reinterpret_cast<u32*>(&arg);
}
inline f32 U32AsF32(u32 arg) {
    return *reinterpret_cast<f32*>(&arg);
}

inline s32 FGetExpPart(f32 x) {
    s32 s = F32AsU32(x);
    return ((s >> 23) & 0xFF) - 127;
}
inline f32 FGetMantPart(f32 x) {
    u32 u = F32AsU32(x);
    return U32AsF32((u & 0x807FFFFF) | 0x3F800000);
}

} // namespace math
} // namespace nw4r

#endif
