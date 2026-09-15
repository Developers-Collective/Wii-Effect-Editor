#pragma once
#include <cstdint>

struct PreviewControls {
    bool useResourceSeed = true;
    int seed = 0;
    uint64_t restart = 0;
    // A changed seed always starts a new simulation, rather than changing the
    // RNG partway through an effect's lifetime.
    void replay() { ++restart; }
    uint16_t selectedSeed() const { return static_cast<uint16_t>(seed); }
};
