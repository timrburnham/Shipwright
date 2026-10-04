#pragma once

#include "WaveField.h"
#include <array>
#include <cstdint>
#include <cstddef>

namespace LakeWater {

// Evaluated 0x40-byte source record from func_80071E70. Values are supplied
// by local ROM extraction; course coordinates are not embedded in source.
struct CartridgeWaveSource {
    float x, z, acrossX, acrossZ, rowX, rowZ, amplitude;
    int32_t acrossCount, rowCount, phaseAdvance, phase, speed, period, minPhase, maxPhase;
    int16_t flags;

    void Step(WaveField& field);
    static bool Decode(const uint8_t* bytes, size_t size, CartridgeWaveSource& source);
};

const std::array<float, 4096>& CartridgeSineTable();

} // namespace LakeWater
