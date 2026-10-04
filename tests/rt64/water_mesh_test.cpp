#include "../../soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaterMesh.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using LakeWater::Mesh::BuildGreedyCachePackets;
using LakeWater::Mesh::GlobalTriangle;
using LakeWater::Mesh::Packet;

static std::vector<GlobalTriangle> Reconstruct(const std::vector<Packet>& packets) {
    std::vector<GlobalTriangle> result;
    for (const Packet& packet : packets) {
        assert(packet.vertexIDs.size() <= LakeWater::Mesh::kVertexCacheSize);
        for (const auto& triangle : packet.triangles) {
            GlobalTriangle global{};
            for (size_t corner = 0; corner < 3; ++corner) {
                assert(triangle[corner] < packet.vertexIDs.size());
                global[corner] = packet.vertexIDs[triangle[corner]];
            }
            result.push_back(global);
        }
    }
    return result;
}

int main() {
    std::vector<Packet> packets;

    // Empty input is a successful empty packetization.
    assert(BuildGreedyCachePackets({}, 0, packets));
    assert(packets.empty());

    // Repeated and degenerate indices survive exactly, including winding and
    // the original triangle order.
    const std::vector<GlobalTriangle> repeated = {
        {7, 7, 7}, {7, 2, 7}, {2, 7, 9}, {2, 7, 9}, {9, 9, 2},
    };
    assert(BuildGreedyCachePackets(repeated, 10, packets));
    assert(packets.size() == 1);
    assert(packets[0].vertexIDs == (std::vector<uint32_t>{7, 2, 9}));
    assert(Reconstruct(packets) == repeated);

    // Greedy exact-boundary case: ten disjoint triangles fill 30 slots; the
    // next disjoint triangle starts a new packet instead of exceeding 32.
    std::vector<GlobalTriangle> thirtyThreeUnique;
    for (uint32_t i = 0; i < 11; ++i) {
        thirtyThreeUnique.push_back({i * 3, i * 3 + 1, i * 3 + 2});
    }
    assert(BuildGreedyCachePackets(thirtyThreeUnique, 33, packets));
    assert(packets.size() == 2);
    assert(packets[0].vertexIDs.size() == 30);
    assert(packets[0].triangles.size() == 10);
    assert(packets[1].vertexIDs.size() == 3);
    assert(packets[1].triangles.size() == 1);
    assert(Reconstruct(packets) == thirtyThreeUnique);

    // The next triangle would take the packet from 30 to 33 unique IDs, so it
    // flushes before that triangle; subsequent sharing is preserved.
    std::vector<GlobalTriangle> boundary;
    for (uint32_t i = 0; i < 10; ++i) {
        boundary.push_back({i * 3, i * 3 + 1, i * 3 + 2});
    }
    boundary.push_back({30, 31, 32});
    boundary.push_back({32, 33, 34});
    assert(BuildGreedyCachePackets(boundary, 35, packets));
    assert(packets.size() == 2);
    assert(packets[0].vertexIDs.size() == 30);
    assert(packets[1].vertexIDs.size() == 5);
    assert(Reconstruct(packets) == boundary);

    // Strong failure behavior: an invalid source reference does not replace
    // the caller's prior result.
    const std::vector<Packet> previous = packets;
    const std::vector<GlobalTriangle> invalid = {{0, 1, 2}, {3, 4, 3}};
    assert(!BuildGreedyCachePackets(invalid, 4, packets));
    assert(packets.size() == previous.size());
    assert(Reconstruct(packets) == Reconstruct(previous));

    return 0;
}
