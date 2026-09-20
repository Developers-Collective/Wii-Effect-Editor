#include <nw4r/math.h>

namespace nw4r::math {
MTX34* MTX34Scale(MTX34* out, const MTX34* in, const VEC3* scale) {
    MTX34 result = *in;
    for (int row = 0; row < 3; ++row) {
        result.m[row][0] *= scale->x;
        result.m[row][1] *= scale->y;
        result.m[row][2] *= scale->z;
    }
    *out = result;
    return out;
}
MTX34* MTX34Trans(MTX34* out, const MTX34* in, const VEC3* trans) {
    MTX34 result = *in;
    for (int row = 0; row < 3; ++row)
        result.m[row][3] = std::fma(in->m[row][2], trans->z, in->m[row][0] * trans->x) +
                           std::fma(in->m[row][1], trans->y, in->m[row][3]);
    *out = result;
    return out;
}
MTX34* MTX34RotXYZFIdx(MTX34* out, f32 fx, f32 fy, f32 fz) {
    f32 sx, cx, sy, cy, sz, cz;
    SinCosFIdx(&sx, &cx, fx);
    SinCosFIdx(&sy, &cy, fy);
    SinCosFIdx(&sz, &cz, fz);
    out->_20 = -sy;
    out->_00 = cz * cy;
    out->_10 = sz * cy;
    out->_21 = cy * sx;
    out->_22 = cy * cx;
    f32 cx_sz = cx * sz, sx_cz = sx * cz, sx_sz = sx * sz, cx_cz = cx * cz;
    out->_01 = std::fma(sx_cz, sy, -cx_sz);
    out->_12 = std::fma(cx_sz, sy, -sx_cz);
    out->_02 = std::fma(cx_cz, sy, sx_sz);
    out->_11 = std::fma(sx_sz, sy, cx_cz);
    out->_03 = out->_13 = out->_23 = 0;
    return out;
}
VEC3* VEC3TransformNormal(VEC3* out, const MTX34* m, const VEC3* v) {
    VEC3 result;
    result.x = std::fma(m->_02, v->z, std::fma(m->_01, v->y, m->_00 * v->x));
    result.y = std::fma(m->_12, v->z, std::fma(m->_11, v->y, m->_10 * v->x));
    result.z = std::fma(m->_22, v->z, std::fma(m->_21, v->y, m->_20 * v->x));
    *out = result;
    return out;
}
}
