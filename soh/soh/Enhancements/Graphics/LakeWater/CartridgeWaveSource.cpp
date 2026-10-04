#include "CartridgeWaveSource.h"
#include <cmath>
#include <cstring>

namespace LakeWater {

bool CartridgeWaveSource::Decode(const uint8_t* bytes, size_t size, CartridgeWaveSource& source) {
    if (bytes == nullptr || size != 64) return false;
    static_assert(sizeof(source) == 64);
    for (unsigned i = 0; i < 15; ++i) {
        const uint32_t bits = (uint32_t(bytes[i*4]) << 24) | (uint32_t(bytes[i*4+1]) << 16) |
            (uint32_t(bytes[i*4+2]) << 8) | bytes[i*4+3];
        std::memcpy(reinterpret_cast<char*>(&source) + i*4, &bits, 4);
    }
    source.flags = static_cast<int16_t>((uint16_t(bytes[60]) << 8) | bytes[61]);
    return std::isfinite(source.x) && std::isfinite(source.z) && std::isfinite(source.acrossX) &&
        std::isfinite(source.acrossZ) && std::isfinite(source.rowX) && std::isfinite(source.rowZ) &&
        std::isfinite(source.amplitude) && std::abs(source.amplitude) <= 127 &&
        source.acrossCount > 0 && source.acrossCount <= 128 && source.rowCount > 0 && source.rowCount <= 128 &&
        source.period > 0 && source.period <= 128 && source.phase == 0 && source.speed >= 0 &&
        source.speed <= source.period * 512 && source.phaseAdvance >= 0 && source.phaseAdvance <= source.period * 512 &&
        source.minPhase >= 0 && source.maxPhase >= source.minPhase && source.maxPhase <= source.period;
}

const std::array<float, 4096>& CartridgeSineTable() {
    // SysUtils_TaylorSeries uses doubles, then stores single precision.
    static const std::array<float, 4096> table = [] {
        std::array<float, 4096> result{};
        for (unsigned i = 0; i < result.size(); ++i) {
            double x = (i * 6.28318530700000011) / 4096;
            double numerator = -x * x * x;
            const double minusSquare = -x * x;
            double denominator = 6;
            for (int j = 2; j < 14; ++j) {
                x += numerator / denominator;
                numerator *= minusSquare;
                denominator *= j * ((j << 2) + 2);
            }
            result[i] = static_cast<float>(x);
        }
        return result;
    }();
    return table;
}

void CartridgeWaveSource::Step(WaveField& field) {
    // func_8004CC7C advances the stored phase once, then walks successive
    // parallel rows. Only the inclusive phase interval emits source triangles.
    const int32_t periodFixed = period * 2048;
    phase -= speed * 4;
    if (phase < 0) {
        phase += periodFixed;
    }
    int32_t rowPhase = phase;
    float rowStartX = x;
    float rowStartZ = z;
    const auto& sine = CartridgeSineTable();
    for (int row = 0; row < rowCount; ++row) {
        rowPhase += phaseAdvance * 4;
        if (rowPhase >= periodFixed) {
            rowPhase -= periodFixed;
        }
        if (rowPhase >= minPhase * 2048 && rowPhase <= maxPhase * 2048) {
            const int32_t height = static_cast<int32_t>(sine[static_cast<uint32_t>(rowPhase) & 4095] * amplitude);
            float sourceX = rowStartX;
            float sourceZ = rowStartZ;
            for (int column = 0; column < acrossCount; ++column) {
                field.SetSourceTriangle(sourceX, sourceZ, height);
                sourceX += acrossX;
                sourceZ += acrossZ;
            }
        }
        rowStartX += rowX;
        rowStartZ += rowZ;
    }
}

} // namespace LakeWater
