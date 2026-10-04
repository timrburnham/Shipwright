#include "LakeDomain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace LakeWater {
namespace {
constexpr double kOneOverSqrt3 = 0.57735026;
constexpr double kTwoOverSqrt3 = 1.1547005;
constexpr double kPitch = 64.0;
constexpr int32_t kLargestUnaliasedCellCoordinate = 384;

double RoundToNearestCell(double value) {
    return std::floor(value + 0.5);
}

double HexDistance(double du, double dv) {
    return std::max({std::abs(du), std::abs(dv), std::abs(du - dv)});
}

bool IsFiniteSource(const CartridgeWaveSource& source) {
    return std::isfinite(source.x) && std::isfinite(source.z) && std::isfinite(source.acrossX) &&
           std::isfinite(source.acrossZ) && std::isfinite(source.rowX) && std::isfinite(source.rowZ) &&
           std::isfinite(source.amplitude) && source.acrossCount > 0 && source.rowCount > 0 &&
           source.acrossCount <= 128 && source.rowCount <= 128 && source.period > 0 && source.period <= 128;
}

bool IsValidDomain(const LakeDomain& domain) {
    return domain.radius >= 2 && domain.radius <= 126 &&
           std::abs(domain.centerU) <= 384 && std::abs(domain.centerV) <= 384 &&
           std::isfinite(domain.minX) && std::isfinite(domain.maxX) &&
           std::isfinite(domain.minZ) && std::isfinite(domain.maxZ) &&
           domain.minX <= domain.maxX && domain.minZ <= domain.maxZ;
}

bool InsideUnaliasedField(double x, double z) {
    const double u = (x + z * kOneOverSqrt3) / kPitch;
    const double v = (z * kTwoOverSqrt3) / kPitch;
    return std::isfinite(u) && std::isfinite(v) && std::abs(u) < kLargestUnaliasedCellCoordinate &&
           std::abs(v) < kLargestUnaliasedCellCoordinate;
}

bool PatchInsideUnaliasedField(const CartridgeWaveSource& source) {
    const double acrossEnd = source.acrossCount - 1.0;
    const double rowEnd = source.rowCount - 1.0;
    const LakeXZ corners[] = {
        {source.x, source.z},
        {source.x + acrossEnd * source.acrossX, source.z + acrossEnd * source.acrossZ},
        {source.x + rowEnd * source.rowX, source.z + rowEnd * source.rowZ},
        {source.x + acrossEnd * source.acrossX + rowEnd * source.rowX,
         source.z + acrossEnd * source.acrossZ + rowEnd * source.rowZ},
    };
    return std::all_of(std::begin(corners), std::end(corners), [](const LakeXZ& p) {
        return InsideUnaliasedField(p.x, p.z);
    });
}

} // namespace

bool ComputeLakeDomain(const LakeXZ* footprint, size_t pointCount, unsigned marginCells, LakeDomain& out) {
    if (footprint == nullptr || pointCount < 3 || marginCells > 126) {
        return false;
    }
    LakeDomain candidate{};
    candidate.minX = candidate.maxX = footprint[0].x;
    candidate.minZ = candidate.maxZ = footprint[0].z;
    for (size_t i = 0; i < pointCount; ++i) {
        const LakeXZ& p = footprint[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.z)) {
            return false;
        }
        candidate.minX = std::min(candidate.minX, p.x);
        candidate.maxX = std::max(candidate.maxX, p.x);
        candidate.minZ = std::min(candidate.minZ, p.z);
        candidate.maxZ = std::max(candidate.maxZ, p.z);
    }

    const double centerX = (candidate.minX + candidate.maxX) * 0.5;
    const double centerZ = (candidate.minZ + candidate.maxZ) * 0.5;
    const double centerU = RoundToNearestCell((centerX + centerZ * kOneOverSqrt3) / kPitch);
    const double centerV = RoundToNearestCell((centerZ * kTwoOverSqrt3) / kPitch);
    if (!std::isfinite(centerU) || !std::isfinite(centerV) || std::abs(centerU) > 384 || std::abs(centerV) > 384) {
        return false;
    }
    candidate.centerU = static_cast<int32_t>(centerU);
    candidate.centerV = static_cast<int32_t>(centerV);

    double requiredRadius = 0.0;
    for (size_t i = 0; i < pointCount; ++i) {
        const double u = (footprint[i].x + footprint[i].z * kOneOverSqrt3) / kPitch;
        const double v = (footprint[i].z * kTwoOverSqrt3) / kPitch;
        requiredRadius = std::max(requiredRadius, HexDistance(u - centerU, v - centerV));
    }
    const double withMargin = std::ceil(requiredRadius) + marginCells;
    if (!std::isfinite(withMargin) || withMargin > 126) {
        return false;
    }
    unsigned radius = static_cast<unsigned>(withMargin);
    if (radius & 1u) {
        ++radius;
    }
    if (radius < 2 || radius > 126) {
        return false;
    }
    candidate.radius = radius;
    out = candidate;
    return true;
}

bool LakeDomainContainsCell(const LakeDomain& domain, int32_t uCell, int32_t vCell) {
    return IsValidDomain(domain) &&
           HexDistance(double(uCell) - domain.centerU, double(vCell) - domain.centerV) <= domain.radius;
}

bool LakeDomainContainsWorldPoint(const LakeDomain& domain, double x, double z) {
    if (!std::isfinite(x) || !std::isfinite(z) || !IsValidDomain(domain)) {
        return false;
    }
    const double u = (x + z * kOneOverSqrt3) / kPitch;
    const double v = (z * kTwoOverSqrt3) / kPitch;
    return HexDistance(u - domain.centerU, v - domain.centerV) <= domain.radius;
}

bool TileCartridgeWaveSource(const CartridgeWaveSource& source, const LakeDomain& domain,
                             std::vector<CartridgeWaveSource>& copies, size_t maxCopies) {
    if (!IsFiniteSource(source) || !IsValidDomain(domain) || maxCopies == 0) {
        return false;
    }
    const double acrossX = source.acrossCount * double(source.acrossX);
    const double acrossZ = source.acrossCount * double(source.acrossZ);
    const double rowX = source.rowCount * double(source.rowX);
    const double rowZ = source.rowCount * double(source.rowZ);
    const double determinant = acrossX * rowZ - acrossZ * rowX;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-5) {
        return false;
    }

    const LakeXZ target[] = {
        {domain.minX, domain.minZ}, {domain.minX, domain.maxZ},
        {domain.maxX, domain.minZ}, {domain.maxX, domain.maxZ},
    };
    double minAcross = std::numeric_limits<double>::infinity();
    double maxAcross = -minAcross;
    double minRow = minAcross;
    double maxRow = -minAcross;
    for (const LakeXZ& p : target) {
        const double dx = p.x - source.x;
        const double dz = p.z - source.z;
        const double a = (dx * rowZ - dz * rowX) / determinant;
        const double b = (acrossX * dz - acrossZ * dx) / determinant;
        minAcross = std::min(minAcross, a);
        maxAcross = std::max(maxAcross, a);
        minRow = std::min(minRow, b);
        maxRow = std::max(maxRow, b);
    }
    constexpr double kSafeIndex = double(std::numeric_limits<int64_t>::max() - 4);
    if (!std::isfinite(minAcross) || !std::isfinite(maxAcross) || !std::isfinite(minRow) ||
        !std::isfinite(maxRow) || std::abs(minAcross) > kSafeIndex || std::abs(maxAcross) > kSafeIndex ||
        std::abs(minRow) > kSafeIndex || std::abs(maxRow) > kSafeIndex) {
        return false;
    }

    // A source tile covers a unit interval in each basis coordinate. Include
    // the neighboring tile at each extent so boundary source samples stay
    // covered despite float-to-lattice truncation in the original writer.
    const int64_t firstAcross = static_cast<int64_t>(std::floor(minAcross)) - 1;
    const int64_t lastAcross = static_cast<int64_t>(std::floor(maxAcross)) + 1;
    const int64_t firstRow = static_cast<int64_t>(std::floor(minRow)) - 1;
    const int64_t lastRow = static_cast<int64_t>(std::floor(maxRow)) + 1;
    if (lastAcross < firstAcross || lastRow < firstRow) {
        return false;
    }
    const uint64_t acrossTiles = uint64_t(lastAcross) - uint64_t(firstAcross) + 1;
    const uint64_t rowTiles = uint64_t(lastRow) - uint64_t(firstRow) + 1;
    if (acrossTiles > maxCopies || rowTiles > maxCopies || acrossTiles > maxCopies / rowTiles) {
        return false;
    }

    std::vector<CartridgeWaveSource> result;
    result.reserve(size_t(acrossTiles * rowTiles));
    const int64_t periodFixed = int64_t(source.period) * 2048;
    for (int64_t rowTile = firstRow; rowTile <= lastRow; ++rowTile) {
        for (int64_t acrossTile = firstAcross; acrossTile <= lastAcross; ++acrossTile) {
            CartridgeWaveSource copy = source;
            copy.x = static_cast<float>(double(source.x) + acrossTile * acrossX + rowTile * rowX);
            copy.z = static_cast<float>(double(source.z) + acrossTile * acrossZ + rowTile * rowZ);
            const int64_t rowPhaseOffset = rowTile * int64_t(source.rowCount) * source.phaseAdvance * 4;
            int64_t phase = (int64_t(source.phase) + rowPhaseOffset) % periodFixed;
            if (phase < 0) {
                phase += periodFixed;
            }
            copy.phase = static_cast<int32_t>(phase);
            if (!std::isfinite(copy.x) || !std::isfinite(copy.z) || !PatchInsideUnaliasedField(copy)) {
                // Never let a source coordinate reach the field's torus modulo
                // from a remote copy. The domain stamp clamp handles shoreline
                // cells; this guard handles the unrelated opposite shore.
                continue;
            }
            result.push_back(copy);
        }
    }
    if (result.empty()) {
        return false;
    }
    copies.swap(result);
    return true;
}

bool TileCartridgeSourceLayout(const std::vector<CartridgeWaveSource>& sources,
                               const LakeDomain& domain, double originX, double originZ,
                               std::vector<CartridgeWaveSource>& copies, size_t maxCopies) {
    if (sources.empty() || sources.size() > maxCopies || !IsValidDomain(domain) ||
        !std::isfinite(originX) || !std::isfinite(originZ) || std::abs(originX) > 30000 ||
        std::abs(originZ) > 30000) return false;
    double minU = INFINITY, maxU = -INFINITY, minV = INFINITY, maxV = -INFINITY;
    for (const auto& source : sources) {
        if (!IsFiniteSource(source)) return false;
        const double determinant = double(source.acrossX) * source.rowZ - double(source.acrossZ) * source.rowX;
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-5) return false;
        for (int row : {0, source.rowCount - 1}) for (int across : {0, source.acrossCount - 1}) {
            const double x = source.x + across * double(source.acrossX) + row * double(source.rowX);
            const double z = source.z + across * double(source.acrossZ) + row * double(source.rowZ);
            const double u = (x + z * kOneOverSqrt3) / kPitch;
            const double v = z * kTwoOverSqrt3 / kPitch;
            minU = std::min(minU, u); maxU = std::max(maxU, u);
            minV = std::min(minV, v); maxV = std::max(maxV, v);
        }
    }
    if (!std::isfinite(minU) || !std::isfinite(maxU) || !std::isfinite(minV) || !std::isfinite(maxV) ||
        std::max(std::abs(minU), std::abs(maxU)) >= 384 ||
        std::max(std::abs(minV), std::abs(maxV)) >= 384) return false;
    // Two extra cells separate the group bounds, so neighboring layouts do
    // not hard-set the same fringe triangles simply because they repeat.
    const double periodU = std::ceil(maxU) - std::floor(minU) + 2;
    const double periodV = std::ceil(maxV) - std::floor(minV) + 2;
    if (!std::isfinite(periodU) || !std::isfinite(periodV) || periodU < 2 || periodV < 2 ||
        periodU > 768 || periodV > 768) return false;
    const double offsetU = (originX + originZ * kOneOverSqrt3) / kPitch;
    const double offsetV = originZ * kTwoOverSqrt3 / kPitch;
    const double domainMinU = domain.centerU - double(domain.radius) - 1;
    const double domainMaxU = domain.centerU + double(domain.radius) + 1;
    const double domainMinV = domain.centerV - double(domain.radius) - 1;
    const double domainMaxV = domain.centerV + double(domain.radius) + 1;
    const int firstU = int(std::ceil((domainMinU - offsetU - maxU) / periodU));
    const int lastU = int(std::floor((domainMaxU - offsetU - minU) / periodU));
    const int firstV = int(std::ceil((domainMinV - offsetV - maxV) / periodV));
    const int lastV = int(std::floor((domainMaxV - offsetV - minV) / periodV));
    const int64_t groups = int64_t(lastU - firstU + 1) * (lastV - firstV + 1);
    if (lastU < firstU || lastV < firstV || groups <= 0 ||
        uint64_t(groups) > maxCopies / sources.size()) return false;
    std::vector<CartridgeWaveSource> result;
    result.reserve(size_t(groups) * sources.size());
    constexpr double kSqrt3Over2 = 0.8660254037844386;
    for (int tileV = firstV; tileV <= lastV; ++tileV) {
        for (int tileU = firstU; tileU <= lastU; ++tileU) {
            const double du = tileU * periodU, dv = tileV * periodV;
            for (const auto& source : sources) {
                CartridgeWaveSource copy = source;
                copy.x = float(source.x + originX + 64.0 * du - 32.0 * dv);
                copy.z = float(source.z + originZ + 64.0 * kSqrt3Over2 * dv);
                if (!PatchInsideUnaliasedField(copy)) continue;
                result.push_back(copy);
            }
        }
    }
    if (result.empty()) return false;
    copies.swap(result);
    return true;
}

} // namespace LakeWater
