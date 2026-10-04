#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace LakeWater {

struct PointXZ {
    int64_t x = 0;
    int64_t z = 0;
};

struct WetTriangle {
    std::array<PointXZ, 3> points{};
    uint8_t layer = 0;
};

struct ShoreSegment {
    PointXZ a{};
    PointXZ b{};
};

/*
 * Builds a layer-aware union mask and its exact outer/hole shore edges.
 * Integer coordinates keep shared and partially subdivided resource edges
 * exact. Flow edges suppress attenuation only where they overlap that shore.
 */
class ShoreProfile {
  public:
    struct DoubleTriangle {
        std::array<double, 2> points[3];
        uint8_t layer;
    };
    struct DoubleSegment {
        double ax, az, bx, bz;
    };

    static constexpr double kShoreFalloffDistance = 128.0;

    bool Initialize(const std::vector<WetTriangle>& wetTriangles,
                    const std::vector<ShoreSegment>& flowEdges = {});
    double Evaluate(double x, double z, uint8_t layer) const;

    bool IsInitialized() const { return initialized_; }
    std::size_t BoundarySegmentCount(uint8_t layer) const;

  private:
    bool initialized_ = false;
    std::vector<DoubleTriangle> triangles_;
    std::array<std::vector<DoubleSegment>, 3> shoreSegments_;
};

} // namespace LakeWater
