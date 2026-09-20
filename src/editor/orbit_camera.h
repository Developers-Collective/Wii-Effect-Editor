#pragma once
#include <algorithm>
#include <cmath>

class OrbitCamera {
  public:
    static constexpr float defaultDistance = 200.f;
    float yaw = 0.f, pitch = 0.f, distance = defaultDistance;

    void reset() {
        yaw = pitch = 0.f;
        distance = defaultDistance;
    }
    void rotate(float dx, float dy) {
        constexpr float pi = 3.14159265358979323846f;
        yaw = std::remainder(yaw - dx * .008f, 2.f * pi);
        pitch = std::clamp(pitch + dy * .008f, -pi * .49f, pi * .49f);
    }
    void zoom(float steps) {
        distance = std::clamp(distance * std::exp(-steps * .15f), .01f, 100000.f);
    }
    void view(float (&out)[3][4]) const {
        const float sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
        // Right, up and backward axes of an origin-facing camera. Translation
        // is always (0,0,-distance); orbiting can never pan the target away.
        out[0][0] = cy;
        out[0][1] = 0;
        out[0][2] = -sy;
        out[0][3] = 0;
        out[1][0] = -sp * sy;
        out[1][1] = cp;
        out[1][2] = -sp * cy;
        out[1][3] = 0;
        out[2][0] = cp * sy;
        out[2][1] = sp;
        out[2][2] = cp * cy;
        out[2][3] = -distance;
    }
};
