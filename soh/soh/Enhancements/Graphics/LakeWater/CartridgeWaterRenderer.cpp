#include "global.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterRenderer.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterAssets.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterMesh.h"
#include "soh/Enhancements/Graphics/LakeWater/LakeWater.h"
#include "soh/Enhancements/Graphics/LakeWater/ShoreProfile.h"
#include "soh/Enhancements/Graphics/LakeWater/WaveField.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <ship/Context.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace {
constexpr size_t kTextureBytes = 32 * 32 * 4;
constexpr const char* kTextureRelativePath = "cartridge-water/sunny_beach_water_32x32.rgba";
constexpr size_t kMaxTriangles = 50000;
constexpr size_t kMaxCommandsPerBatch = 3000;
// Hash of the extracted Original Sunny Beach 32x32 RGBA32 block.
constexpr std::array<uint8_t, 32> kTextureSha256 = {
    0x1E, 0xEA, 0xAE, 0xE9, 0x52, 0x80, 0x20, 0x68, 0x57, 0x7A, 0xEE, 0x6B, 0xD6, 0x53, 0xE4, 0xB9,
    0xA6, 0xD1, 0xA1, 0x5C, 0x2F, 0x84, 0x46, 0xA5, 0x6D, 0xD3, 0x78, 0x4D, 0xDD, 0x5C, 0xA0, 0x2A,
};

// Minimal SHA-256 implementation keeps cartridge staging validation independent
// of optional platform crypto libraries.
uint32_t Ror(uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32 - amount));
}
void Sha256(const uint8_t* bytes, size_t length, uint8_t digest[32]) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const uint64_t bitLength = uint64_t(length) * 8;
    const size_t paddedLength = ((length + 9 + 63) / 64) * 64;
    std::vector<uint8_t> padded(paddedLength, 0);
    std::memcpy(padded.data(), bytes, length);
    padded[length] = 0x80;
    for (unsigned i = 0; i < 8; ++i) padded[paddedLength - 1 - i] = uint8_t(bitLength >> (i * 8));
    for (size_t block = 0; block < paddedLength; block += 64) {
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i) {
            const uint8_t* p = padded.data() + block + i * 4;
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }
        for (unsigned i = 16; i < 64; ++i) {
            const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], q=h[7];
        for (unsigned i = 0; i < 64; ++i) {
            const uint32_t s1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = q + s1 + ch + k[i] + w[i];
            const uint32_t s0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            q=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=q;
    }
    for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 4; ++j) digest[i * 4 + j] = uint8_t(h[i] >> (24 - j * 8));
}

bool ReadCheckedAsset(const char* relativePath, size_t size, const uint8_t* expectedDigest,
                      std::vector<uint8_t>& output) {
    const std::string path = Ship::Context::LocateFileAcrossAppDirs(relativePath);
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> candidate(size);
    if (!file.read(reinterpret_cast<char*>(candidate.data()), size) || file.peek() != EOF) return false;
    uint8_t digest[32];
    Sha256(candidate.data(), size, digest);
    if (!std::equal(std::begin(digest), std::end(digest), expectedDigest)) return false;
    output = std::move(candidate);
    return true;
}

bool LoadTexture(const uint8_t** outTexture) {
    static std::mutex mutex;
    static std::vector<uint8_t> texture;
    static bool attempted = false;
    static bool valid = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!attempted) {
        attempted = true;
        valid = ReadCheckedAsset(kTextureRelativePath, kTextureBytes, kTextureSha256.data(), texture);
    }
    if (!valid) return false;
    *outTexture = texture.data();
    return true;
}

struct MeshPoint {
    float x, z;
    int32_t cellU, cellV;
    uint8_t cellTriangle;
    float bary[3];
};
float Cross(const MeshPoint& a, const MeshPoint& b, const MeshPoint& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}
MeshPoint Mix(const MeshPoint& a, const MeshPoint& b, float t) {
    MeshPoint out = a;
    out.x += (b.x - a.x) * t;
    out.z += (b.z - a.z) * t;
    for (unsigned i = 0; i < 3; ++i) out.bary[i] += (b.bary[i] - a.bary[i]) * t;
    return out;
}
MeshPoint LatticePosition(int32_t u, int32_t v, int32_t cellU, int32_t cellV, uint8_t triangle,
                          float b0, float b1, float b2) {
    // Positions are fixed on the same oblique 64-unit lattice queried by
    // WaveField::Sample. Barycentric weights survive static clipping and are
    // evaluated against the published field each frame.
    constexpr float kSqrt3Over2 = 0.8660254037844386f;
    const float x = 64.0f * u - 32.0f * v;
    const float z = 64.0f * kSqrt3Over2 * v;
    return { x, z, cellU, cellV, triangle, { b0, b1, b2 } };
}
struct VertexRecipe {
    float x, z;
    int32_t cellU, cellV;
    uint8_t cellTriangle;
    uint8_t layer;
    bool fall;
    float bary[3];
    float meanWeights[3];
    float shoreFactor;
    float stencilFactors[3][3];
};
struct CachedTriangle {
    std::array<VertexRecipe, 3> vertices;
    uint8_t layer;
};
struct CommandBatch {
    size_t firstPacket;
    size_t packetCount;
    size_t commandCount;
};
struct Topology {
    bool valid = false;
    float actorX = 0.0f;
    float actorZ = 0.0f;
    std::vector<CachedTriangle> triangles;
    LakeWater::ShoreProfile shoreProfile;
    std::unordered_map<uint64_t, std::array<float, 3>> shoreNodeFactors;
    std::unordered_map<uint64_t, std::vector<size_t>> cellTriangleLookup;
    std::vector<VertexRecipe> uniqueRecipes;
    std::vector<LakeWater::Mesh::GlobalTriangle> indexedTriangles;
    std::vector<LakeWater::Mesh::Packet> packets;
    std::vector<size_t> packetVertexOffsets;
    std::vector<CommandBatch> commandBatches;
    size_t packetVertexCount = 0;
};
std::array<Topology, 2> sTopology;
bool sTextureReady = false;
bool sSourceReady = false;
bool sPreflightReady = false;
float sPreflightActorX = 0.0f;
float sPreflightActorZ = 0.0f;
bool sPreflightOriginKnown = false;
bool sDedupOracleChecked = false;
bool sDedupOracleFailed = false;

struct RecipeKey {
    std::array<int64_t, 17> values{};
    bool operator==(const RecipeKey& other) const { return values == other.values; }
};
struct RecipeKeyHash {
    size_t operator()(const RecipeKey& key) const {
        size_t hash = static_cast<size_t>(1469598103934665603ull);
        for (int64_t value : key.values) {
            hash ^= static_cast<size_t>(value);
            hash *= static_cast<size_t>(1099511628211ull);
        }
        return hash;
    }
};

int64_t RecipeFloatBits(float value) {
    if (value == 0.0f) value = 0.0f; // Canonicalize negative zero only.
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

RecipeKey MakeRecipeKey(const VertexRecipe& recipe) {
    struct NodeWeight { int32_t u, v; int64_t weight; };
    int32_t nodeU[3];
    int32_t nodeV[3];
    if (recipe.cellTriangle == 0) {
        nodeU[0] = recipe.cellU; nodeV[0] = recipe.cellV;
        nodeU[1] = recipe.cellU + 1; nodeV[1] = recipe.cellV;
        nodeU[2] = recipe.cellU + 1; nodeV[2] = recipe.cellV + 1;
    } else {
        nodeU[0] = recipe.cellU; nodeV[0] = recipe.cellV;
        nodeU[1] = recipe.cellU; nodeV[1] = recipe.cellV + 1;
        nodeU[2] = recipe.cellU + 1; nodeV[2] = recipe.cellV + 1;
    }
    std::array<NodeWeight, 3> terms{};
    size_t termCount = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (recipe.bary[i] != 0.0f) {
            terms[termCount++] = {nodeU[i], nodeV[i], RecipeFloatBits(recipe.bary[i])};
        }
    }
    std::sort(terms.begin(), terms.begin() + termCount, [](const NodeWeight& a, const NodeWeight& b) {
        return a.u < b.u || (a.u == b.u && a.v < b.v);
    });

    RecipeKey key{};
    key.values[0] = RecipeFloatBits(recipe.x);
    key.values[1] = RecipeFloatBits(recipe.z);
    key.values[2] = recipe.layer;
    key.values[3] = recipe.fall ? 1 : 0;
    for (size_t i = 0; i < 3; ++i) {
        key.values[4 + i * 2] = i < termCount ? terms[i].u : std::numeric_limits<int32_t>::min();
        key.values[5 + i * 2] = i < termCount ? terms[i].v : std::numeric_limits<int32_t>::min();
        key.values[10 + i] = i < termCount ? terms[i].weight : 0;
        key.values[13 + i] = RecipeFloatBits(recipe.meanWeights[i]);
    }
    key.values[16] = RecipeFloatBits(recipe.shoreFactor);
    return key;
}

uint64_t PackLatticeCell(int32_t u, int32_t v) {
    return (uint64_t(static_cast<uint32_t>(u)) << 32) | static_cast<uint32_t>(v);
}

float ShoreFactorForWeights(const LakeWater::ShoreProfile& profile, float x, float z,
                            const float meanWeights[3]) {
    float factor = 0.0f;
    for (uint8_t layer = 0; layer < 3; ++layer) {
        factor += meanWeights[layer] * static_cast<float>(profile.Evaluate(x, z, layer));
    }
    return std::clamp(factor, 0.0f, 1.0f);
}

void CacheShoreNodeFactors(Topology* topology, int32_t cellU, int32_t cellV) {
    for (int32_t v = cellV - 1; v <= cellV + 2; ++v) {
        for (int32_t u = cellU - 1; u <= cellU + 2; ++u) {
            const uint64_t key = PackLatticeCell(u, v);
            if (topology->shoreNodeFactors.find(key) != topology->shoreNodeFactors.end()) continue;
            const MeshPoint node = LatticePosition(u, v, u, v, 0, 1.0f, 0.0f, 0.0f);
            std::array<float, 3> factors{};
            for (uint8_t layer = 0; layer < 3; ++layer) {
                factors[layer] = static_cast<float>(topology->shoreProfile.Evaluate(node.x, node.z, layer));
            }
            topology->shoreNodeFactors.emplace(key, factors);
        }
    }
}

float GetNodeShoreFactor(const Topology& topology, int32_t u, int32_t v, const float meanWeights[3]) {
    const auto found = topology.shoreNodeFactors.find(PackLatticeCell(u, v));
    if (found == topology.shoreNodeFactors.end()) return 0.0f;
    float factor = 0.0f;
    for (uint8_t layer = 0; layer < 3; ++layer) {
        factor += meanWeights[layer] * found->second[layer];
    }
    return std::clamp(factor, 0.0f, 1.0f);
}

void RecipeCellNodes(const VertexRecipe& recipe, int32_t nodeU[3], int32_t nodeV[3]) {
    if (recipe.cellTriangle == 0) {
        nodeU[0] = recipe.cellU; nodeV[0] = recipe.cellV;
        nodeU[1] = recipe.cellU + 1; nodeV[1] = recipe.cellV;
        nodeU[2] = recipe.cellU + 1; nodeV[2] = recipe.cellV + 1;
    } else {
        nodeU[0] = recipe.cellU; nodeV[0] = recipe.cellV;
        nodeU[1] = recipe.cellU; nodeV[1] = recipe.cellV + 1;
        nodeU[2] = recipe.cellU + 1; nodeV[2] = recipe.cellV + 1;
    }
}

void PopulateRecipeStencilFactors(VertexRecipe* recipe, const Topology& topology) {
    int32_t nodeU[3];
    int32_t nodeV[3];
    RecipeCellNodes(*recipe, nodeU, nodeV);
    for (size_t i = 0; i < 3; ++i) {
        recipe->stencilFactors[i][0] = GetNodeShoreFactor(topology, nodeU[i] - 1, nodeV[i] - 1,
                                                          recipe->meanWeights);
        recipe->stencilFactors[i][1] = GetNodeShoreFactor(topology, nodeU[i], nodeV[i] + 1,
                                                          recipe->meanWeights);
        recipe->stencilFactors[i][2] = GetNodeShoreFactor(topology, nodeU[i] + 1, nodeV[i],
                                                          recipe->meanWeights);
    }
}

bool FinalizeTopology(Topology* topology) {
    topology->shoreNodeFactors.clear();
    topology->cellTriangleLookup.clear();
    for (size_t triangleIndex = 0; triangleIndex < topology->triangles.size(); ++triangleIndex) {
        const CachedTriangle& triangle = topology->triangles[triangleIndex];
        const VertexRecipe& first = triangle.vertices[0];
        CacheShoreNodeFactors(topology, first.cellU, first.cellV);
        if (first.layer != LAKE_WATER_LAYER_FALL) {
            topology->cellTriangleLookup[PackLatticeCell(first.cellU, first.cellV)].push_back(triangleIndex);
        }
    }

    std::unordered_map<RecipeKey, uint32_t, RecipeKeyHash> recipeIds;
    recipeIds.reserve(topology->triangles.size() * 3);
    topology->uniqueRecipes.clear();
    topology->indexedTriangles.clear();
    topology->uniqueRecipes.reserve(topology->triangles.size() * 3);
    topology->indexedTriangles.reserve(topology->triangles.size());

    for (CachedTriangle& triangle : topology->triangles) {
        LakeWater::Mesh::GlobalTriangle indices{};
        for (size_t corner = 0; corner < 3; ++corner) {
            VertexRecipe& recipe = triangle.vertices[corner];
            PopulateRecipeStencilFactors(&recipe, *topology);
            const RecipeKey key = MakeRecipeKey(recipe);
            auto found = recipeIds.find(key);
            if (found == recipeIds.end()) {
                if (topology->uniqueRecipes.size() >= std::numeric_limits<uint32_t>::max()) return false;
                const uint32_t id = static_cast<uint32_t>(topology->uniqueRecipes.size());
                topology->uniqueRecipes.push_back(recipe);
                found = recipeIds.emplace(key, id).first;
            }
            indices[corner] = found->second;
        }
        topology->indexedTriangles.push_back(indices);
    }

    if (!LakeWater::Mesh::BuildGreedyCachePackets(topology->indexedTriangles, topology->uniqueRecipes.size(),
                                                   topology->packets)) return false;
    topology->packetVertexOffsets.clear();
    topology->packetVertexOffsets.reserve(topology->packets.size());
    topology->packetVertexCount = 0;
    topology->commandBatches.clear();
    size_t batchFirstPacket = 0;
    size_t batchPacketCount = 0;
    size_t batchCommands = 0;
    for (size_t i = 0; i < topology->packets.size(); ++i) {
        const auto& packet = topology->packets[i];
        topology->packetVertexOffsets.push_back(topology->packetVertexCount);
        topology->packetVertexCount += packet.vertexIDs.size();
        // One vertex load, TRI2 for pairs, TRI1 for an odd tail, and one end.
        const size_t packetCommands = 1 + packet.triangles.size() / 2 + packet.triangles.size() % 2;
        if (batchPacketCount != 0 && batchCommands + packetCommands + 1 > kMaxCommandsPerBatch) {
            topology->commandBatches.push_back({batchFirstPacket, batchPacketCount, batchCommands + 1});
            batchFirstPacket = i;
            batchPacketCount = 0;
            batchCommands = 0;
        }
        batchPacketCount++;
        batchCommands += packetCommands;
    }
    if (batchPacketCount != 0) {
        topology->commandBatches.push_back({batchFirstPacket, batchPacketCount, batchCommands + 1});
    }
    topology->valid = !topology->indexedTriangles.empty() && !topology->packets.empty();
    return topology->valid;
}

void AppendCachedTriangle(const std::vector<MeshPoint>& polygon, uint8_t layer, Topology* topology) {
    const auto makeRecipe = [layer, topology](const MeshPoint& point) {
        float meanWeights[3] = {0.0f, 0.0f, 0.0f};
        meanWeights[layer] = 1.0f;
        const float shoreFactor = ShoreFactorForWeights(topology->shoreProfile, point.x, point.z, meanWeights);
        return VertexRecipe{point.x, point.z, point.cellU, point.cellV, point.cellTriangle, layer, false,
                            {point.bary[0], point.bary[1], point.bary[2]},
                            {meanWeights[0], meanWeights[1], meanWeights[2]}, shoreFactor};
    };
    for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        if (std::fabs(Cross(polygon[0], polygon[i], polygon[i + 1])) < 0.01f) continue;
        if (topology->triangles.size() >= kMaxTriangles) throw std::length_error("cartridge water topology limit");
        topology->triangles.push_back({{makeRecipe(polygon[0]), makeRecipe(polygon[i]), makeRecipe(polygon[i + 1])}, layer});
    }
}
void ClipAgainstEdge(std::vector<MeshPoint>* polygon, const MeshPoint& a, const MeshPoint& b, float sign) {
    std::vector<MeshPoint> result;
    if (polygon->empty()) return;
    MeshPoint previous = polygon->back();
    float previousSide = sign * Cross(a, b, previous);
    for (const MeshPoint& current : *polygon) {
        const float currentSide = sign * Cross(a, b, current);
        const bool previousInside = previousSide >= -0.0001f;
        const bool currentInside = currentSide >= -0.0001f;
        if (previousInside != currentInside) {
            const float denominator = previousSide - currentSide;
            if (std::fabs(denominator) > 1.0e-8f) {
                result.push_back(Mix(previous, current, previousSide / denominator));
            }
        }
        if (currentInside) result.push_back(current);
        previous = current;
        previousSide = currentSide;
    }
    *polygon = std::move(result);
}
bool ClipCellTriangle(const MeshPoint& a, const MeshPoint& b, const MeshPoint& c, const LakeWaterTriangle& wet,
                      Topology* topology) {
    const MeshPoint wa{wet.points[0].x, wet.points[0].z, 0, 0, 0, {0, 0, 0}};
    const MeshPoint wb{wet.points[1].x, wet.points[1].z, 0, 0, 0, {0, 0, 0}};
    const MeshPoint wc{wet.points[2].x, wet.points[2].z, 0, 0, 0, {0, 0, 0}};
    const float sign = Cross(wa, wb, wc) >= 0.0f ? 1.0f : -1.0f;
    std::vector<MeshPoint> polygon{a, b, c};
    ClipAgainstEdge(&polygon, wa, wb, sign);
    ClipAgainstEdge(&polygon, wb, wc, sign);
    ClipAgainstEdge(&polygon, wc, wa, sign);
    if (polygon.size() >= 3) AppendCachedTriangle(polygon, wet.layer, topology);
    return true;
}

struct FallPoint {
    float x, y, z, t;
    float bary[3];
    float meanWeights[3];
};
FallPoint MixFallPoint(const FallPoint& a, const FallPoint& b, float t) {
    FallPoint out{};
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    out.t = a.t + (b.t - a.t) * t;
    for (unsigned i = 0; i < 3; ++i) {
        out.bary[i] = a.bary[i] + (b.bary[i] - a.bary[i]) * t;
        out.meanWeights[i] = a.meanWeights[i] + (b.meanWeights[i] - a.meanWeights[i]) * t;
    }
    return out;
}
void ClipFallAtT(std::vector<FallPoint>* polygon, float boundary, bool keepGreater) {
    std::vector<FallPoint> result;
    if (polygon->empty()) return;
    FallPoint previous = polygon->back();
    float previousSide = keepGreater ? previous.t - boundary : boundary - previous.t;
    for (const FallPoint& current : *polygon) {
        const float currentSide = keepGreater ? current.t - boundary : boundary - current.t;
        const bool previousInside = previousSide >= -1.0e-6f;
        const bool currentInside = currentSide >= -1.0e-6f;
        if (previousInside != currentInside) {
            const float denominator = previousSide - currentSide;
            if (std::fabs(denominator) > 1.0e-8f) {
                result.push_back(MixFallPoint(previous, current, previousSide / denominator));
            }
        }
        if (currentInside) result.push_back(current);
        previous = current;
        previousSide = currentSide;
    }
    *polygon = std::move(result);
}

VertexRecipe MakeFallRecipe(const FallPoint& point, const LakeWater::ShoreProfile& shoreProfile) {
    constexpr float kInvSqrt3 = 0.5773502691896258f;
    constexpr float kTwoInvSqrt3 = 1.1547005383792517f;
    const float u = (point.x + point.z * kInvSqrt3) / 64.0f;
    const float v = (point.z * kTwoInvSqrt3) / 64.0f;
    const int32_t cellU = static_cast<int32_t>(std::floor(u));
    const int32_t cellV = static_cast<int32_t>(std::floor(v));
    const float fu = u - cellU, fv = v - cellV;
    const bool firstTriangle = fv <= fu;
    float b0, b1, b2;
    if (firstTriangle) {
        b0 = 1.0f - fu; b1 = fu - fv; b2 = fv;
    } else {
        b0 = 1.0f - fv; b1 = fv - fu; b2 = fu;
    }
    const float shoreFactor = ShoreFactorForWeights(shoreProfile, point.x, point.z, point.meanWeights);
    return {point.x, point.z, cellU, cellV, static_cast<uint8_t>(firstTriangle ? 0 : 1),
            LAKE_WATER_LAYER_FALL, true, {b0, b1, b2},
            {point.meanWeights[0], point.meanWeights[1], point.meanWeights[2]}, shoreFactor};
}

bool BuildFallTriangle(const LakeWaterTriangle& wet, bool lowWater, Topology* topology) {
    const float mainLocalY = lowWater ? -680.0f : 0.0f;
    const float lowerLocalY = lowWater ? 120.0f : 200.0f;
    const auto classifyLayer = [=](float y, uint8_t* layer) {
        if (std::fabs(y - mainLocalY) < 0.5f) *layer = LAKE_WATER_LAYER_MAIN;
        else if (std::fabs(y - lowerLocalY) < 0.5f) *layer = LAKE_WATER_LAYER_LOWER_RIVER;
        else if (std::fabs(y - 440.0f) < 0.5f) *layer = LAKE_WATER_LAYER_UPPER_RIVER;
        else return false;
        return true;
    };
    std::array<FallPoint, 3> source{};
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t layer;
        const auto& point = wet.points[i];
        if (!classifyLayer(point.y, &layer)) return false;
        source[i].x = point.x; source[i].y = point.y; source[i].z = point.z;
        source[i].bary[i] = 1.0f;
        source[i].meanWeights[layer] = 1.0f;
    }
    const auto& a = wet.points[0];
    const auto& b = wet.points[1];
    const auto& c = wet.points[2];
    const float abx=b.x-a.x, aby=b.y-a.y, abz=b.z-a.z;
    const float acx=c.x-a.x, acy=c.y-a.y, acz=c.z-a.z;
    const float area3d = (aby*acz-abz*acy)*(aby*acz-abz*acy) +
                         (abz*acx-abx*acz)*(abz*acx-abx*acz) +
                         (abx*acy-aby*acx)*(abx*acy-aby*acx);
    if (area3d < 0.01f) return true;

    // A vertical face has zero area in XZ but nonzero rendered area. Project it
    // onto its longest XZ axis and split every time that line crosses a WaveField
    // cell edge (u, v, or u-v integer), so its endpoint samples stay continuous.
    unsigned axisA = 0, axisB = 1;
    float axisLengthSquared = -1.0f;
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = i + 1; j < 3; ++j) {
        const float dx = source[j].x - source[i].x, dz = source[j].z - source[i].z;
        const float lengthSquared = dx * dx + dz * dz;
        if (lengthSquared > axisLengthSquared) { axisLengthSquared = lengthSquared; axisA = i; axisB = j; }
    }
    if (axisLengthSquared < 1.0e-6f) return true;
    const float originX = source[axisA].x, originZ = source[axisA].z;
    const float axisX = source[axisB].x - originX, axisZ = source[axisB].z - originZ;
    for (FallPoint& point : source) {
        point.t = ((point.x - originX) * axisX + (point.z - originZ) * axisZ) / axisLengthSquared;
        const float projectedX = originX + point.t * axisX, projectedZ = originZ + point.t * axisZ;
        if (std::hypot(point.x - projectedX, point.z - projectedZ) > 0.05f) return false;
    }
    const float minT = std::min({source[0].t, source[1].t, source[2].t});
    const float maxT = std::max({source[0].t, source[1].t, source[2].t});
    std::vector<float> cuts{minT, maxT};
    const auto appendCrossings = [&](float start, float delta) {
        if (std::fabs(delta) < 1.0e-8f) return;
        const float a = start + minT * delta, b = start + maxT * delta;
        for (int32_t boundary = static_cast<int32_t>(std::floor(std::min(a, b))) + 1;
             boundary <= static_cast<int32_t>(std::floor(std::max(a, b))); ++boundary) {
            const float t = (boundary - start) / delta;
            if (t > minT + 1.0e-6f && t < maxT - 1.0e-6f) cuts.push_back(t);
        }
    };
    constexpr float kInvSqrt3 = 0.5773502691896258f;
    appendCrossings((originX + originZ * kInvSqrt3) / 64.0f,
                    (axisX + axisZ * kInvSqrt3) / 64.0f);
    appendCrossings((originZ * 1.1547005383792517f) / 64.0f,
                    (axisZ * 1.1547005383792517f) / 64.0f);
    appendCrossings(((originX - originZ * kInvSqrt3) / 64.0f),
                    ((axisX - axisZ * kInvSqrt3) / 64.0f));
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](float a, float b) { return std::fabs(a - b) < 1.0e-6f; }),
               cuts.end());
    for (size_t segment = 0; segment + 1 < cuts.size(); ++segment) {
        std::vector<FallPoint> polygon(source.begin(), source.end());
        ClipFallAtT(&polygon, cuts[segment], true);
        ClipFallAtT(&polygon, cuts[segment + 1], false);
        if (polygon.size() < 3) continue;
        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            if (topology->triangles.size() >= kMaxTriangles) throw std::length_error("cartridge water topology limit");
            topology->triangles.push_back({{MakeFallRecipe(polygon[0], topology->shoreProfile),
                                            MakeFallRecipe(polygon[i], topology->shoreProfile),
                                            MakeFallRecipe(polygon[i + 1], topology->shoreProfile)},
                                           LAKE_WATER_LAYER_FALL});
        }
    }
    return true;
}
bool BuildTopology(s32 lowWater, float actorX, float actorZ, Topology* topology) {
    constexpr float kInvSqrt3 = 0.5773502691896258f;
    constexpr float kTwoInvSqrt3 = 1.1547005383792517f;
    const LakeWaterTriangle* wet = nullptr;
    u32 wetCount = 0;
    if (!LakeWater_GetFootprint(lowWater, actorX, actorZ, &wet, &wetCount) || wet == nullptr ||
        wetCount != (lowWater ? 12u : 37u)) return false;
    topology->triangles.clear();
    topology->shoreNodeFactors.clear();
    topology->cellTriangleLookup.clear();
    topology->triangles.reserve(lowWater ? 12000 : 40000);
    topology->valid = false;
    topology->actorX = actorX;
    topology->actorZ = actorZ;
    std::vector<LakeWater::WetTriangle> wetTriangles;
    std::vector<LakeWater::ShoreSegment> flowEdges;
    wetTriangles.reserve(wetCount);
    flowEdges.reserve(wetCount * 3);
    for (u32 t = 0; t < wetCount; ++t) {
        const LakeWaterTriangle& source = wet[t];
        for (unsigned p = 0; p < 3; ++p) {
            if (!std::isfinite(source.points[p].x) || !std::isfinite(source.points[p].z) ||
                !std::isfinite(source.points[p].y) || std::fabs(source.points[p].x) > 30000.0f ||
                std::fabs(source.points[p].y) > 30000.0f || std::fabs(source.points[p].z) > 30000.0f) return false;
        }
        if (source.layer == LAKE_WATER_LAYER_FALL) {
            for (unsigned a = 0; a < 3; ++a) {
                const unsigned b = (a + 1) % 3;
                const LakeWater::PointXZ pointA{static_cast<int64_t>(std::llround(source.points[a].x)),
                                                static_cast<int64_t>(std::llround(source.points[a].z))};
                const LakeWater::PointXZ pointB{static_cast<int64_t>(std::llround(source.points[b].x)),
                                                static_cast<int64_t>(std::llround(source.points[b].z))};
                if (pointA.x != pointB.x || pointA.z != pointB.z) {
                    flowEdges.push_back({pointA, pointB});
                }
            }
            continue;
        }
        if (source.layer > LAKE_WATER_LAYER_UPPER_RIVER) return false;
        const float area = (source.points[1].x - source.points[0].x) * (source.points[2].z - source.points[0].z) -
                           (source.points[1].z - source.points[0].z) * (source.points[2].x - source.points[0].x);
        if (std::fabs(area) < 0.01f) continue;
        LakeWater::WetTriangle converted{};
        converted.layer = source.layer;
        for (unsigned p = 0; p < 3; ++p) {
            converted.points[p] = {static_cast<int64_t>(std::llround(source.points[p].x)),
                                   static_cast<int64_t>(std::llround(source.points[p].z))};
        }
        wetTriangles.push_back(converted);
    }
    if (!topology->shoreProfile.Initialize(wetTriangles, flowEdges)) return false;
    for (u32 t = 0; t < wetCount; ++t) {
        for (unsigned p = 0; p < 3; ++p) {
            if (!std::isfinite(wet[t].points[p].x) || !std::isfinite(wet[t].points[p].z) ||
                !std::isfinite(wet[t].points[p].y) || std::fabs(wet[t].points[p].x) > 30000.0f ||
                std::fabs(wet[t].points[p].y) > 30000.0f || std::fabs(wet[t].points[p].z) > 30000.0f) return false;
        }
        if (wet[t].layer == LAKE_WATER_LAYER_FALL) {
            if (!BuildFallTriangle(wet[t], lowWater != 0, topology)) return false;
            continue;
        }
        if (wet[t].layer > LAKE_WATER_LAYER_UPPER_RIVER) return false;
        const float area = (wet[t].points[1].x - wet[t].points[0].x) *
                               (wet[t].points[2].z - wet[t].points[0].z) -
                           (wet[t].points[1].z - wet[t].points[0].z) *
                               (wet[t].points[2].x - wet[t].points[0].x);
        // The exact Zelda resource streams include intentional degenerate
        // source triangles. They are part of the validated 37/12 census but
        // contribute no area to the clipped lattice mesh.
        if (std::fabs(area) < 0.01f) continue;
        float uMin = INFINITY, uMax = -INFINITY, vMin = INFINITY, vMax = -INFINITY;
        for (const auto& p : wet[t].points) {
            const float u = (p.x + p.z * kInvSqrt3) / 64.0f;
            const float v = (p.z * kTwoInvSqrt3) / 64.0f;
            uMin = std::min(uMin, u); uMax = std::max(uMax, u);
            vMin = std::min(vMin, v); vMax = std::max(vMax, v);
        }
        const int32_t u0 = static_cast<int32_t>(std::floor(uMin));
        const int32_t u1 = static_cast<int32_t>(std::ceil(uMax));
        const int32_t v0 = static_cast<int32_t>(std::floor(vMin));
        const int32_t v1 = static_cast<int32_t>(std::ceil(vMax));
        const int64_t cellsToScan = int64_t(u1 - u0) * int64_t(v1 - v0);
        if (cellsToScan < 0 || cellsToScan > 250000) return false;
        for (int32_t v = v0; v < v1; ++v) for (int32_t u = u0; u < u1; ++u) {
            const auto p00a = LatticePosition(u, v, u, v, 0, 1, 0, 0);
            const auto p10 = LatticePosition(u + 1, v, u, v, 0, 0, 1, 0);
            const auto p11a = LatticePosition(u + 1, v + 1, u, v, 0, 0, 0, 1);
            const auto p00b = LatticePosition(u, v, u, v, 1, 1, 0, 0);
            const auto p11b = LatticePosition(u + 1, v + 1, u, v, 1, 0, 0, 1);
            const auto p01b = LatticePosition(u, v + 1, u, v, 1, 0, 1, 0);
            // Exact split used by WaveField::Sample: (00,10,11), (00,01,11).
            if (!ClipCellTriangle(p00a, p10, p11a, wet[t], topology) ||
                !ClipCellTriangle(p00b, p01b, p11b, wet[t], topology)) return false;
        }
    }
    return FinalizeTopology(topology);
}

struct SurfaceAttributes { float y, s, t; };
int16_t AttenuateHeight(int16_t heightFixed, float factor) {
    if (factor >= 1.0f) return heightFixed;
    if (factor <= 0.0f) return 0;
    const long scaled = std::lround(static_cast<double>(heightFixed) * factor);
    return static_cast<int16_t>(std::clamp(scaled, long(INT16_MIN), long(INT16_MAX)));
}

SurfaceAttributes ReadSurfaceAttributes(const LakeWater::WaveField& field, const VertexRecipe& recipe,
                                        size_t sourceNode, int32_t u, int32_t v, float mean) {
    const int16_t h = field.ReadCell(u, v).heightFixed;
    const LakeWaterCartridgeUV uv = LakeWater_ComputeCartridgeUV(
        AttenuateHeight(field.ReadCell(u - 1, v - 1).heightFixed,
                        recipe.stencilFactors[sourceNode][0]),
        AttenuateHeight(field.ReadCell(u, v + 1).heightFixed,
                        recipe.stencilFactors[sourceNode][1]),
        AttenuateHeight(field.ReadCell(u + 1, v).heightFixed,
                        recipe.stencilFactors[sourceNode][2]));
    return {float(LakeWater_ComputeCartridgeVertexY(h, int32_t(mean))), float(uv.s), float(uv.t)};
}
bool EvaluateRecipe(const LakeWater::WaveField& field, const VertexRecipe& recipe,
                    const LakeWaterCartridgeAppearance& appearance, Vtx* outVertex) {
    if (recipe.fall) {
        std::array<SurfaceAttributes, 3> src;
        if (recipe.cellTriangle == 0) {
            src = {ReadSurfaceAttributes(field, recipe, 0, recipe.cellU, recipe.cellV, 0.0f),
                   ReadSurfaceAttributes(field, recipe, 1, recipe.cellU + 1, recipe.cellV, 0.0f),
                   ReadSurfaceAttributes(field, recipe, 2, recipe.cellU + 1, recipe.cellV + 1, 0.0f)};
        } else {
            src = {ReadSurfaceAttributes(field, recipe, 0, recipe.cellU, recipe.cellV, 0.0f),
                   ReadSurfaceAttributes(field, recipe, 1, recipe.cellU, recipe.cellV + 1, 0.0f),
                   ReadSurfaceAttributes(field, recipe, 2, recipe.cellU + 1, recipe.cellV + 1, 0.0f)};
        }
        const float s = src[0].s * recipe.bary[0] + src[1].s * recipe.bary[1] + src[2].s * recipe.bary[2];
        const float t = src[0].t * recipe.bary[0] + src[1].t * recipe.bary[1] + src[2].t * recipe.bary[2];
        const auto wave = field.Sample(recipe.x, recipe.z, 0.0f);
        if (!wave.valid) return false;
        const float base = recipe.meanWeights[0] * appearance.waterLevels[0] +
                           recipe.meanWeights[1] * appearance.waterLevels[1] +
                           recipe.meanWeights[2] * appearance.waterLevels[2];
        Vtx out{};
        out.v.ob[0] = static_cast<s16>(std::lround(recipe.x));
        out.v.ob[1] = static_cast<s16>(std::lround(base + wave.height * recipe.shoreFactor));
        out.v.ob[2] = static_cast<s16>(std::lround(recipe.z));
        // The clipped bank itself is flat even when its cell straddles a wave.
        // Return to the cartridge's neutral slope-map center at that edge.
        out.v.tc[0] = static_cast<s16>(std::lround(1024.0f + (s - 1024.0f) * recipe.shoreFactor));
        out.v.tc[1] = static_cast<s16>(std::lround(1024.0f + (t - 1024.0f) * recipe.shoreFactor));
        out.v.cn[0] = out.v.cn[1] = out.v.cn[2] = out.v.cn[3] = 255;
        *outVertex = out;
        return true;
    }
    std::array<SurfaceAttributes, 3> src;
    const int32_t u = recipe.cellU, v = recipe.cellV;
    const float mean = appearance.waterLevels[recipe.layer];
    if (recipe.cellTriangle == 0) {
        src = {ReadSurfaceAttributes(field, recipe, 0, u, v, mean),
               ReadSurfaceAttributes(field, recipe, 1, u + 1, v, mean),
               ReadSurfaceAttributes(field, recipe, 2, u + 1, v + 1, mean)};
    } else {
        src = {ReadSurfaceAttributes(field, recipe, 0, u, v, mean),
               ReadSurfaceAttributes(field, recipe, 1, u, v + 1, mean),
               ReadSurfaceAttributes(field, recipe, 2, u + 1, v + 1, mean)};
    }
    const float unattenuatedY = src[0].y * recipe.bary[0] + src[1].y * recipe.bary[1] +
                                src[2].y * recipe.bary[2];
    const float y = mean + (unattenuatedY - mean) * recipe.shoreFactor;
    const float s = src[0].s*recipe.bary[0] + src[1].s*recipe.bary[1] + src[2].s*recipe.bary[2];
    const float t = src[0].t*recipe.bary[0] + src[1].t*recipe.bary[1] + src[2].t*recipe.bary[2];
    Vtx out{};
    out.v.ob[0] = static_cast<s16>(std::lround(recipe.x));
    out.v.ob[1] = static_cast<s16>(std::lround(y));
    out.v.ob[2] = static_cast<s16>(std::lround(recipe.z));
    out.v.flag = 0;
    out.v.tc[0] = static_cast<s16>(std::lround(1024.0f + (s - 1024.0f) * recipe.shoreFactor));
    out.v.tc[1] = static_cast<s16>(std::lround(1024.0f + (t - 1024.0f) * recipe.shoreFactor));
    out.v.cn[0] = out.v.cn[1] = out.v.cn[2] = out.v.cn[3] = 255;
    *outVertex = out;
    return true;
}

bool ToOriginalLatticeCell(float x, float z, int32_t* cellU, int32_t* cellV, float* fracU, float* fracV) {
    constexpr float kOneOverSqrt3 = 0.5773502691896258f;
    constexpr float kTwoOverSqrt3 = 1.1547005383792517f;
    const float transformedU = x + z * kOneOverSqrt3;
    const float transformedV = z * kTwoOverSqrt3;
    if (!std::isfinite(transformedU) || !std::isfinite(transformedV) ||
        transformedU > 2147483000.0f || transformedU < -2147483000.0f ||
        transformedV > 2147483000.0f || transformedV < -2147483000.0f) return false;
    const int32_t wrappedU = static_cast<int32_t>(transformedU) % 24576;
    const int32_t wrappedV = static_cast<int32_t>(transformedV) % 24576;
    const auto shiftFloor = [](int32_t value) {
        return value >= 0 ? value >> 6 : -static_cast<int32_t>((-int64_t(value) + 63) >> 6);
    };
    *cellU = shiftFloor(wrappedU);
    *cellV = shiftFloor(wrappedV);
    *fracU = static_cast<float>(wrappedU - *cellU * 64) / 64.0f;
    *fracV = static_cast<float>(wrappedV - *cellV * 64) / 64.0f;
    return true;
}

bool SampleFromCell(const LakeWater::WaveField& field, const LakeWaterCartridgeAppearance& appearance,
                    const Topology& topology, const std::vector<Vtx>* preEvaluatedVertices, int32_t cellU,
                    int32_t cellV, float x, float z, uint8_t layer, float* ySurface) {
    const auto found = topology.cellTriangleLookup.find(PackLatticeCell(cellU, cellV));
    if (found == topology.cellTriangleLookup.end()) return false;
    for (size_t triangleIndex : found->second) {
        const CachedTriangle& triangle = topology.triangles[triangleIndex];
        if (triangle.layer != layer) continue;
        std::array<Vtx, 3> vertices{};
        bool evaluated = true;
        for (size_t corner = 0; corner < 3; ++corner) {
            if (preEvaluatedVertices != nullptr) {
                const uint32_t vertexID = topology.indexedTriangles[triangleIndex][corner];
                if (vertexID >= preEvaluatedVertices->size()) return false;
                vertices[corner] = (*preEvaluatedVertices)[vertexID];
            } else if (!EvaluateRecipe(field, triangle.vertices[corner], appearance, &vertices[corner])) {
                evaluated = false;
                break;
            }
        }
        if (!evaluated) continue;
        const double ax = vertices[0].v.ob[0], az = vertices[0].v.ob[2];
        const double bx = vertices[1].v.ob[0], bz = vertices[1].v.ob[2];
        const double cx = vertices[2].v.ob[0], cz = vertices[2].v.ob[2];
        const double area = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
        if (std::fabs(area) < 1.0e-9) continue;
        const double weightA = ((bx - x) * (cz - z) - (bz - z) * (cx - x)) / area;
        const double weightB = ((cx - x) * (az - z) - (cz - z) * (ax - x)) / area;
        const double weightC = 1.0 - weightA - weightB;
        constexpr double kBarycentricTolerance = 1.0e-5;
        if (weightA < -kBarycentricTolerance || weightB < -kBarycentricTolerance ||
            weightC < -kBarycentricTolerance || weightA > 1.0 + kBarycentricTolerance ||
            weightB > 1.0 + kBarycentricTolerance || weightC > 1.0 + kBarycentricTolerance) continue;
        *ySurface = static_cast<float>(weightA * vertices[0].v.ob[1] + weightB * vertices[1].v.ob[1] +
                                       weightC * vertices[2].v.ob[1]);
        return std::isfinite(*ySurface);
    }
    return false;
}

bool SampleSurfaceFromTopology(const LakeWater::WaveField& field, const LakeWaterCartridgeAppearance& appearance,
                               const Topology& topology, const std::vector<Vtx>* preEvaluatedVertices, float x,
                               float z, uint8_t layer, float* ySurface) {
    int32_t cellU, cellV;
    float fracU, fracV;
    if (!ToOriginalLatticeCell(x, z, &cellU, &cellV, &fracU, &fracV)) return false;
    if (SampleFromCell(field, appearance, topology, preEvaluatedVertices, cellU, cellV,
                       x, z, layer, ySurface)) return true;
    // Float coordinate conversion at an exact lattice edge can select either
    // neighboring cell. Search the immediate ring without scanning the mesh.
    for (int32_t dv = -1; dv <= 1; ++dv) {
        for (int32_t du = -1; du <= 1; ++du) {
            if (du == 0 && dv == 0) continue;
            if (SampleFromCell(field, appearance, topology, preEvaluatedVertices, cellU + du, cellV + dv,
                               x, z, layer, ySurface)) return true;
        }
    }
    return false;
}

bool ValidateRenderedSurface(const LakeWater::WaveField& field, const LakeWaterCartridgeAppearance& appearance,
                              const Topology& topology, const std::vector<Vtx>& vertices,
                              size_t* checked, float* maxError) {
    *checked = 0;
    *maxError = 0.0f;
    for (size_t index = 0; index < topology.triangles.size(); ++index) {
        const auto& triangle = topology.triangles[index];
        if (triangle.layer == LAKE_WATER_LAYER_FALL) continue;
        const auto& ids = topology.indexedTriangles[index];
        const Vtx& a = vertices[ids[0]];
        const Vtx& b = vertices[ids[1]];
        const Vtx& c = vertices[ids[2]];
        const double area = double(b.v.ob[0] - a.v.ob[0]) * (c.v.ob[2] - a.v.ob[2]) -
                            double(b.v.ob[2] - a.v.ob[2]) * (c.v.ob[0] - a.v.ob[0]);
        if (area == 0.0) continue;
        const float x = (float(a.v.ob[0]) + b.v.ob[0] + c.v.ob[0]) / 3.0f;
        const float z = (float(a.v.ob[2]) + b.v.ob[2] + c.v.ob[2]) / 3.0f;
        const float expected = (float(a.v.ob[1]) + b.v.ob[1] + c.v.ob[1]) / 3.0f;
        float actual;
        if (!SampleSurfaceFromTopology(field, appearance, topology, nullptr, x, z, triangle.layer, &actual)) {
            spdlog::error("Cartridge surface query missed rendered triangle {} at {},{}", index, x, z);
            return false;
        }
        const float error = std::fabs(actual - expected);
        *maxError = std::max(*maxError, error);
        ++*checked;
        // Rounding clipped XZ corners can produce tiny overlaps between adjacent
        // source triangles. Require contact agreement within one world unit.
        if (error > 1.0f) return false;
    }
    return *checked != 0;
}

bool ValidateDeduplicatedTopology(const LakeWater::WaveField& field, const LakeWaterCartridgeAppearance& appearance,
                                  const Topology& topology, const std::vector<Vtx>& uniqueVertices,
                                  size_t* mismatchTriangle, size_t* mismatchCorner) {
    if (topology.indexedTriangles.size() != topology.triangles.size()) return false;
    for (size_t triangle = 0; triangle < topology.triangles.size(); ++triangle) {
        for (size_t corner = 0; corner < 3; ++corner) {
            const uint32_t uniqueID = topology.indexedTriangles[triangle][corner];
            if (uniqueID >= uniqueVertices.size()) return false;
            Vtx original{};
            if (!EvaluateRecipe(field, topology.triangles[triangle].vertices[corner], appearance, &original)) {
                return false;
            }
            if (std::memcmp(&original, &uniqueVertices[uniqueID], sizeof(Vtx)) != 0) {
                if (mismatchTriangle != nullptr) *mismatchTriangle = triangle;
                if (mismatchCorner != nullptr) *mismatchCorner = corner;
                return false;
            }
        }
    }
    return true;
}

bool WaterTraceEnabled() {
    static const bool enabled = [] {
        const char* trace = std::getenv("SHIP_WR64_WATER_TRACE");
        return trace != nullptr && trace[0] != '\0' && trace[0] != '0';
    }();
    return enabled;
}
bool UpdateFrameVertices(const LakeWater::WaveField& field, const LakeWaterCartridgeAppearance& appearance,
                         const Topology& topology,
                         std::vector<Vtx>* vertices) {
    if (!topology.valid || topology.triangles.empty() || topology.triangles.size() > kMaxTriangles ||
        topology.uniqueRecipes.empty()) return false;
    vertices->resize(topology.uniqueRecipes.size());
    for (size_t i = 0; i < topology.uniqueRecipes.size(); ++i) {
        if (!EvaluateRecipe(field, topology.uniqueRecipes[i], appearance, &(*vertices)[i])) return false;
    }
    return true;
}

bool PackFrameVertices(const Topology& topology, const std::vector<Vtx>& vertices, std::vector<Vtx>* packedVertices) {
    if (topology.packetVertexOffsets.size() != topology.packets.size()) return false;
    packedVertices->resize(topology.packetVertexCount);
    for (size_t packetIndex = 0; packetIndex < topology.packets.size(); ++packetIndex) {
        const auto& packet = topology.packets[packetIndex];
        const size_t offset = topology.packetVertexOffsets[packetIndex];
        if (offset + packet.vertexIDs.size() > packedVertices->size() ||
            packet.vertexIDs.size() > LakeWater::Mesh::kVertexCacheSize) return false;
        for (size_t i = 0; i < packet.vertexIDs.size(); ++i) {
            const uint32_t vertexID = packet.vertexIDs[i];
            if (vertexID >= vertices.size()) return false;
            (*packedVertices)[offset + i] = vertices[vertexID];
        }
    }
    return true;
}

// Follow Zelda's two graphics pools. The native translator consumes host data
// during each render/interpolation submission; retain the previous game frame
// while the other pool is rebuilt. Large water meshes do not fit Graph_Alloc's
// roughly 190 KiB opaque arena.
struct FrameMesh {
    // Evaluated once per unique recipe, then packed in each packet's cache order.
    std::vector<Vtx> vertices;
    std::vector<Vtx> packetVertices;
    std::vector<std::vector<Gfx>> batches;
    std::vector<Vtx> bodyVertices;
    std::array<Gfx, 128> master{};
    Mtx identity{};
};
std::array<FrameMesh, 2> sFrameMeshes;

bool ReserveFrameBuffers() {
    const Topology& high = sTopology[0];
    const Topology& low = sTopology[1];
    const size_t maxUniqueVertices = std::max(high.uniqueRecipes.size(), low.uniqueRecipes.size());
    const size_t maxPacketVertices = std::max(high.packetVertexCount, low.packetVertexCount);
    const size_t maxBatchCount = std::max(high.commandBatches.size(), low.commandBatches.size());
    const size_t maxBatchCommands = std::max(
        high.commandBatches.empty() ? size_t(0) : std::max_element(high.commandBatches.begin(), high.commandBatches.end(),
            [](const CommandBatch& a, const CommandBatch& b) { return a.commandCount < b.commandCount; })->commandCount,
        low.commandBatches.empty() ? size_t(0) : std::max_element(low.commandBatches.begin(), low.commandBatches.end(),
            [](const CommandBatch& a, const CommandBatch& b) { return a.commandCount < b.commandCount; })->commandCount);
    if (maxUniqueVertices == 0 || maxPacketVertices == 0 || maxBatchCount == 0 ||
        maxBatchCommands > kMaxCommandsPerBatch) return false;
    for (FrameMesh& frame : sFrameMeshes) {
        frame.bodyVertices.reserve(37 * 3);
        frame.vertices.clear();
        frame.vertices.reserve(maxUniqueVertices);
        frame.packetVertices.clear();
        frame.packetVertices.reserve(maxPacketVertices);
        frame.batches.clear();
        frame.batches.resize(maxBatchCount);
        for (auto& batch : frame.batches) batch.reserve(maxBatchCommands);
    }
    return true;
}

} // namespace

extern "C" s32 LakeWaterCartridge_LoadSourceRecords(u8* records, u32 byteCount) {
    static constexpr uint8_t digest[32] = {
        0x49,0x83,0xad,0x6e,0x96,0x4a,0xdd,0x16,0x19,0x9e,0x4c,0xfb,0x63,0x2f,0x5b,0xc8,
        0x15,0x5f,0x5f,0x5e,0xff,0x5a,0xe2,0x41,0x5b,0xb9,0x79,0x69,0xd3,0x31,0x71,0xc5,
    };
    static std::vector<uint8_t> sourceRecords;
    static bool attempted = false;
    static bool valid = false;
    if (records == nullptr || byteCount != 3 * 64) return false;
    if (!attempted) {
        attempted = true;
        valid = ReadCheckedAsset("cartridge-water/sunny_beach_wave_sources.bin", byteCount, digest, sourceRecords);
    }
    sSourceReady = valid;
    if (!valid) return false;
    std::memcpy(records, sourceRecords.data(), byteCount);
    return true;
}

extern "C" s32 LakeWaterCartridge_IsReady(void) {
    return sPreflightReady && sTextureReady && sSourceReady && !sDedupOracleFailed &&
           sTopology[0].valid && sTopology[1].valid;
}

extern "C" s32 LakeWaterCartridge_Preflight(s32 lowWater, f32 actorX, f32 actorZ,
                                             const LakeWaterCartridgeAppearance* appearance) {
    if ((lowWater != 0 && lowWater != 1) || !std::isfinite(actorX) || !std::isfinite(actorZ) ||
        std::fabs(actorX) > 30000.0f || std::fabs(actorZ) > 30000.0f || appearance == nullptr ||
        appearance->fogFar <= appearance->fogNear || !std::isfinite(appearance->waterLevels[0]) ||
        !std::isfinite(appearance->waterLevels[1]) || !std::isfinite(appearance->waterLevels[2])) {
        sPreflightReady = false;
        return false;
    }
    const uint8_t* texture = nullptr;
    u8 sourceRecords[3 * 64];
    sTextureReady = LoadTexture(&texture);
    if (!sTextureReady || texture == nullptr || !LakeWaterCartridge_LoadSourceRecords(sourceRecords, sizeof(sourceRecords))) {
        sPreflightReady = false;
        return false;
    }
    const bool sameOrigin = sPreflightOriginKnown && actorX == sPreflightActorX && actorZ == sPreflightActorZ;
    if (sPreflightReady && sameOrigin) return true;

    sPreflightReady = false;
    try {
        if (!sameOrigin) {
            sDedupOracleChecked = false;
            sDedupOracleFailed = false;
            sTopology[0] = Topology{};
            sTopology[1] = Topology{};
            if (!BuildTopology(false, actorX, actorZ, &sTopology[0]) ||
                !BuildTopology(true, actorX, actorZ, &sTopology[1])) return false;
            sPreflightActorX = actorX;
            sPreflightActorZ = actorZ;
            sPreflightOriginKnown = true;
        }
        if (!ReserveFrameBuffers()) return false;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    sPreflightReady = true;
    static bool loggedTopology = false;
    const char* trace = std::getenv("SHIP_WR64_WATER_TRACE");
    if (!loggedTopology && trace != nullptr && trace[0] != '\0' && trace[0] != '0') {
        loggedTopology = true;
        const auto maxCommands = [](const Topology& topology) {
            size_t result = 0;
            for (const CommandBatch& batch : topology.commandBatches) {
                result = std::max(result, batch.commandCount);
            }
            return result;
        };
        const Topology& high = sTopology[0];
        const Topology& low = sTopology[1];
        spdlog::info("Cartridge water mesh: high triangles={} recipes={} vertexLoads={} loadedVertices={} childLists={} maxChildCommands={}; "
                     "low triangles={} recipes={} vertexLoads={} loadedVertices={} childLists={} maxChildCommands={}",
                     high.triangles.size(), high.uniqueRecipes.size(), high.packets.size(), high.packetVertexCount,
                     high.commandBatches.size(), maxCommands(high),
                     low.triangles.size(), low.uniqueRecipes.size(), low.packets.size(), low.packetVertexCount,
                     low.commandBatches.size(), maxCommands(low));
    }
    return true;
}

extern "C" s32 LakeWaterCartridge_SampleSurface(const void* publishedField, s32 lowWater,
                                                f32 actorX, f32 actorZ, f32 x, f32 z, u8 layer,
                                                f32 mean, f32* ySurface) {
    if (publishedField == nullptr || ySurface == nullptr || !LakeWaterCartridge_IsReady() ||
        (lowWater != 0 && lowWater != 1) || layer > LAKE_WATER_LAYER_UPPER_RIVER ||
        !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(mean) || std::fabs(mean) > 30000.0f) return false;
    const Topology& topology = sTopology[lowWater ? 1 : 0];
    if (actorX != topology.actorX || actorZ != topology.actorZ) return false;
    LakeWaterCartridgeAppearance appearance{};
    appearance.waterLevels[layer] = mean;
    return SampleSurfaceFromTopology(*static_cast<const LakeWater::WaveField*>(publishedField), appearance,
                                      topology, nullptr, x, z, layer, ySurface);
}

extern "C" s32 LakeWaterCartridge_Draw(PlayState* play, f32 meanLevel, s32 lowWater, f32 actorX, f32 actorZ,
                                         const void* publishedField,
                                         const LakeWaterCartridgeAppearance* appearance, Gfx** gfxCursor) {
    if (play == nullptr || play->state.gfxCtx == nullptr || publishedField == nullptr || appearance == nullptr ||
        gfxCursor == nullptr || *gfxCursor == nullptr || appearance->fogFar <= appearance->fogNear ||
        !std::isfinite(meanLevel) || std::fabs(meanLevel - appearance->waterLevels[LAKE_WATER_LAYER_MAIN]) > 0.5f ||
        !LakeWaterCartridge_Preflight(lowWater, actorX, actorZ, appearance) ||
        !LakeWaterCartridge_IsReady()) {
        return false;
    }
    const uint8_t* texture = nullptr;
    if (!LoadTexture(&texture)) return false;

    const auto& field = *static_cast<const LakeWater::WaveField*>(publishedField);
    FrameMesh& frame = sFrameMeshes[play->state.gfxCtx->gfxPoolIdx & 1u];
    const Topology& topology = sTopology[lowWater ? 1 : 0];
    if (!UpdateFrameVertices(field, *appearance, topology, &frame.vertices)) return false;
    if (WaterTraceEnabled() && !sDedupOracleChecked) {
        sDedupOracleChecked = true;
        size_t mismatchTriangle = 0;
        size_t mismatchCorner = 0;
        bool valid = ValidateDeduplicatedTopology(field, *appearance, topology, frame.vertices,
                                                   &mismatchTriangle, &mismatchCorner);
        const Topology& otherTopology = sTopology[lowWater ? 0 : 1];
        std::vector<Vtx> otherVertices;
        if (valid) {
            otherVertices.resize(otherTopology.uniqueRecipes.size());
            for (size_t i = 0; i < otherTopology.uniqueRecipes.size(); ++i) {
                if (!EvaluateRecipe(field, otherTopology.uniqueRecipes[i], *appearance, &otherVertices[i])) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                valid = ValidateDeduplicatedTopology(field, *appearance, otherTopology, otherVertices,
                                                      &mismatchTriangle, &mismatchCorner);
            }
        }
        size_t surfaceChecked = 0, otherSurfaceChecked = 0;
        float surfaceError = 0.0f, otherSurfaceError = 0.0f;
        if (valid) {
            valid = ValidateRenderedSurface(field, *appearance, topology, frame.vertices,
                                              &surfaceChecked, &surfaceError) &&
                    ValidateRenderedSurface(field, *appearance, otherTopology, otherVertices,
                                              &otherSurfaceChecked, &otherSurfaceError);
            spdlog::info("Cartridge surface query oracle: checked={}/{} maxError={}/{} valid={}",
                         surfaceChecked, otherSurfaceChecked, surfaceError, otherSurfaceError, valid);
        }
        if (!valid) {
            sDedupOracleFailed = true;
            spdlog::error("Cartridge water vertex dedup oracle failed: mode={} triangle={} corner={}",
                          lowWater ? "low" : "high", mismatchTriangle, mismatchCorner);
            return false;
        }
        spdlog::info("Cartridge water vertex dedup oracle passed for high/low topologies: recipes={}/{} triangles={}/{}",
                     sTopology[0].uniqueRecipes.size(), sTopology[1].uniqueRecipes.size(),
                     sTopology[0].triangles.size(), sTopology[1].triangles.size());
    }
    if (!PackFrameVertices(topology, frame.vertices, &frame.packetVertices)) return false;
    Vtx* frameVertices = frame.packetVertices.data();
    const size_t batchCount = topology.commandBatches.size();
    if (batchCount + 32 >= frame.master.size() || batchCount > frame.batches.size()) return false;
    Gfx* master = frame.master.data();
    // Shipwright's guMtxIdent stores floats, but the RT64 translator consumes
    // packed MtxS words. Use the same conversion as Zelda's actor matrices.
    float identityFloat[4][4];
    guMtxIdentF(identityFloat);
    guMtxF2L(identityFloat, &frame.identity);
    Gfx* body = master;
    // Vertices include the actor's XZ origin and each live water-box mean Y.
    // Use identity modelview to avoid applying the water actor transform twice.
    gSPMatrix(body++, &frame.identity, G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // Original Sunny Beach first draws D_106F930, the fixed cyan +Y=-100
    // plane, then overlays the alpha decal map at the live wave surface. Keep
    // this separate pass: its ROM render mode writes depth, while the
    // highlight material performs Z compare without depth updates.
    const LakeWaterTriangle* bodyFootprint = nullptr;
    u32 bodyTriangleCount = 0;
    if (!LakeWater_GetFootprint(lowWater, actorX, actorZ, &bodyFootprint, &bodyTriangleCount)) return false;
    frame.bodyVertices.clear();
    for (u32 triangle = 0; triangle < bodyTriangleCount; ++triangle) {
        const auto& source = bodyFootprint[triangle];
        if (source.layer > LAKE_WATER_LAYER_UPPER_RIVER) continue;
        const float area = (source.points[1].x - source.points[0].x) *
                               (source.points[2].z - source.points[0].z) -
                           (source.points[1].z - source.points[0].z) *
                               (source.points[2].x - source.points[0].x);
        if (std::fabs(area) < 0.01f) continue;
        // Adapt the course background to Zelda's wet footprint and three live
        // water levels. Preserve the ROM's negative XZ winding and -100 offset.
        for (unsigned corner = 0; corner < 3; ++corner) {
            const unsigned index = area < 0 ? corner : (corner == 0 ? 0 : 3 - corner);
            Vtx vertex{};
            vertex.v.ob[0] = static_cast<s16>(std::lround(source.points[index].x));
            vertex.v.ob[1] = static_cast<s16>(std::lround(appearance->waterLevels[source.layer] +
                                                       float(LAKE_WATER_WR64_BODY_Y)));
            vertex.v.ob[2] = static_cast<s16>(std::lround(source.points[index].z));
            vertex.v.cn[0] = LAKE_WATER_WR64_BODY_R;
            vertex.v.cn[1] = LAKE_WATER_WR64_BODY_G;
            vertex.v.cn[2] = LAKE_WATER_WR64_BODY_B;
            vertex.v.cn[3] = 255;
            frame.bodyVertices.push_back(vertex);
        }
    }
    body = LakeWater_EmitOriginalSunnyBeachBodyMaterial(body);
    for (size_t first = 0; first < frame.bodyVertices.size(); first += 30) {
        const size_t count = std::min(size_t(30), frame.bodyVertices.size() - first);
        __gSPVertex(body++, frame.bodyVertices.data() + first, count, 0);
        for (size_t corner = 0; corner < count; corner += 3) {
            gSP1Triangle(body++, corner, corner + 1, corner + 2, 0);
        }
    }
    body = LakeWater_EmitOriginalSunnyBeachMaterial(
        body, reinterpret_cast<const u32*>(texture), appearance->environmentGray, appearance->environmentAlpha);
    for (size_t batch = 0; batch < batchCount; ++batch) {
        const CommandBatch& commandBatch = topology.commandBatches[batch];
        frame.batches[batch].resize(commandBatch.commandCount);
        Gfx* batchCommands = frame.batches[batch].data();
        Gfx* batchGfx = batchCommands;
        const size_t packetEnd = commandBatch.firstPacket + commandBatch.packetCount;
        for (size_t packetIndex = commandBatch.firstPacket; packetIndex < packetEnd; ++packetIndex) {
            const auto& packet = topology.packets[packetIndex];
            const size_t vertexOffset = topology.packetVertexOffsets[packetIndex];
            gSPVertex(batchGfx++, reinterpret_cast<uintptr_t>(&frameVertices[vertexOffset]),
                      packet.vertexIDs.size(), 0);
            size_t triangle = 0;
            for (; triangle + 1 < packet.triangles.size(); triangle += 2) {
                const auto& first = packet.triangles[triangle];
                const auto& second = packet.triangles[triangle + 1];
                gSP2Triangles(batchGfx++, first[0], first[1], first[2], 0,
                              second[0], second[1], second[2], 0);
            }
            if (triangle < packet.triangles.size()) {
                const auto& last = packet.triangles[triangle];
                gSP1Triangle(batchGfx++, last[0], last[1], last[2], 0);
            }
        }
        gSPEndDisplayList(batchGfx++);
        if (static_cast<size_t>(batchGfx - batchCommands) != commandBatch.commandCount) return false;
        gSPDisplayList(body++, batchCommands);
    }
    gSPEndDisplayList(body++);
    gSPDisplayList((*gfxCursor)++, master);
    gSPPopMatrix((*gfxCursor)++, G_MTX_MODELVIEW);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    return true;
}
