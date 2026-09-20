#pragma once
// Scalar counterparts to the paired-single operations in math_types.h.
// Temporary values preserve the original routines' support for aliased inputs.
namespace nw4r::math {
inline VEC3* VEC3Add(VEC3* o, const VEC3* a, const VEC3* b) {
    *o = VEC3(a->x + b->x, a->y + b->y, a->z + b->z);
    return o;
}
inline VEC3* VEC3Sub(VEC3* o, const VEC3* a, const VEC3* b) {
    *o = VEC3(a->x - b->x, a->y - b->y, a->z - b->z);
    return o;
}
inline VEC3* VEC3Scale(VEC3* o, const VEC3* a, f32 s) {
    *o = VEC3(a->x * s, a->y * s, a->z * s);
    return o;
}
inline f32 VEC3Dot(const VEC3* a, const VEC3* b) {
    return std::fma(a->x, b->x, a->y * b->y) + a->z * b->z;
}
inline f32 VEC3LenSq(const VEC3* a) {
    return VEC3Dot(a, a);
}
inline VEC3* VEC3Lerp(VEC3* o, const VEC3* a, const VEC3* b, f32 t) {
    *o = VEC3(std::fma(b->x - a->x, t, a->x), std::fma(b->y - a->y, t, a->y), std::fma(b->z - a->z, t, a->z));
    return o;
}
}
