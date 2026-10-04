#include "ShoreProfile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace LakeWater {
namespace {

constexpr int64_t kMaxCoordinate = 1'000'000;
constexpr std::size_t kMaxInputItems = 512;

struct IntegerEdge {
    PointXZ a;
    PointXZ b;
};

bool PointLess(const PointXZ& a, const PointXZ& b) {
    return a.x < b.x || (a.x == b.x && a.z < b.z);
}

bool SamePoint(const PointXZ& a, const PointXZ& b) {
    return a.x == b.x && a.z == b.z;
}

bool ValidPoint(const PointXZ& point) {
    return point.x >= -kMaxCoordinate && point.x <= kMaxCoordinate &&
           point.z >= -kMaxCoordinate && point.z <= kMaxCoordinate;
}

int64_t Cross(const PointXZ& a, const PointXZ& b, const PointXZ& p) {
    return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
}

bool OnSegment(const PointXZ& point, const PointXZ& a, const PointXZ& b) {
    if (Cross(a, b, point) != 0) return false;
    return point.x >= std::min(a.x, b.x) && point.x <= std::max(a.x, b.x) &&
           point.z >= std::min(a.z, b.z) && point.z <= std::max(a.z, b.z);
}

using SegmentKey = std::array<int64_t, 4>;

SegmentKey MakeSegmentKey(PointXZ a, PointXZ b) {
    if (PointLess(b, a)) std::swap(a, b);
    return {a.x, a.z, b.x, b.z};
}

void MergeCollinearSegments(std::vector<SegmentKey>* segments) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < segments->size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < segments->size(); ++j) {
                const SegmentKey first = (*segments)[i];
                const SegmentKey second = (*segments)[j];
                const PointXZ firstA{first[0], first[1]}, firstB{first[2], first[3]};
                const PointXZ secondA{second[0], second[1]}, secondB{second[2], second[3]};
                PointXZ shared{};
                PointXZ firstOther{};
                PointXZ secondOther{};
                if (SamePoint(firstA, secondA)) {
                    shared = firstA; firstOther = firstB; secondOther = secondB;
                } else if (SamePoint(firstA, secondB)) {
                    shared = firstA; firstOther = firstB; secondOther = secondA;
                } else if (SamePoint(firstB, secondA)) {
                    shared = firstB; firstOther = firstA; secondOther = secondB;
                } else if (SamePoint(firstB, secondB)) {
                    shared = firstB; firstOther = firstA; secondOther = secondA;
                } else {
                    continue;
                }
                if (Cross(shared, firstOther, secondOther) != 0) continue;
                const int64_t firstX = firstOther.x - shared.x;
                const int64_t firstZ = firstOther.z - shared.z;
                const int64_t secondX = secondOther.x - shared.x;
                const int64_t secondZ = secondOther.z - shared.z;
                if (firstX * secondX + firstZ * secondZ > 0) continue;
                (*segments)[i] = MakeSegmentKey(firstOther, secondOther);
                segments->erase(segments->begin() + j);
                changed = true;
                break;
            }
        }
    }
}

bool IsOnFlowSegment(const PointXZ& a, const PointXZ& b, const std::vector<ShoreSegment>& flowEdges) {
    for (const ShoreSegment& flow : flowEdges) {
        if (OnSegment(a, flow.a, flow.b) && OnSegment(b, flow.a, flow.b)) return true;
    }
    return false;
}

bool PointInsideTriangle(double x, double z, const ShoreProfile::DoubleTriangle& triangle) {
    const auto cross = [x, z](const std::array<double, 2>& a, const std::array<double, 2>& b) {
        return (b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0]);
    };
    const double c0 = cross(triangle.points[0], triangle.points[1]);
    const double c1 = cross(triangle.points[1], triangle.points[2]);
    const double c2 = cross(triangle.points[2], triangle.points[0]);
    constexpr double kEpsilon = 1.0e-9;
    return (c0 >= -kEpsilon && c1 >= -kEpsilon && c2 >= -kEpsilon) ||
           (c0 <= kEpsilon && c1 <= kEpsilon && c2 <= kEpsilon);
}

double DistanceSquared(double x, double z, const ShoreProfile::DoubleSegment& segment) {
    const double dx = segment.bx - segment.ax;
    const double dz = segment.bz - segment.az;
    const double lengthSquared = dx * dx + dz * dz;
    if (lengthSquared == 0.0) return (x - segment.ax) * (x - segment.ax) + (z - segment.az) * (z - segment.az);
    const double t = std::clamp(((x - segment.ax) * dx + (z - segment.az) * dz) / lengthSquared, 0.0, 1.0);
    const double nearestX = segment.ax + t * dx;
    const double nearestZ = segment.az + t * dz;
    const double offsetX = x - nearestX;
    const double offsetZ = z - nearestZ;
    return offsetX * offsetX + offsetZ * offsetZ;
}

} // namespace

bool ShoreProfile::Initialize(const std::vector<WetTriangle>& wetTriangles,
                              const std::vector<ShoreSegment>& flowEdges) {
    initialized_ = false;
    triangles_.clear();
    for (auto& segments : shoreSegments_) segments.clear();
    if (wetTriangles.empty() || wetTriangles.size() > kMaxInputItems || flowEdges.size() > kMaxInputItems) {
        return false;
    }

    std::array<std::vector<IntegerEdge>, 3> edgesByLayer;
    std::array<std::vector<PointXZ>, 3> pointsByLayer;
    triangles_.reserve(wetTriangles.size());

    for (const WetTriangle& triangle : wetTriangles) {
        if (triangle.layer >= 3) return false;
        for (const PointXZ& point : triangle.points) {
            if (!ValidPoint(point)) return false;
        }
        if (Cross(triangle.points[0], triangle.points[1], triangle.points[2]) == 0) return false;
        DoubleTriangle converted{};
        converted.layer = triangle.layer;
        for (std::size_t i = 0; i < triangle.points.size(); ++i) {
            converted.points[i] = {static_cast<double>(triangle.points[i].x),
                                   static_cast<double>(triangle.points[i].z)};
        }
        triangles_.push_back(converted);
        for (std::size_t i = 0; i < 3; ++i) {
            const PointXZ& a = triangle.points[i];
            const PointXZ& b = triangle.points[(i + 1) % 3];
            edgesByLayer[triangle.layer].push_back({a, b});
            pointsByLayer[triangle.layer].push_back(a);
            pointsByLayer[triangle.layer].push_back(b);
        }
    }

    for (const ShoreSegment& edge : flowEdges) {
        if (!ValidPoint(edge.a) || !ValidPoint(edge.b) || SamePoint(edge.a, edge.b)) return false;
        for (auto& points : pointsByLayer) {
            points.push_back(edge.a);
            points.push_back(edge.b);
        }
    }

    for (uint8_t layer = 0; layer < 3; ++layer) {
        std::map<SegmentKey, unsigned> pieceUseCount;
        const auto& sourceEdges = edgesByLayer[layer];
        auto& candidatePoints = pointsByLayer[layer];
        std::sort(candidatePoints.begin(), candidatePoints.end(), PointLess);
        candidatePoints.erase(std::unique(candidatePoints.begin(), candidatePoints.end(), SamePoint),
                              candidatePoints.end());
        for (const IntegerEdge& edge : sourceEdges) {
            std::vector<PointXZ> splitPoints;
            for (const PointXZ& point : candidatePoints) {
                if (OnSegment(point, edge.a, edge.b)) splitPoints.push_back(point);
            }
            std::sort(splitPoints.begin(), splitPoints.end(), [&edge](const PointXZ& a, const PointXZ& b) {
                const int64_t adx = a.x - edge.a.x, adz = a.z - edge.a.z;
                const int64_t bdx = b.x - edge.a.x, bdz = b.z - edge.a.z;
                const int64_t aProjection = adx * (edge.b.x - edge.a.x) + adz * (edge.b.z - edge.a.z);
                const int64_t bProjection = bdx * (edge.b.x - edge.a.x) + bdz * (edge.b.z - edge.a.z);
                return aProjection < bProjection;
            });
            splitPoints.erase(std::unique(splitPoints.begin(), splitPoints.end(), SamePoint), splitPoints.end());
            for (std::size_t i = 0; i + 1 < splitPoints.size(); ++i) {
                if (SamePoint(splitPoints[i], splitPoints[i + 1])) continue;
                ++pieceUseCount[MakeSegmentKey(splitPoints[i], splitPoints[i + 1])];
            }
        }

        std::vector<SegmentKey> boundaryPieces;
        for (const auto& [key, useCount] : pieceUseCount) {
            if (useCount == 2) continue; // Shared internal edge.
            if (useCount != 1) return false; // Reject a non-manifold shore instead of guessing.
            const PointXZ a{key[0], key[1]};
            const PointXZ b{key[2], key[3]};
            if (IsOnFlowSegment(a, b, flowEdges)) continue;
            boundaryPieces.push_back(key);
        }
        MergeCollinearSegments(&boundaryPieces);
        std::sort(boundaryPieces.begin(), boundaryPieces.end());
        for (const SegmentKey& key : boundaryPieces) {
            shoreSegments_[layer].push_back({static_cast<double>(key[0]), static_cast<double>(key[1]),
                                             static_cast<double>(key[2]), static_cast<double>(key[3])});
        }
    }
    initialized_ = true;
    return true;
}

double ShoreProfile::Evaluate(double x, double z, uint8_t layer) const {
    if (!initialized_ || layer >= 3 || !std::isfinite(x) || !std::isfinite(z)) return 0.0;
    bool inside = false;
    for (const DoubleTriangle& triangle : triangles_) {
        if (triangle.layer == layer && PointInsideTriangle(x, z, triangle)) {
            inside = true;
            break;
        }
    }
    if (!inside) return 0.0;

    double distanceSquared = kShoreFalloffDistance * kShoreFalloffDistance;
    for (const DoubleSegment& segment : shoreSegments_[layer]) {
        distanceSquared = std::min(distanceSquared, DistanceSquared(x, z, segment));
        if (distanceSquared == 0.0) return 0.0;
    }
    const double distance = std::sqrt(distanceSquared);
    const double t = std::clamp(distance / kShoreFalloffDistance, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::size_t ShoreProfile::BoundarySegmentCount(uint8_t layer) const {
    return layer < 3 ? shoreSegments_[layer].size() : 0;
}

} // namespace LakeWater
