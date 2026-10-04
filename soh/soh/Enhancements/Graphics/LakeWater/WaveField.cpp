#include "WaveField.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace LakeWater {
namespace {
constexpr float kOneOverSqrt3 = 0.57735026f;
constexpr float kTwoOverSqrt3 = 1.1547005f;
constexpr float kSqrt3Over2 = 0.8660254f;

// C/C++ right shift of a negative signed value is implementation-defined.
// The MIPS instruction used by the cartridge is an arithmetic shift, so spell
// that operation out to keep the port's results independent of host compiler.
int32_t ArithmeticShift(int32_t value, unsigned shift) {
    if (shift >= 31) {
        return value < 0 ? -1 : 0;
    }
    if (value >= 0 || shift == 0) {
        return value >> shift;
    }
    const uint32_t magnitude = static_cast<uint32_t>(-(static_cast<int64_t>(value)));
    const uint32_t bias = (uint32_t{1} << shift) - 1;
    return -static_cast<int32_t>((magnitude + bias) >> shift);
}

int32_t Modulo(int32_t value, int32_t divisor) {
    // The cartridge uses signed remainder, then adds a positive row bias.
    return value % divisor;
}

int32_t FixedCoordinate(float value) {
    // MIPS trunc.w.s truncates toward zero, as does a finite float-to-int cast.
    if (!std::isfinite(value) || value > 2147483000.0f || value < -2147483000.0f) {
        return 0;
    }
    return static_cast<int32_t>(value);
}

bool CoordinateRepresentable(float value) {
    return std::isfinite(value) && value <= 2147483000.0f && value >= -2147483000.0f;
}

struct LatticePoint {
    int32_t u;
    int32_t v;
    float fu;
    float fv;
};

LatticePoint ToLattice(float x, float z) {
    const int32_t u = Modulo(FixedCoordinate(x + z * kOneOverSqrt3), 24576);
    const int32_t v = Modulo(FixedCoordinate(z * kTwoOverSqrt3), 24576);
    const int32_t cu = ArithmeticShift(u, 6);
    const int32_t cv = ArithmeticShift(v, 6);
    return {cu, cv, float(u - cu * 64) / 64.0f, float(v - cv * 64) / 64.0f};
}

} // namespace

WaveField::WaveField() : WaveField(IntegratorShifts{9, 5, 6}) {}

WaveField::WaveField(IntegratorShifts shifts) : shifts_(shifts) {
    if (shifts_.restoreShift > 31 || shifts_.couplingShift > 31 || shifts_.dampingShift > 31) {
        throw std::invalid_argument("Wave Race integrator shift out of supported range");
    }
}

void WaveField::Reset() {
    std::fill(std::begin(cells_), std::end(cells_), Cell{0, 0});
    windowU_ = 0;
    windowV_ = 0;
    radius_ = 12;
    clampSources_ = false;
}

int16_t WaveField::WrapS16(int32_t value) {
    const uint16_t bits = static_cast<uint16_t>(static_cast<uint32_t>(value));
    int16_t result;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

unsigned WaveField::Index(int32_t uCell, int32_t vCell) {
    // Exact 384 x 128 pack/wrap used by func_8004F3D4 and func_8004D30C.
    const int32_t col = uCell & 0x7F;
    const int32_t row = (vCell + (uCell & ~0x7F) + 0x600) % int32_t(kRows);
    return unsigned(row * int32_t(kColumns) + col);
}

void WaveField::Stamp(float worldX, float worldZ, int32_t amplitudeUnits) {
    if (!CoordinateRepresentable(worldX + worldZ * kOneOverSqrt3) ||
        !CoordinateRepresentable(worldZ * kTwoOverSqrt3)) {
        return;
    }
    const int32_t u = Modulo(FixedCoordinate(worldX + worldZ * kOneOverSqrt3), 24576);
    const int32_t v = Modulo(FixedCoordinate(worldZ * kTwoOverSqrt3), 24576);
    const int32_t cu = ArithmeticShift(u, 6);
    const int32_t cv = ArithmeticShift(v, 6);
    const int32_t fu = u - cu * 64;
    const int32_t fv = v - cv * 64;

    // func_8004F3D4 stamps the far corner of the containing half-cell.
    const unsigned target = fu < fv ? Index(cu, cv + 1) : Index(cu + 1, cv);
    // MIPS `sll arg2, 8` wraps in 32 bits; avoid signed-overflow UB here.
    const uint32_t deltaBits = static_cast<uint32_t>(amplitudeUnits) << 8;
    int32_t delta;
    std::memcpy(&delta, &deltaBits, sizeof(delta));
    Cell& cell = cells_[target];
    if (delta < cell.height) {
        cell.height = WrapS16(int32_t(cell.height) + delta);
        cell.velocity = 0;
    }
}

void WaveField::SetWindowCenter(float worldX, float worldZ) {
    if (!CoordinateRepresentable(worldX + worldZ * kOneOverSqrt3) ||
        !CoordinateRepresentable(worldZ * kTwoOverSqrt3)) {
        return;
    }
    const int32_t u = Modulo(FixedCoordinate(worldX + worldZ * kOneOverSqrt3), 24576);
    const int32_t v = Modulo(FixedCoordinate(worldZ * kTwoOverSqrt3), 24576);
    windowU_ = ArithmeticShift(u + 32, 6);
    windowV_ = ArithmeticShift(v + 32, 6);
}

void WaveField::SetSourceTriangle(float worldX, float worldZ, int32_t amplitudeUnits) {
    if (!CoordinateRepresentable(worldX + worldZ * kOneOverSqrt3) ||
        !CoordinateRepresentable(worldZ * kTwoOverSqrt3)) {
        return;
    }
    // Domain stamps must be rejected before the cartridge torus can alias a
    // distant tile into a local cell. Preserve original modulo outside adapters.
    if (clampSources_) {
        const int32_t rawU = ArithmeticShift(FixedCoordinate(worldX + worldZ * kOneOverSqrt3), 6);
        const int32_t rawV = ArithmeticShift(FixedCoordinate(worldZ * kTwoOverSqrt3), 6);
        const int64_t du = int64_t(rawU) - windowU_, dv = int64_t(rawV) - windowV_;
        if (std::abs(du) > radius_ + 1 || std::abs(dv) > radius_ + 1 ||
            std::abs(du - dv) > radius_ + 1) return;
    }
    const LatticePoint p = ToLattice(worldX, worldZ);
    const uint32_t heightBits = static_cast<uint32_t>(amplitudeUnits) << 8;
    int32_t heightSigned;
    std::memcpy(&heightSigned, &heightBits, sizeof(heightSigned));
    const int16_t height = WrapS16(heightSigned);
    const auto set = [this, height](int32_t u, int32_t v) {
        if (clampSources_) {
            const int32_t du = u - windowU_, dv = v - windowV_;
            if (std::abs(du) > int32_t(radius_) || std::abs(dv) > int32_t(radius_) ||
                std::abs(du - dv) > int32_t(radius_)) return;
        }
        cells_[Index(u, v)] = {height, 0};
    };
    set(p.u, p.v);
    set(p.u + 1, p.v + 1);
    if (p.fu < p.fv) set(p.u, p.v + 1);
    else set(p.u + 1, p.v);
}

bool WaveField::SetDomain(int32_t centerU, int32_t centerV, unsigned radius) {
    if (radius < 2 || radius >= 128 || (radius & 1) ||
        centerU < -384 || centerU > 384 || centerV < -384 || centerV > 384) {
        return false;
    }
    windowU_ = centerU;
    windowV_ = centerV;
    radius_ = radius;
    clampSources_ = true;
    return true;
}

void WaveField::Step() {
    // func_8004EAA4 walks growing rows, shrinking rows, then the top edge.
    // Heights are integrated as each cell's final edge is processed. Moving
    // integration to a second pass changes later edge forces, even though the
    // graph contains the same neighbors. Preserve the cartridge ordering.
    const int32_t radius = static_cast<int32_t>(radius_);
    const auto cell = [this](int32_t u, int32_t v) -> Cell& {
        return cells_[Index(windowU_ + u, windowV_ + v)];
    };
    const auto pair = [this](Cell& a, Cell& b) {
        const int32_t mean = ArithmeticShift(int32_t(a.height) + int32_t(b.height), shifts_.restoreShift);
        const int32_t diff = ArithmeticShift(int32_t(b.height) - int32_t(a.height), shifts_.couplingShift);
        a.velocity = WrapS16(int32_t(a.velocity) + diff - mean);
        b.velocity = WrapS16(int32_t(b.velocity) - diff - mean);
    };
    const auto integrate = [this](Cell& a) {
        a.height = WrapS16(int32_t(a.height) + int32_t(a.velocity));
        a.velocity = WrapS16(int32_t(a.velocity) - ArithmeticShift(a.velocity, shifts_.dampingShift));
    };
    for (int32_t v = -radius; v < 0; ++v) {
        const int32_t lastU = radius + v;
        pair(cell(-radius, v), cell(-radius, v + 1));
        for (int32_t u = -radius + 1; u <= lastU; ++u) {
            pair(cell(u - 1, v), cell(u, v + 1));
            pair(cell(u, v), cell(u, v + 1));
            pair(cell(u, v), cell(u - 1, v));
            integrate(cell(u - 1, v));
        }
        pair(cell(lastU, v), cell(lastU + 1, v + 1));
        integrate(cell(lastU, v));
    }
    for (int32_t v = 0; v < radius; ++v) {
        const int32_t firstU = v - radius;
        for (int32_t u = firstU + 1; u <= radius; ++u) {
            pair(cell(u - 1, v), cell(u, v + 1));
            pair(cell(u, v), cell(u, v + 1));
            pair(cell(u, v), cell(u - 1, v));
            integrate(cell(u - 1, v));
        }
        integrate(cell(radius, v));
    }
    for (int32_t u = 1; u <= radius; ++u) {
        // The unrolled top-edge loop reverses every other pair. Signed shift
        // rounding makes that orientation observable for odd differences.
        if (u & 1) {
            pair(cell(u - 1, radius), cell(u, radius));
        } else {
            pair(cell(u, radius), cell(u - 1, radius));
        }
        integrate(cell(u - 1, radius));
    }
    integrate(cell(radius, radius));
}

SurfaceSample WaveField::Sample(float worldX, float worldZ, float meanHeight) const {
    if (!CoordinateRepresentable(worldX + worldZ * kOneOverSqrt3) ||
        !CoordinateRepresentable(worldZ * kTwoOverSqrt3) || !std::isfinite(meanHeight)) {
        return {};
    }
    // Domain stamps must be rejected before the cartridge torus can alias a
    // distant tile into a local cell. Preserve original modulo outside adapters.
    if (clampSources_) {
        const int32_t rawU = ArithmeticShift(FixedCoordinate(worldX + worldZ * kOneOverSqrt3), 6);
        const int32_t rawV = ArithmeticShift(FixedCoordinate(worldZ * kTwoOverSqrt3), 6);
        const int64_t du = int64_t(rawU) - windowU_, dv = int64_t(rawV) - windowV_;
        if (std::abs(du) > radius_ + 1 || std::abs(dv) > radius_ + 1 ||
            std::abs(du - dv) > radius_ + 1) return {};
    }
    const LatticePoint p = ToLattice(worldX, worldZ);
    const float h00 = float(ArithmeticShift(cells_[Index(p.u, p.v)].height, 8));
    const float h10 = float(ArithmeticShift(cells_[Index(p.u + 1, p.v)].height, 8));
    const float h01 = float(ArithmeticShift(cells_[Index(p.u, p.v + 1)].height, 8));
    const float h11 = float(ArithmeticShift(cells_[Index(p.u + 1, p.v + 1)].height, 8));

    float displacement;
    float du;
    float dv;
    if (p.fv <= p.fu) {
        // Triangle (00, 10, 11), matching the game's frac-v <= frac-u split.
        displacement = h00 * (1.0f - p.fu) + h10 * (p.fu - p.fv) + h11 * p.fv;
        du = h10 - h00;
        dv = h11 - h10;
    } else {
        // Triangle (00, 01, 11).
        displacement = h00 * (1.0f - p.fv) + h01 * (p.fv - p.fu) + h11 * p.fu;
        du = h11 - h01;
        dv = h01 - h00;
    }

    // Convert lattice derivatives into world XZ derivatives. u=x+z/sqrt(3),
    // v=2z/sqrt(3), each coordinate advancing 64 units per cell.
    const float dhdu = du / kCellPitch;
    const float dhdv = dv / kCellPitch;
    const float dhdx = dhdu;
    const float dhdz = dhdu * kOneOverSqrt3 + dhdv * kTwoOverSqrt3;
    const float length = std::sqrt(dhdx * dhdx + 1.0f + dhdz * dhdz);
    return {true, meanHeight + displacement, -dhdx / length, 1.0f / length, -dhdz / length};
}

RawCellState WaveField::ReadCell(int32_t uCell, int32_t vCell) const {
    const Cell& cell = cells_[Index(uCell, vCell)];
    return {cell.height, cell.velocity};
}

bool WaveField::ReadCellByFieldByteOffset(uint32_t fieldByteOffset, RawCellState& state) const {
    constexpr uint32_t kBytesPerCell = 4;
    constexpr uint32_t kFieldBytes = kCells * kBytesPerCell;
    if (fieldByteOffset >= kFieldBytes || (fieldByteOffset % kBytesPerCell) != 0) {
        return false;
    }
    const unsigned packedIndex = fieldByteOffset / kBytesPerCell;
    const Cell& cell = cells_[packedIndex];
    state = {cell.height, cell.velocity};
    return true;
}

} // namespace LakeWater
