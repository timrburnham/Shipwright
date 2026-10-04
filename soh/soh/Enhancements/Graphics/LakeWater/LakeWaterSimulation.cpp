#include "soh/Enhancements/Graphics/LakeWater/LakeWaterSimulation.h"

#include "soh/Enhancements/Graphics/LakeWater/WaveField.h"
#include "soh/Enhancements/Graphics/LakeWater/LakeWater.h"
#include "soh/Enhancements/Graphics/LakeWater/LakeDomain.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaveSource.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterRenderer.h"
#include <cstdio>
#include <cstdlib>

namespace {

bool sSourceReady = false;
constexpr size_t kSourceRecordBytes = 64;
constexpr size_t kSourceRecordCount = 3;
std::vector<LakeWater::CartridgeWaveSource> sSourceRecords;
LakeWater::LakeDomain sDomain{};
std::vector<LakeWater::CartridgeWaveSource> sInitialSources;
std::vector<LakeWater::CartridgeWaveSource> sSources;
bool sDomainReady = false;
f32 sOriginX = 0.0f, sOriginZ = 0.0f;

LakeWater::WaveField sField;
PlayState* sOwner = nullptr;
bool sPublished = false;

bool IsReadyForLake(PlayState* play) {
    return play != nullptr && play == sOwner && play->sceneNum == SCENE_LAKE_HYLIA &&
           LakeWater::WaveField::kCartridgeParityVerified && sSourceReady && sDomainReady;
}

} // namespace

extern "C" void LakeWaterSimulation_Initialize(PlayState* play) {
    sOwner = play;
    sField.Reset();
    sPublished = false;
    u8 records[kSourceRecordBytes * kSourceRecordCount];
    sSourceReady = LakeWaterCartridge_LoadSourceRecords(records, sizeof(records));
    sDomainReady = false;
    sSourceRecords.clear();
    sInitialSources.clear();
    sSources.clear();
    if (sSourceReady) {
        for (size_t i = 0; i < kSourceRecordCount; ++i) {
            LakeWater::CartridgeWaveSource source{};
            if (!LakeWater::CartridgeWaveSource::Decode(records + i * kSourceRecordBytes,
                                                         kSourceRecordBytes, source)) {
                sSourceReady = false;
                sSourceRecords.clear();
                break;
            }
            sSourceRecords.push_back(source);
        }
    }
}

extern "C" void LakeWaterSimulation_Reset(PlayState* play) {
    if (play == nullptr || play == sOwner) {
        sField.Reset();
        sPublished = false;
        if (sDomainReady) {
            sField.SetDomain(sDomain.centerU, sDomain.centerV, sDomain.radius);
        }
        sSources = sInitialSources;
    }
}

extern "C" s32 LakeWaterSimulation_ConfigureDomain(PlayState* play, f32 actorX, f32 actorZ) {
    if (play == nullptr || play != sOwner || !sSourceReady) {
        return false;
    }
    if (sDomainReady && actorX == sOriginX && actorZ == sOriginZ) {
        return true;
    }
    sDomainReady = false;
    sPublished = false;
    std::vector<LakeWater::LakeXZ> points;
    for (s32 low = 0; low <= 1; ++low) {
        const LakeWaterTriangle* triangles = nullptr;
        u32 count = 0;
        if (!LakeWater_GetFootprint(low, actorX, actorZ, &triangles, &count)) {
            return false;
        }
        for (u32 i = 0; i < count; ++i) {
            for (const auto& point : triangles[i].points) {
                points.push_back({point.x, point.z});
            }
        }
    }
    if (!LakeWater::ComputeLakeDomain(points.data(), points.size(), 3, sDomain)) {
        return false;
    }
    if (!LakeWater::TileCartridgeSourceLayout(sSourceRecords, sDomain, actorX, actorZ, sInitialSources)) {
        sInitialSources.clear();
        return false;
    }
    sField.Reset();
    if (!sField.SetDomain(sDomain.centerU, sDomain.centerV, sDomain.radius)) {
        return false;
    }
    sSources = sInitialSources;
    sOriginX = actorX;
    sOriginZ = actorZ;
    sDomainReady = true;
    const char* trace = std::getenv("SHIP_WR64_WATER_TRACE");
    if (trace != nullptr && trace[0] != '\0' && trace[0] != '0') {
        std::fprintf(stderr, "[LakeWater] fixed domain center=%d,%d radius=%u sourceTiles=%zu\n",
                     sDomain.centerU, sDomain.centerV, sDomain.radius, sSources.size());
    }
    return true;
}

extern "C" s32 LakeWaterSimulation_IsReady(PlayState* play) {
    return IsReadyForLake(play) && sPublished;
}

extern "C" s32 LakeWaterSimulation_CanRun(PlayState* play) {
    return IsReadyForLake(play);
}

extern "C" void LakeWaterSimulation_Step(PlayState* play, f32 cameraX, f32 cameraZ) {
    if (!IsReadyForLake(play)) {
        return;
    }

    // The lake keeps one fixed field: camera motion cannot relocate waves.
    (void)cameraX;
    (void)cameraZ;
    for (auto& source : sSources) {
        source.Step(sField);
    }
    sField.Step();
    sPublished = true;
}

extern "C" s32 LakeWaterSimulation_Sample(PlayState* play, f32 x, f32 z, f32 meanHeight, f32* ySurface) {
    if (!LakeWaterSimulation_IsReady(play) || ySurface == nullptr) {
        return false;
    }

    const LakeWater::SurfaceSample sample = sField.Sample(x, z, meanHeight);
    if (!sample.valid) {
        return false;
    }
    *ySurface = sample.height;
    return true;
}

extern "C" void* LakeWaterSimulation_GetPublishedField(PlayState* play) {
    return LakeWaterSimulation_IsReady(play) ? &sField : nullptr;
}
