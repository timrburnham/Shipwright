#include "../../soh/soh/Enhancements/Graphics/LakeWater/LakeDomain.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace LakeWater;

    // Measured high-water bounds supplied by the Lake Hylia footprint census.
    // This test uses only the bounds, not ROM-derived vertex/source payloads.
    const LakeXZ footprint[] = {
        {-3618.0, -2808.0}, {-3618.0, 4231.0},
        {4114.0, -2808.0}, {4114.0, 4231.0},
    };
    LakeDomain domain;
    assert(ComputeLakeDomain(footprint, 4, 2, domain));
    assert(domain.centerU == 10 && domain.centerV == 13);
    assert(domain.radius == 96);
    for (const LakeXZ& point : footprint) {
        assert(LakeDomainContainsWorldPoint(domain, point.x, point.z));
    }
    assert(!LakeDomainContainsWorldPoint(domain, 20000.0, 20000.0));

    // The axis-aligned bounds' corners are the worst points under the oblique
    // hex metric, so the selected even-radius domain has no uncovered corner.
    for (int z = 0; z <= 8; ++z) {
        for (int x = 0; x <= 8; ++x) {
            const double px = footprint[0].x + (footprint[2].x - footprint[0].x) * x / 8.0;
            const double pz = footprint[0].z + (footprint[1].z - footprint[0].z) * z / 8.0;
            assert(LakeDomainContainsWorldPoint(domain, px, pz));
        }
    }

    CartridgeWaveSource source{};
    source.x = -3618.0f;
    source.z = -2808.0f;
    source.acrossX = 64.0f;
    source.acrossZ = 0.0f;
    source.rowX = 0.0f;
    source.rowZ = 64.0f;
    source.amplitude = 2.0f;
    source.acrossCount = 4;
    source.rowCount = 3;
    source.phaseAdvance = 3;
    source.phase = 11;
    source.speed = 1;
    source.period = 32;
    source.minPhase = 0;
    source.maxPhase = 32;

    std::vector<CartridgeWaveSource> tiled;
    assert(TileCartridgeWaveSource(source, domain, tiled));
    assert(tiled.size() > 100 && tiled.size() < 4096);
    for (const CartridgeWaveSource& copy : tiled) {
        assert(std::isfinite(copy.x) && std::isfinite(copy.z));
        // All original line stamps lie in the bounded non-aliased field range;
        // WaveField then rejects stamps outside the fixed domain hex.
        for (int row = 0; row < copy.rowCount; ++row) {
            for (int across = 0; across < copy.acrossCount; ++across) {
                const double x = copy.x + across * copy.acrossX + row * copy.rowX;
                const double z = copy.z + across * copy.acrossZ + row * copy.rowZ;
                const double u = (x + z * 0.57735026) / 64.0;
                const double v = z * 1.1547005 / 64.0;
                assert(std::abs(u) < 384.0 && std::abs(v) < 384.0);
            }
        }
    }

    // The periodic copies tile the entire measured footprint with no source
    // gap larger than one lattice-cell diagonal.
    for (int iz = 0; iz <= 8; ++iz) {
        for (int ix = 0; ix <= 8; ++ix) {
            const double px = footprint[0].x + (footprint[2].x - footprint[0].x) * ix / 8.0;
            const double pz = footprint[0].z + (footprint[1].z - footprint[0].z) * iz / 8.0;
            double nearestSquared = std::numeric_limits<double>::infinity();
            for (const CartridgeWaveSource& copy : tiled) {
                for (int row = 0; row < copy.rowCount; ++row) {
                    for (int across = 0; across < copy.acrossCount; ++across) {
                        const double dx = px - (copy.x + across * copy.acrossX + row * copy.rowX);
                        const double dz = pz - (copy.z + across * copy.acrossZ + row * copy.rowZ);
                        nearestSquared = std::min(nearestSquared, dx * dx + dz * dz);
                    }
                }
            }
            assert(nearestSquared < 50.0 * 50.0);
        }
    }

    // Find adjacent row tiles with the same across-tile offset. Advancing the
    // row tile shifts initial phase by rowCount*phaseAdvance, so the next tile's
    // first row follows directly after the previous tile's final row.
    bool foundSeam = false;
    constexpr int64_t periodFixed = 32 * 2048;
    for (const CartridgeWaveSource& a : tiled) {
        for (const CartridgeWaveSource& b : tiled) {
            if (std::abs((b.x - a.x) - source.rowCount * source.rowX) < 0.01f &&
                std::abs((b.z - a.z) - source.rowCount * source.rowZ) < 0.01f) {
                const int64_t expected = (a.phase + source.rowCount * source.phaseAdvance * 4) % periodFixed;
                assert(b.phase == expected);
                foundSeam = true;
                break;
            }
        }
        if (foundSeam) break;
    }
    assert(foundSeam);

    std::vector<CartridgeWaveSource> unchanged = tiled;
    assert(!TileCartridgeWaveSource(source, domain, tiled, 1));
    assert(tiled.size() == unchanged.size());

    // Layout repetition must retain the geography and phase of distinct
    // source patches, rather than independently overlaying each everywhere.
    CartridgeWaveSource second = source;
    second.x += 1024.0f;
    second.z += 512.0f;
    second.phase = 23;
    std::vector<CartridgeWaveSource> layout;
    assert(TileCartridgeSourceLayout({source, second}, domain, 0, 0, layout));
    bool foundOriginal = false;
    for (size_t i = 0; i + 1 < layout.size(); i += 2) {
        const auto& first = layout[i];
        const auto& next = layout[i + 1];
        assert(std::abs((next.x - first.x) - (second.x - source.x)) < 0.01f);
        assert(std::abs((next.z - first.z) - (second.z - source.z)) < 0.01f);
        assert(first.phase == source.phase && next.phase == second.phase);
        assert(first.speed == source.speed && next.amplitude == second.amplitude);
        foundOriginal |= std::abs(first.x - source.x) < 0.01f && std::abs(first.z - source.z) < 0.01f;
    }
    assert(foundOriginal);
    const auto priorLayout = layout;
    assert(!TileCartridgeSourceLayout({source, second}, domain, 0, 0, layout, 1));
    assert(layout.size() == priorLayout.size());
    assert(!TileCartridgeSourceLayout({}, domain, 0, 0, layout));
    assert(!TileCartridgeSourceLayout({source}, domain, NAN, 0, layout));

    CartridgeWaveSource remoteSource = source;
    remoteSource.x = std::numeric_limits<float>::max();
    assert(!TileCartridgeSourceLayout({remoteSource}, domain, 0, 0, layout));

    const LakeXZ invalid[] = {{0.0, 0.0}, {1.0, 1.0}, {NAN, 2.0}};
    assert(!ComputeLakeDomain(invalid, 3, 0, domain));
    CartridgeWaveSource degenerate = source;
    degenerate.rowX = degenerate.rowZ = 0.0f;
    assert(!TileCartridgeWaveSource(degenerate, domain, tiled));
}
