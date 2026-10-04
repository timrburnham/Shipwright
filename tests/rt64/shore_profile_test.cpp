#include "../../soh/soh/Enhancements/Graphics/LakeWater/ShoreProfile.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace {
using namespace LakeWater;

WetTriangle Triangle(PointXZ a, PointXZ b, PointXZ c, uint8_t layer = 0) {
    return {{{a, b, c}}, layer};
}

void AddRectangle(std::vector<WetTriangle>& triangles, int64_t x0, int64_t z0, int64_t x1, int64_t z1,
                  uint8_t layer = 0) {
    triangles.push_back(Triangle({x0, z0}, {x1, z0}, {x1, z1}, layer));
    triangles.push_back(Triangle({x0, z0}, {x1, z1}, {x0, z1}, layer));
}

} // namespace

int main() {
    using namespace LakeWater;

    // A triangulation diagonal is internal, so it cannot create a false bank.
    std::vector<WetTriangle> rectangle;
    AddRectangle(rectangle, 0, 0, 400, 400);
    ShoreProfile profile;
    assert(profile.Initialize(rectangle));
    assert(profile.BoundarySegmentCount(0) == 4);
    assert(profile.BoundarySegmentCount(1) == 0);
    assert(profile.Evaluate(200.0, 200.0, 0) == 1.0);
    assert(profile.Evaluate(200.0, 200.0, 1) == 0.0);
    assert(profile.Evaluate(-1.0, 200.0, 0) == 0.0);

    // The outer bank starts at zero; smoothstep reaches one at 128 units.
    assert(profile.Evaluate(0.0, 200.0, 0) == 0.0);
    assert(std::abs(profile.Evaluate(64.0, 200.0, 0) - 0.5) < 1.0e-12);
    assert(profile.Evaluate(128.0, 200.0, 0) == 1.0);
    const double below = profile.Evaluate(127.0, 200.0, 0);
    const double above = profile.Evaluate(129.0, 200.0, 0);
    assert(below <= above && above - below < 0.001);
    assert(profile.Evaluate(std::numeric_limits<double>::quiet_NaN(), 20.0, 0) == 0.0);

    // A long shared edge is subdivided on only one side. Exact splitting at
    // the other side's midpoint still removes both pieces from the shore.
    std::vector<WetTriangle> partialSharedEdge = {
        Triangle({0, 0}, {200, 0}, {200, 200}),
        Triangle({0, 0}, {200, 200}, {0, 200}),
        Triangle({300, 100}, {200, 0}, {400, 0}),
        Triangle({300, 100}, {400, 0}, {400, 200}),
        Triangle({300, 100}, {400, 200}, {200, 200}),
        Triangle({300, 100}, {200, 200}, {200, 100}),
        Triangle({300, 100}, {200, 100}, {200, 0}),
    };
    assert(profile.Initialize(partialSharedEdge));
    assert(profile.BoundarySegmentCount(0) == 4);
    assert(profile.Evaluate(200.0, 100.0, 0) > 0.8);

    // A fall/flow edge that overlaps a complete shore edge is removed from
    // the attenuation boundary, preserving a wet connection at the outlet.
    const std::vector<ShoreSegment> riverOutlet = {{{0, 0}, {400, 0}}};
    assert(profile.Initialize(rectangle, riverOutlet));
    assert(profile.BoundarySegmentCount(0) == 3);
    assert(profile.Evaluate(200.0, 0.0, 0) == 1.0);
    assert(profile.Evaluate(200.0, 0.0, 1) == 0.0);

    // Boundary extraction preserves both concave exterior banks and hole
    // shores; points outside a layer's triangle union always evaluate to zero.
    std::vector<WetTriangle> ring;
    AddRectangle(ring, 0, 0, 400, 150);
    AddRectangle(ring, 0, 250, 400, 400);
    AddRectangle(ring, 0, 150, 150, 250);
    AddRectangle(ring, 250, 150, 400, 250);
    assert(profile.Initialize(ring));
    assert(profile.Evaluate(200.0, 200.0, 0) == 0.0);
    assert(profile.Evaluate(150.0, 200.0, 0) == 0.0);
    assert(profile.Evaluate(149.0, 200.0, 0) > 0.0);
    assert(profile.BoundarySegmentCount(0) == 8);

    std::vector<WetTriangle> concave;
    AddRectangle(concave, 0, 0, 200, 400);
    AddRectangle(concave, 200, 0, 400, 200);
    assert(profile.Initialize(concave));
    assert(profile.Evaluate(300.0, 300.0, 0) == 0.0);
    assert(profile.Evaluate(200.0, 200.0, 0) == 0.0);
    assert(profile.Evaluate(190.0, 190.0, 0) > 0.0);

    // Invalid layers, degenerate triangles, overlarge exact coordinates, and
    // malformed flow edges fail closed and leave the profile unusable.
    assert(!profile.Initialize({}));
    assert(!profile.IsInitialized());
    assert(profile.Evaluate(10.0, 10.0, 0) == 0.0);
    assert(!profile.Initialize({Triangle({0, 0}, {1, 1}, {2, 2})}));
    assert(!profile.Initialize({Triangle({0, 0}, {1, 0}, {0, 1}, 3)}));
    assert(!profile.Initialize({Triangle({0, 0}, {1, 0}, {0, 1})}, {{{1, 1}, {1, 1}}}));
    assert(!profile.Initialize({Triangle({0, 0}, {1, 0}, {0, 1})}, {{{1'000'001, 0}, {0, 0}}}));

    return 0;
}
