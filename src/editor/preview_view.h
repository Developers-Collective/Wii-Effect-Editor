#pragma once
#include "orbit_camera.h"
struct PreviewRect {
    float x = 0, y = 0, width = 0, height = 0;
};
void drawPreviewGrid(const PreviewRect& rect, const OrbitCamera& camera, bool grid = true, bool axes = true);
