#pragma once

#include "CartridgeWaveSource.h"

#include <cstddef>
#include <vector>

namespace LakeWater {

struct LakeXZ {
    double x;
    double z;
};

// Integer center and radius in the original 64-unit oblique lattice. The
// source integrator's active region is |du|, |dv|, |du-dv| <= radius.
struct LakeDomain {
    int32_t centerU = 0;
    int32_t centerV = 0;
    unsigned radius = 0;
    double minX = 0.0;
    double maxX = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
};

// Computes the smallest even hex radius around the midpoint of the XZ bounds
// that contains every supplied footprint vertex, plus `marginCells`. Returns
// false for invalid points or domains that would exceed the non-aliasing
// radius supported by WaveField::SetDomain (126).
bool ComputeLakeDomain(const LakeXZ* footprint, size_t pointCount, unsigned marginCells, LakeDomain& out);

// Tests a point/cell against the same hex predicate used by the integrator.
bool LakeDomainContainsWorldPoint(const LakeDomain& domain, double x, double z);
bool LakeDomainContainsCell(const LakeDomain& domain, int32_t uCell, int32_t vCell);

// Extends a ROM-decoded source over the domain using its original across/row
// basis. Each copy is translated by acrossCount*across and rowCount*row;
// row-tile phase is advanced by the same number of original row steps so
// adjacent copies remain phase-continuous. The caller should clamp individual
// source stamps with LakeDomainContainsWorldPoint before writing the field.
// `maxCopies` bounds accidental pathological allocations.
bool TileCartridgeWaveSource(const CartridgeWaveSource& source, const LakeDomain& domain,
                             std::vector<CartridgeWaveSource>& copies, size_t maxCopies = 4096);

// Repeat an entire course source layout with common lattice translations.
// Preserve relative positions, phases, and source ordering; independently
// filling the lake with each patch overlays patterns absent from the course.
bool TileCartridgeSourceLayout(const std::vector<CartridgeWaveSource>& sources,
                               const LakeDomain& domain, double originX, double originZ,
                               std::vector<CartridgeWaveSource>& copies, size_t maxCopies = 4096);

} // namespace LakeWater
