#ifndef NW4R_UT_ALGORITHM_H
#define NW4R_UT_ALGORITHM_H
#include <nw4r/types_nw4r.h>

namespace nw4r {
namespace ut {
namespace {

/******************************************************************************
 *
 * Value operations
 *
 ******************************************************************************/
template <typename T> inline T Max(T t1, T t2) {
    return (t1 < t2) ? t2 : t1;
}

template <typename T> inline T Min(T t1, T t2) {
    return (t1 > t2) ? t2 : t1;
}

template <typename T> inline T Clamp(T value, T min, T max) {
    return value > max ? max : (value < min ? min : value);
}

template <typename T> inline T Abs(T x) {
    // Static cast needed to break abs optimization
    return x < 0 ? static_cast<T>(-x) : static_cast<T>(x);
}

template <> f32 inline Abs(f32 x) {
    return std::fabs(x);
}

/******************************************************************************
 *
 * Bit operations
 *
 ******************************************************************************/
template <typename T> inline T BitExtract(T bits, int pos, int len) {
    T mask = (1 << len) - 1;
    return (bits >> pos) & mask;
}

template <typename T> inline bool TestBit(T t, int pos) {
    return BitExtract<T>(t, sizeof(T), pos);
}

/******************************************************************************
 *
 * Pointer arithmetic
 *
 ******************************************************************************/
inline uintptr_t GetIntPtr(const void* pPtr) {
    return reinterpret_cast<uintptr_t>(pPtr);
}

template <typename T> inline const void* AddOffsetToPtr(const void* pBase, T offset) {
    return reinterpret_cast<const void*>(GetIntPtr(pBase) + offset);
}
template <typename T> inline void* AddOffsetToPtr(void* pBase, T offset) {
    return reinterpret_cast<void*>(GetIntPtr(pBase) + offset);
}

inline s32 GetOffsetFromPtr(const void* pStart, const void* pEnd) {
    return static_cast<s32>(GetIntPtr(pEnd) - GetIntPtr(pStart));
}

inline int ComparePtr(const void* pPtr1, const void* pPtr2) {
    return static_cast<int>(GetIntPtr(pPtr1) - GetIntPtr(pPtr2));
}

/******************************************************************************
 *
 * Rounding
 *
 ******************************************************************************/
template <typename T> inline T RoundUp(T t, unsigned int alignment) {
    return (alignment + t - 1) & ~T(alignment - 1);
}

template <typename T> inline void* RoundUp(T* pPtr, unsigned int alignment) {
    uintptr_t value = reinterpret_cast<uintptr_t>(pPtr);
    uintptr_t rounded = (alignment + value - 1) & ~uintptr_t(alignment - 1);
    return reinterpret_cast<void*>(rounded);
}

template <typename T> inline T RoundDown(T t, unsigned int alignment) {
    return t & ~T(alignment - 1);
}

template <typename T> inline void* RoundDown(T* pPtr, unsigned int alignment) {
    uintptr_t value = reinterpret_cast<uintptr_t>(pPtr);
    uintptr_t rounded = value & ~(alignment - 1);
    return reinterpret_cast<void*>(rounded);
}

} // namespace
} // namespace ut
} // namespace nw4r

#endif
