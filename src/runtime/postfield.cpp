#include "postfield.h"
#include "curves.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace breff {
using namespace nw4r;
namespace {
constexpr float epsilon = 1.1920928955078125e-7f;
struct Reader {
    std::span<const uint8_t> data;
    uint8_t byte(size_t p) const {
        if (p >= data.size())
            throw std::runtime_error("Truncated post-field");
        return data[p];
    }
    uint16_t word(size_t p) const {
        return uint16_t(byte(p)) << 8 | byte(p + 1);
    }
    uint32_t dword(size_t p) const {
        return uint32_t(word(p)) << 16 | word(p + 2);
    }
    float number(size_t p) const {
        return std::bit_cast<float>(dword(p));
    }
    math::VEC3 vector(size_t p) const {
        return {number(p), number(p + 4), number(p + 8)};
    }
    size_t info() const {
        return 32 + size_t(dword(12)) + dword(16) + dword(20) + dword(24);
    }
};
math::VEC3 point(const math::MTX34& m, math::VEC3 v) {
    math::VEC3Transform(&v, &m, &v);
    return v;
}
math::VEC3 normal(const math::MTX34& m, math::VEC3 v) {
    math::VEC3TransformNormal(&v, &m, &v);
    return v;
}
float dot(math::VEC3 a, math::VEC3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
math::VEC3 multiply(math::VEC3 a, math::VEC3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
math::MTX34 matrix(math::VEC3 scale, math::VEC3 rotation, math::VEC3 translation, const FieldContext& ctx,
                   bool emitterCenter) {
    math::MTX34 m;
    math::MTX34RotXYZRad(&m, rotation.x, rotation.y, rotation.z);
    if (scale.x == 0)
        scale.x = 1e-5f;
    if (scale.y == 0)
        scale.y = 1e-5f;
    if (scale.z == 0)
        scale.z = 1e-5f;
    math::MTX34Scale(&m, &m, &scale);
    m._03 = translation.x;
    m._13 = translation.y;
    m._23 = translation.z;
    if (emitterCenter) {
        m._03 += ctx.emitterToWorld._03;
        m._13 += ctx.emitterToWorld._13;
        m._23 += ctx.emitterToWorld._23;
    }
    math::MTX34Mult(&m, &ctx.worldToLocal, &m);
    return m;
}
unsigned positionType(math::VEC3 p, unsigned shape, unsigned option) {
    if (shape <= 2) {
        const int axis = option == 1 ? 2 : option == 2 ? 0 : 1;
        const float h = (&p.x)[axis];
        if (!shape)
            return h > 0 ? 2 : h < 0 ? 3 : 1;
        if (h != 0)
            return 3;
        const float a = (&p.x)[(axis + 1) % 3], b = (&p.x)[(axis + 2) % 3];
        return std::abs(a) <= 1 && std::abs(b) <= 1 && (shape == 1 || a * a + b * b <= 1) ? 1 : 4;
    }
    if (std::abs(p.x) > 1 || std::abs(p.y) > 1 || std::abs(p.z) > 1)
        return 3;
    if (shape == 3)
        return std::abs(p.x) == 1 || std::abs(p.y) == 1 || std::abs(p.z) == 1 ? 1 : 2;
    float radius = p.x * p.x + p.z * p.z;
    if (shape == 4) {
        if (((option & 1) && p.y < 0) || ((option & 2) && p.y > 0))
            return 3;
        radius += p.y * p.y;
        if (radius > 1)
            return 3;
        return radius == 1 || (option && p.y == 0) ? 1 : 2;
    }
    if (radius > 1)
        return 3;
    return radius == 1 || std::abs(p.y) == 1 ? 1 : 2;
}
struct Hit {
    float t = 2;
    math::VEC3 point{0, 0, 0}, normal{0, 0, 0};
    unsigned axes = 0;
    bool curved = false;
};
Hit intersect(math::VEC3 start, math::VEC3 end, unsigned shape, unsigned option) {
    Hit best;
    const auto delta = end - start;
    auto accept = [&](float t, math::VEC3 n, unsigned axes, bool curved, bool includeEnd) {
        if (!(t > 0 && (includeEnd ? t <= 1 : t < 1)))
            return;
        if (shape == 3 && std::abs(t - best.t) <= epsilon) {
            best.axes |= axes;
            return;
        }
        if (t >= best.t)
            return;
        best = {t, start + delta * t, n, axes, curved};
    };
    auto plane = [&](int axis, float height, int bounds) {
        float a = (&start.x)[axis] - height, b = (&end.x)[axis] - height, d = (&delta.x)[axis];
        if (d == 0)
            return;
        if (shape <= 2 || shape == 4) {
            if (std::abs(a) < epsilon && std::abs(b) < epsilon)
                return;
            if (!((a > 0 && b < epsilon) || (a < 0 && b > epsilon)))
                return;
        }
        float t = -a / d;
        auto p = start + delta * t;
        float x = (&p.x)[(axis + 1) % 3], z = (&p.x)[(axis + 2) % 3];
        if (bounds == 1 && (std::abs(x) > 1 || std::abs(z) > 1))
            return;
        if (bounds == 2 && x * x + z * z >= 1)
            return;
        math::VEC3 n(0, 0, 0);
        (&n.x)[axis] = a > 0 ? 1.f : -1.f;
        accept(t, n, 1u << axis, false, true);
    };
    if (shape <= 2)
        plane(option == 1 ? 2 : option == 2 ? 0 : 1, 0, shape);
    else if (shape == 3)
        for (int i = 0; i < 3; ++i) {
            plane(i, 1, 1);
            plane(i, -1, 1);
        }
    else {
        float a = dot(delta, delta), b = dot(start, delta), c = dot(start, start);
        if (shape == 5) {
            a = delta.x * delta.x + delta.z * delta.z;
            b = start.x * delta.x + start.z * delta.z;
            c = start.x * start.x + start.z * start.z;
        }
        const float discriminant = 4 * b * b - 4 * a * (c - 1);
        if (a > 0 && discriminant >= 0) {
            const float root = std::sqrt(discriminant);
            for (float t : {(-2 * b - root) / (2 * a), (-2 * b + root) / (2 * a)}) {
                auto p = start + delta * t;
                if (shape == 4 && ((option == 1 && p.y < -epsilon) || (option == 2 && p.y > epsilon)))
                    continue;
                if (shape == 5 && std::abs(p.y) > 1)
                    continue;
                auto n = p;
                if (shape == 5)
                    n.y = 0;
                accept(t, n, 0, true, false);
            }
        }
        if (shape == 5) {
            plane(1, 1, 2);
            plane(1, -1, 2);
        } else if (option == 1 || option == 2)
            plane(1, 0, 2);
    }
    return best;
}
}
void PostField::evaluate(std::span<const uint8_t> source, uint32_t tick, uint16_t seed, uint32_t life) {
    Reader r{source};
    if (r.dword(28)) {
        track = source;
        for (int i = 0; i < 9; ++i)
            transform[i] = r.number(r.info() + 4 * i);
    }
    if (r.dword(12)) {
        unsigned offset = r.byte(1) / 4;
        if (offset > 6)
            throw std::runtime_error("Invalid post-field animation target");
        evaluateF32(source, {transform.data() + offset, 3}, tick, seed, life);
    }
}
bool PostField::apply(ef::Particle& particle, const FieldContext& ctx, math::VEC3 velocity,
                      math::VEC3 displacement) const {
    Reader r{track};
    const size_t i = r.info();
    const unsigned options = r.word(i + 44);
    const unsigned shape = r.byte(i + 41), shapeOption = r.byte(i + 42), collisionType = r.byte(i + 43),
                   speedControl = r.byte(i + 40);
    auto m = matrix({transform[0], transform[1], transform[2]}, {transform[3], transform[4], transform[5]},
                    {transform[6], transform[7], transform[8]}, ctx, options & 1);
    math::MTX34 inverse;
    math::MTX34Inv(&inverse, &m);
    auto start = point(inverse, ctx.position);
    auto& p = particle.mParameter;
    unsigned previous = p.mCollisionStatus, current = positionType(start, shape, shapeOption);
    p.mCollisionStatus = u8(current);
    const float reference = r.number(i + 36);
    bool kill = false;
    auto limit = [&](math::VEC3& v) {
        float size = dot(v, v);
        if (size > reference * reference) {
            if (speedControl == 1) {
                if (size >= epsilon)
                    v *= 1.f / std::sqrt(size);
                v *= reference;
            } else if (speedControl == 2)
                kill = true;
        }
    };
    auto child = [&](const math::VEC3* location) {
        if (!(options & 0x30) || r.dword(24) <= 4)
            return;
        const auto name = curveName(track, r.word(i + 70));
        auto* resource = ef::Resource::GetInstance()->_FindEmitter(name.c_str(), nullptr);
        if (!resource)
            throw std::runtime_error("Missing collision child effect: " + name);
        ef::EmitterInheritSetting inherit{};
        inherit.speed = int16_t(r.word(i + 60));
        inherit.scale = r.byte(i + 62);
        inherit.alpha = r.byte(i + 63);
        inherit.color = r.byte(i + 64);
        inherit.weight = r.byte(i + 65);
        inherit.type = r.byte(i + 66);
        inherit.flag = r.byte(i + 67);
        auto& queue = particle.mParticleManager->mManagerEM->mManagerEF->mManagerES->mCreationQueue;
        if (options & 16)
            queue.AddParticleCreation(&inherit, &particle, resource, particle.mCalcRemain, location);
        if (options & 32)
            queue.AddEmitterCreation(&inherit, &particle, resource, particle.mCalcRemain, location);
    };
    limit(velocity);
    bool handled = false;
    if (!kill && particle.mTick >= r.word(i + 46) && options) {
        if (collisionType == 0) {
            auto end = point(inverse, ctx.position + (displacement + velocity) * p.mMomentum);
            auto localVelocity = normal(inverse, velocity);
            for (int bounce = 0; bounce < 5; ++bounce) {
                auto hit = intersect(start, end, shape, shapeOption);
                if (hit.t > 1.5f || dot(end - start, end - start) < epsilon * epsilon) {
                    if (!handled && particle.mTick > r.word(i + 46) &&
                        ((previous == 2 && (current == 3 || current == 1)) ||
                         (previous == 3 && (current == 1 || current == 2)))) {
                        kill = (options & 64) != 0;
                        child(nullptr);
                    }
                    break;
                }
                auto remaining = end - start, contact = hit.point;
                if (options & 2) {
                    if (hit.curved) {
                        float n2 = dot(hit.normal, hit.normal);
                        auto reflect = [&](math::VEC3 v) {
                            auto n = hit.normal * (dot(v, hit.normal) / n2);
                            return (v - n) - n;
                        };
                        remaining = reflect(remaining);
                        localVelocity = reflect(localVelocity);
                        float radius = shape == 5 ? start.x * start.x + start.z * start.z : dot(start, start);
                        contact *= radius <= 1 ? .99999f : 1.00001f;
                    } else
                        for (int axis = 0; axis < 3; ++axis)
                            if (hit.axes & (1u << axis)) {
                                (&remaining.x)[axis] *= -1;
                                (&localVelocity.x)[axis] *= -1;
                                if (shape != 3)
                                    (&contact.x)[axis] += (&hit.normal.x)[axis] * epsilon;
                            }
                }
                auto location = point(m, contact);
                velocity = normal(m, localVelocity);
                if (options & 10) {
                    velocity = multiply(velocity, r.vector(i + 48));
                    p.mPosition = location + multiply(normal(m, remaining * (1 - hit.t)), r.vector(i + 48));
                } else
                    p.mPosition = point(m, end);
                p.mVelocity = velocity;
                p.mCollisionStatus |= 128;
                handled = true;
                if (options & 64) {
                    kill = true;
                    p.mPosition = location;
                }
                child(&location);
                if (kill)
                    break;
                start = contact;
                end = point(inverse, p.mPosition);
                localVelocity = normal(inverse, velocity);
                if (bounce == 4)
                    kill = true;
            }
            if (handled) {
                limit(p.mVelocity);
                velocity = p.mVelocity;
            }
        } else if ((collisionType == 1 && current == 2) || (collisionType == 2 && current == 3)) {
            if (options & 8) {
                velocity = multiply(velocity, r.vector(i + 48));
                displacement = multiply(displacement, r.vector(i + 48));
                limit(velocity);
            }
            if (options & 64)
                kill = true;
            child(nullptr);
        }
    }
    if (kill)
        return false;
    if (!handled) {
        p.mVelocity = velocity;
        p.mPosition += displacement * p.mMomentum;
        p.mPosition += velocity * p.mMomentum;
    }
    if (r.byte(i + 72) & 1) {
        auto wrap = matrix(r.vector(i + 76), r.vector(i + 88), r.vector(i + 100), ctx, r.byte(i + 72) & 2);
        math::MTX34 inv;
        math::MTX34Inv(&inv, &wrap);
        auto pos = point(inv, p.mPosition);
        bool changed = false;
        for (int axis = 0; axis < 3; ++axis) {
            auto& v = (&pos.x)[axis];
            if (v > 1) {
                v = std::fmod(v + 1, 2) - 1;
                changed = true;
            } else if (v < -1) {
                v = std::fmod(v - 1, 2) + 1;
                changed = true;
            }
        }
        if (changed)
            p.mPosition = point(wrap, pos);
    }
    return true;
}
}
