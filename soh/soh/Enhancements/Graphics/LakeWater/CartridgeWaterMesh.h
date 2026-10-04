#ifndef SOH_CARTRIDGE_WATER_MESH_H
#define SOH_CARTRIDGE_WATER_MESH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace LakeWater::Mesh {

constexpr size_t kVertexCacheSize = 32;
using GlobalTriangle = std::array<uint32_t, 3>;
using LocalTriangle = std::array<uint8_t, 3>;

struct Packet {
    // Global vertex IDs in the order loaded into the hardware vertex cache.
    std::vector<uint32_t> vertexIDs;
    // Input-order triangles, rewritten to indices into vertexIDs.
    std::vector<LocalTriangle> triangles;
};

/*
 * Greedily packetize triangles in their original order for the 32-entry
 * F3DEX2 vertex cache. Vertices are added to each packet in first-use order;
 * a packet closes only when the next triangle would require a 33rd unique
 * vertex. Degenerate and repeated-index triangles are preserved verbatim.
 *
 * Returns false for an out-of-range global vertex ID and leaves `output`
 * untouched. On success, replaces `output`; empty input produces no packets.
 */
inline bool BuildGreedyCachePackets(const std::vector<GlobalTriangle>& input, size_t vertexCount,
                                    std::vector<Packet>& output) {
    std::vector<Packet> result;
    Packet packet;
    packet.vertexIDs.reserve(kVertexCacheSize);

    for (const GlobalTriangle& triangle : input) {
        for (uint32_t index : triangle) {
            if (static_cast<size_t>(index) >= vertexCount) return false;
        }

        std::array<uint32_t, 3> missing{};
        size_t missingCount = 0;
        for (uint32_t index : triangle) {
            bool foundInPacket = false;
            for (uint32_t cached : packet.vertexIDs) {
                if (cached == index) {
                    foundInPacket = true;
                    break;
                }
            }
            if (foundInPacket) continue;

            bool foundInTriangle = false;
            for (size_t i = 0; i < missingCount; ++i) {
                if (missing[i] == index) {
                    foundInTriangle = true;
                    break;
                }
            }
            if (!foundInTriangle) missing[missingCount++] = index;
        }

        if (!packet.triangles.empty() && packet.vertexIDs.size() + missingCount > kVertexCacheSize) {
            result.push_back(std::move(packet));
            packet = Packet{};
            packet.vertexIDs.reserve(kVertexCacheSize);
        }

        for (uint32_t index : triangle) {
            size_t localIndex = 0;
            while (localIndex < packet.vertexIDs.size() && packet.vertexIDs[localIndex] != index) {
                ++localIndex;
            }
            if (localIndex == packet.vertexIDs.size()) {
                packet.vertexIDs.push_back(index);
            }
        }

        LocalTriangle local{};
        for (size_t corner = 0; corner < triangle.size(); ++corner) {
            size_t localIndex = 0;
            while (packet.vertexIDs[localIndex] != triangle[corner]) ++localIndex;
            local[corner] = static_cast<uint8_t>(localIndex);
        }
        packet.triangles.push_back(local);
    }

    if (!packet.triangles.empty()) result.push_back(std::move(packet));
    output = std::move(result);
    return true;
}

} // namespace LakeWater::Mesh

#endif // SOH_CARTRIDGE_WATER_MESH_H
