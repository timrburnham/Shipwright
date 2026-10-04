#include "global.h"
#include "soh/Enhancements/Graphics/LakeWater/LakeWater.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterAssets.h"
#include "soh/Enhancements/Graphics/LakeWater/CartridgeWaterRenderer.h"
#include "soh/Enhancements/Graphics/LakeWater/LakeWaterSimulation.h"
#include "soh/ResourceManagerHelpers.h"
#include "regs.h"
#include "objects/object_spot06_objects/object_spot06_objects.h"
#include <stdio.h>
#include <stdlib.h>

#define LAKE_WATER_MAX_DLIST_COMMANDS 128
#define LAKE_WATER_HIGH_VERTEX_COUNT 36
#define LAKE_WATER_LOW_VERTEX_COUNT 18
#define LAKE_WATER_HIGH_TRIANGLE_COUNT 37
#define LAKE_WATER_LOW_TRIANGLE_COUNT 12
#define LAKE_WATER_HIGH_VERTEX_HASH 0x426a94746bc46bf8ULL
#define LAKE_WATER_LOW_VERTEX_HASH 0xb648e41c43130dfeULL
#define LAKE_WATER_SIM_TICK_RATE 3

/* Scene-owned adapter state is shared by mesh submission and water queries. */
static PlayState* sLakeWaterPlay = NULL;
static s32 sTraceChecked = false;
static s32 sTraceEnabled = false;
static s32 sTraceLastFrame = -1;
static u32 sDrawAttempts = 0;
static u32 sSampleAttempts = 0;
static u32 sDrawsSubmitted = 0;
static u32 sSamplesUsed = 0;
static u32 sSamplesByLayer[3] = { 0, 0, 0 };
static u32 sSimulationTicks = 0;
static f32 sSampledMinY = 0.0f;
static f32 sSampledMaxY = 0.0f;
static s32 sSampledRangeValid = false;
static s32 sLowWater = false;
static s32 sWasEnabled = false;
static s32 sDrawFailed = false;
static s32 sLastFrameValid = false;
static s32 sTickAccumulator = 0;
static u32 sLastFrame = 0;
static f32 sActorX = 0.0f;
static f32 sActorZ = 0.0f;
static s32 sHighFootprintState = 0;
static s32 sLowFootprintState = 0;
static u32 sHighTriangleCount = 0;
static u32 sLowTriangleCount = 0;
static LakeWaterTriangle sHighFootprint[LAKE_WATER_MAX_FOOTPRINT_TRIANGLES];
static LakeWaterTriangle sLowFootprint[LAKE_WATER_MAX_FOOTPRINT_TRIANGLES];
static LakeWaterTriangle sWorldFootprint[LAKE_WATER_MAX_FOOTPRINT_TRIANGLES];

static s32 LakeWater_TraceEnabled(void) {
    if (!sTraceChecked) {
        const char* value = getenv("SHIP_WR64_WATER_TRACE");
        sTraceEnabled = value != NULL && value[0] != '\0' && value[0] != '0';
        sTraceChecked = true;
    }
    return sTraceEnabled;
}

static s32 LakeWater_IsEnabled(void) {
    return CVarGetInteger(CVAR_ENHANCEMENT("WaveRaceLakeWater"), 1) != 0;
}

/* Both visible geometry and Zelda contact queries must pass this same gate. */
static s32 LakeWater_IsReplacementReady(PlayState* play, s32 lowWater, f32 actorX, f32 actorZ,
                                        LakeWaterCartridgeAppearance* appearance, void** publishedField,
                                        const LakeWaterTriangle** footprint, u32* footprintCountOut) {
    const LakeWaterTriangle* triangles;
    u32 triangleCount;

    if (play == NULL || appearance == NULL || !LakeWater_IsEnabled() || sLakeWaterPlay != play ||
        play->sceneNum != SCENE_LAKE_HYLIA || sDrawFailed ||
        !LakeWater_GetCartridgeAppearance(play, appearance) ||
        !LakeWaterCartridge_Preflight(lowWater, actorX, actorZ, appearance) || !LakeWaterCartridge_IsReady() ||
        !LakeWaterSimulation_IsReady(play) ||
        !LakeWater_GetFootprint(lowWater, actorX, actorZ, &triangles, &triangleCount) ||
        triangles == NULL || triangleCount == 0) {
        return false;
    }
    if (footprint != NULL) {
        *footprint = triangles;
    }
    if (footprintCountOut != NULL) {
        *footprintCountOut = triangleCount;
    }
    if (publishedField != NULL) {
        *publishedField = LakeWaterSimulation_GetPublishedField(play);
        return *publishedField != NULL;
    }
    return LakeWaterSimulation_GetPublishedField(play) != NULL;
}

static void LakeWater_ResetSimulation(PlayState* play) {
    LakeWaterSimulation_Reset(play);
    sTickAccumulator = 0;
    sLastFrameValid = false;
}

static s32 LakeWater_AppendTriangle(LakeWaterTriangle* triangles, u32* count, u32 capacity,
                                    const Vtx* vertices, u32 vertexBase, u32 loadedVertexCount, u32 sourceVertexCount,
                                    u32 a, u32 b, u32 c) {
    const u32 indices[3] = { a, b, c };
    u32 i;

    if (*count >= capacity) {
        return false;
    }
    for (i = 0; i < 3; i++) {
        u32 index = indices[i];
        if (index >= loadedVertexCount || vertexBase + index >= sourceVertexCount) {
            return false;
        }
        triangles[*count].points[i].x = vertices[vertexBase + index].v.ob[0];
        triangles[*count].points[i].y = vertices[vertexBase + index].v.ob[1];
        triangles[*count].points[i].z = vertices[vertexBase + index].v.ob[2];
    }
    {
        const f32 y0 = triangles[*count].points[0].y;
        const f32 y1 = triangles[*count].points[1].y;
        const f32 y2 = triangles[*count].points[2].y;
        const f32 mainY = (sourceVertexCount == LAKE_WATER_LOW_VERTEX_COUNT) ? -680.0f : 0.0f;
        const f32 lowerY = (sourceVertexCount == LAKE_WATER_LOW_VERTEX_COUNT) ? 120.0f : 200.0f;
        if (y0 == y1 && y1 == y2 && y0 == mainY) {
            triangles[*count].layer = LAKE_WATER_LAYER_MAIN;
        } else if (y0 == y1 && y1 == y2 && y0 == lowerY) {
            triangles[*count].layer = LAKE_WATER_LAYER_LOWER_RIVER;
        } else if (y0 == y1 && y1 == y2 && y0 == 440.0f) {
            triangles[*count].layer = LAKE_WATER_LAYER_UPPER_RIVER;
        } else {
            triangles[*count].layer = LAKE_WATER_LAYER_FALL;
        }
    }
    (*count)++;
    return true;
}

static s32 LakeWater_BuildFootprint(s32 lowWater, LakeWaterTriangle* triangles, u32* triangleCount) {
    Gfx* displayList;
    Vtx* vertices;
    const char* vertexPath;
    u64 expectedHash;
    u32 vertexCount;
    u32 vertexBase = 0;
    u32 loadedVertexCount = 0;
    u32 commandIndex;
    u32 resultCount = 0;
    s32 sawEnd = false;

    if (lowWater) {
        displayList = ResourceMgr_LoadGfxByName(gLakeHyliaLowWaterDL);
        vertexPath = "__OTR__objects/object_spot06_objects/object_spot06_objectsVtx_000000";
        expectedHash = LAKE_WATER_LOW_VERTEX_HASH;
        vertexCount = LAKE_WATER_LOW_VERTEX_COUNT;
    } else {
        displayList = ResourceMgr_LoadGfxByName(gLakeHyliaHighWaterDL);
        vertexPath = "__OTR__objects/object_spot06_objects/object_spot06_objectsVtx_000230";
        expectedHash = LAKE_WATER_HIGH_VERTEX_HASH;
        vertexCount = LAKE_WATER_HIGH_VERTEX_COUNT;
    }
    vertices = ResourceMgr_LoadVtxByName((char*)vertexPath);
    if (displayList == NULL || vertices == NULL) {
        return false;
    }

    for (commandIndex = 0; commandIndex < LAKE_WATER_MAX_DLIST_COMMANDS; commandIndex++) {
        Gfx* command = &displayList[commandIndex];
        u32 opcode = command->words.w0 >> 24;

        if (opcode == G_MARKER) {
            // Resource identity marker is a two-command OTR payload.
            commandIndex++;
            continue;
        }
        if (opcode == G_VTX_OTR_HASH) {
            u64 hash;
            u32 offset = command->words.w1;
            u32 packed = command->words.w0;
            u32 nVertices = (packed >> 12) & 0xFF;
            u32 destination = ((packed >> 1) & 0x7F) - nVertices;

            if (commandIndex + 1 >= LAKE_WATER_MAX_DLIST_COMMANDS) {
                return false;
            }
            hash = ((u64)displayList[commandIndex + 1].words.w0 << 32) |
                   displayList[commandIndex + 1].words.w1;
            if (hash != expectedHash || offset % sizeof(Vtx) != 0 || destination != 0) {
                return false;
            }
            vertexBase = offset / sizeof(Vtx);
            loadedVertexCount = nVertices;
            if (vertexBase + loadedVertexCount > vertexCount) {
                return false;
            }
            commandIndex++;
            continue;
        }
        if (opcode == G_TRI2) {
            u32 wordIndex;
            for (wordIndex = 0; wordIndex < 2; wordIndex++) {
                u32 word = wordIndex == 0 ? command->words.w0 : command->words.w1;
                u32 a = (word >> 17) & 0x7F;
                u32 b = (word >> 9) & 0x7F;
                u32 c = (word >> 1) & 0x7F;
                if (!LakeWater_AppendTriangle(triangles, &resultCount, LAKE_WATER_MAX_FOOTPRINT_TRIANGLES,
                                              vertices, vertexBase, loadedVertexCount, vertexCount, a, b, c)) {
                    return false;
                }
            }
            continue;
        }
        if (opcode == G_TRI1) {
            u32 a = (command->words.w0 >> 17) & 0x7F;
            u32 b = (command->words.w0 >> 9) & 0x7F;
            u32 c = (command->words.w0 >> 1) & 0x7F;
            if (!LakeWater_AppendTriangle(triangles, &resultCount, LAKE_WATER_MAX_FOOTPRINT_TRIANGLES,
                                          vertices, vertexBase, loadedVertexCount, vertexCount, a, b, c)) {
                return false;
            }
            continue;
        }
        if (opcode == G_ENDDL) {
            sawEnd = true;
            break;
        }
    }

    if (!sawEnd || resultCount != (lowWater ? LAKE_WATER_LOW_TRIANGLE_COUNT : LAKE_WATER_HIGH_TRIANGLE_COUNT)) {
        return false;
    }
    *triangleCount = resultCount;
    if (LakeWater_TraceEnabled()) {
        f32 minX = 32767.0f, minZ = 32767.0f, maxX = -32768.0f, maxZ = -32768.0f;
        u32 t, p;
        for (t = 0; t < resultCount; t++) {
            for (p = 0; p < 3; p++) {
                f32 x = triangles[t].points[p].x;
                f32 z = triangles[t].points[p].z;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (z < minZ) minZ = z;
                if (z > maxZ) maxZ = z;
            }
        }
        u32 layerCounts[4] = { 0, 0, 0, 0 };
        for (t = 0; t < resultCount; t++) {
            if (triangles[t].layer < ARRAY_COUNT(layerCounts)) layerCounts[triangles[t].layer]++;
        }
        fprintf(stderr, "[LakeWater] %s footprint local bounds x=%.0f..%.0f z=%.0f..%.0f "
                        "layers(main/lower/upper/fall)=%u/%u/%u/%u\n",
                lowWater ? "low" : "high", minX, maxX, minZ, maxZ,
                layerCounts[0], layerCounts[1], layerCounts[2], layerCounts[3]);
    }
    return true;
}

void LakeWater_Initialize(PlayState* play) {
    if (play == NULL || play->sceneNum != SCENE_LAKE_HYLIA) {
        return;
    }

    sLakeWaterPlay = play;
    sTraceLastFrame = -1;
    sDrawAttempts = 0;
    sSampleAttempts = 0;
    sDrawsSubmitted = 0;
    sSamplesUsed = 0;
    sSamplesByLayer[0] = sSamplesByLayer[1] = sSamplesByLayer[2] = 0;
    sSimulationTicks = 0;
    sSampledRangeValid = false;
    sHighFootprintState = 0;
    sLowFootprintState = 0;
    sHighTriangleCount = 0;
    sLowTriangleCount = 0;
    sLowWater = false;
    sActorX = sActorZ = 0.0f;
    sWasEnabled = false;
    sDrawFailed = false;
    sTickAccumulator = 0;
    sLastFrameValid = false;
    LakeWaterSimulation_Initialize(play);
}

void LakeWater_Destroy(PlayState* play) {
    if (sLakeWaterPlay == play) {
        sLakeWaterPlay = NULL;
        sTraceLastFrame = -1;
        sDrawAttempts = 0;
        sSampleAttempts = 0;
        sDrawsSubmitted = 0;
        sSamplesUsed = 0;
        sSamplesByLayer[0] = sSamplesByLayer[1] = sSamplesByLayer[2] = 0;
        sSimulationTicks = 0;
        sSampledRangeValid = false;
        sHighFootprintState = 0;
        sLowFootprintState = 0;
        sWasEnabled = false;
        sDrawFailed = false;
        LakeWater_ResetSimulation(play);
        LakeWaterSimulation_Initialize(NULL);
    }
}

void LakeWater_Update(PlayState* play) {
    s32 enabled;
    s32 updateRate;

    if (play == NULL || sLakeWaterPlay != play || play->sceneNum != SCENE_LAKE_HYLIA) {
        return;
    }

    if (LakeWater_TraceEnabled() && play->gameplayFrames % 120 == 0 &&
        sTraceLastFrame != (s32)play->gameplayFrames) {
        LakeWaterCartridgeAppearance traceAppearance;
        const s32 replacementReady = LakeWater_IsReplacementReady(play, sLowWater, sActorX, sActorZ,
                                                                    &traceAppearance, NULL, NULL, NULL);
        const char* status = !LakeWater_IsEnabled() ? "disabled; Zelda fallback" :
                             (sDrawFailed ? "draw failed; Zelda fallback" :
                              (replacementReady ? "ready" : "missing prerequisite; Zelda fallback"));
        f32 meanMain1 = 0.0f;
        f32 meanMain2 = 0.0f;
        f32 meanLower = 0.0f;
        f32 meanUpper = 0.0f;
        if (play->colCtx.colHeader != NULL && play->colCtx.colHeader->numWaterBoxes >= 4) {
            meanUpper = play->colCtx.colHeader->waterBoxes[0].ySurface;
            meanLower = play->colCtx.colHeader->waterBoxes[1].ySurface;
            meanMain1 = play->colCtx.colHeader->waterBoxes[2].ySurface;
            meanMain2 = play->colCtx.colHeader->waterBoxes[3].ySurface;
        }
        fprintf(stderr,
                "[LakeWater] status=%s drawCalls=%u submitted=%u sampleCalls=%u sampled=%u ticks=%u "
                "highTriangles=%u lowTriangles=%u mode=%s means(upper/lower/main0/main1)=%0.2f/%0.2f/%0.2f/%0.2f "
                "samples(main/lower/upper)=%u/%u/%u sampledY=%s%.2f..%.2f frame=%u\n",
                status,
                sDrawAttempts, sDrawsSubmitted, sSampleAttempts, sSamplesUsed, sSimulationTicks,
                sHighTriangleCount, sLowTriangleCount, sLowWater ? "low" : "high",
                meanUpper, meanLower, meanMain1, meanMain2,
                sSamplesByLayer[LAKE_WATER_LAYER_MAIN], sSamplesByLayer[LAKE_WATER_LAYER_LOWER_RIVER],
                sSamplesByLayer[LAKE_WATER_LAYER_UPPER_RIVER],
                sSampledRangeValid ? "" : "unavailable ",
                sSampledRangeValid ? sSampledMinY : 0.0f, sSampledRangeValid ? sSampledMaxY : 0.0f,
                play->gameplayFrames);
        sTraceLastFrame = play->gameplayFrames;
        sDrawAttempts = 0;
        sDrawsSubmitted = 0;
        sSampleAttempts = 0;
        sSamplesUsed = 0;
        sSamplesByLayer[LAKE_WATER_LAYER_MAIN] = 0;
        sSamplesByLayer[LAKE_WATER_LAYER_LOWER_RIVER] = 0;
        sSamplesByLayer[LAKE_WATER_LAYER_UPPER_RIVER] = 0;
        sSimulationTicks = 0;
        sSampledRangeValid = false;
    }

    enabled = LakeWater_IsEnabled();
    if (!enabled) {
        if (sWasEnabled) {
            LakeWater_ResetSimulation(play);
            sDrawFailed = false;
        }
        sWasEnabled = false;
        return;
    }

    if (!sWasEnabled) {
        sWasEnabled = true;
        sDrawFailed = false;
        LakeWaterSimulation_Initialize(play);
        sTickAccumulator = 0;
        sLastFrame = play->gameplayFrames;
        sLastFrameValid = true;
        return;
    }

    if (sLastFrameValid && sLastFrame == play->gameplayFrames) {
        return;
    }
    if (sLastFrameValid && play->gameplayFrames < sLastFrame) {
        LakeWater_ResetSimulation(play);
    }
    sLastFrame = play->gameplayFrames;
    sLastFrameValid = true;

    LakeWaterCartridgeAppearance appearance;
    if (!LakeWater_GetCartridgeAppearance(play, &appearance) ||
        !LakeWaterCartridge_Preflight(sLowWater, sActorX, sActorZ, &appearance) ||
        !LakeWaterCartridge_IsReady()) {
        sTickAccumulator = 0;
        return;
    }

    if (!LakeWaterSimulation_CanRun(play)) {
        sTickAccumulator = 0;
        return;
    }

    updateRate = R_UPDATE_RATE;
    if (updateRate > 0) {
        sTickAccumulator += updateRate;
        while (sTickAccumulator >= LAKE_WATER_SIM_TICK_RATE) {
            LakeWaterSimulation_Step(play, 0.0f, 0.0f);
            sSimulationTicks++;
            sTickAccumulator -= LAKE_WATER_SIM_TICK_RATE;
        }
    }

}

s32 LakeWater_Sample(PlayState* play, WaterBox* waterBox, f32 x, f32 z, f32* ySurface) {
    WaterBox* boxes;
    const LakeWaterTriangle* triangles;
    LakeWaterCartridgeAppearance appearance;
    void* publishedField = NULL;
    u32 triangleCount;
    u32 i;
    s32 insideFootprint = false;

    s32 targetLayer;
    if (play == NULL || waterBox == NULL || play->sceneNum != SCENE_LAKE_HYLIA ||
        play->colCtx.colHeader == NULL || play->colCtx.colHeader->numWaterBoxes < 4) {
        return false;
    }
    boxes = play->colCtx.colHeader->waterBoxes;
    if (waterBox == &boxes[0]) {
        targetLayer = LAKE_WATER_LAYER_UPPER_RIVER;
    } else if (waterBox == &boxes[1]) {
        targetLayer = LAKE_WATER_LAYER_LOWER_RIVER;
    } else if (waterBox == &boxes[2] || waterBox == &boxes[3]) {
        targetLayer = LAKE_WATER_LAYER_MAIN;
    } else {
        return false;
    }
    sSampleAttempts++;
    if (!LakeWater_IsReplacementReady(play, sLowWater, sActorX, sActorZ, &appearance, &publishedField,
                                      &triangles, &triangleCount)) {
        return false;
    }
    /* Point-in-triangle mask test; separate Zelda boxes retain their native mean. */
    for (i = 0; i < triangleCount; i++) {
        const LakeWaterTriangle* triangle = &triangles[i];
        if (triangle->layer != targetLayer) continue;
        f32 x0 = triangle->points[0].x;
        f32 z0 = triangle->points[0].z;
        f32 x1 = triangle->points[1].x;
        f32 z1 = triangle->points[1].z;
        f32 x2 = triangle->points[2].x;
        f32 z2 = triangle->points[2].z;
        // Repeated source vertices produce zero-area triangles. They cannot
        // define a wet region (three identical corners would accept all XZ).
        if ((x1 - x0) * (z2 - z0) - (z1 - z0) * (x2 - x0) == 0.0f) {
            continue;
        }
        f32 d1 = (x - x1) * (z0 - z1) + (z - z1) * (x1 - x0);
        f32 d2 = (x - x2) * (z1 - z2) + (z - z2) * (x2 - x1);
        f32 d3 = (x - x0) * (z2 - z0) + (z - z0) * (x0 - x2);
        if ((d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f) ||
            (d1 <= 0.0f && d2 <= 0.0f && d3 <= 0.0f)) {
            insideFootprint = true;
            break;
        }
    }
    if (!insideFootprint) {
        return false;
    }
    if (!LakeWaterCartridge_SampleSurface(publishedField, sLowWater, sActorX, sActorZ, x, z,
                                           (u8)targetLayer, waterBox->ySurface, ySurface)) {
        return false;
    }
    sSamplesUsed++;
    sSamplesByLayer[targetLayer]++;
    if (!sSampledRangeValid) {
        sSampledMinY = sSampledMaxY = *ySurface;
        sSampledRangeValid = true;
    } else {
        if (*ySurface < sSampledMinY) sSampledMinY = *ySurface;
        if (*ySurface > sSampledMaxY) sSampledMaxY = *ySurface;
    }
    return true;
}

void* LakeWater_GetPublishedWaveField(PlayState* play) {
    LakeWaterCartridgeAppearance appearance;
    void* publishedField = NULL;
    if (!LakeWater_IsReplacementReady(play, sLowWater, sActorX, sActorZ, &appearance, &publishedField, NULL, NULL)) {
        return NULL;
    }
    return publishedField;
}

s32 LakeWater_GetCartridgeAppearance(PlayState* play, LakeWaterCartridgeAppearance* appearance) {
    if (play == NULL || appearance == NULL || play->sceneNum != SCENE_LAKE_HYLIA) {
        return false;
    }

    /*
     * The one-player Sunny Beach material's env color is an unmodulated white
     * decal with the Zelda water-plane alpha. Fog is adapted from Zelda's
     * final LightContext values, which z_kankyo has already blended and
     * adjusted for the current time, room, and environmental effects.
     */
    appearance->environmentGray = 255;
    appearance->environmentAlpha = 128;
    if (play->colCtx.colHeader == NULL || play->colCtx.colHeader->numWaterBoxes < 4) return false;
    appearance->waterLevels[0] = play->colCtx.colHeader->waterBoxes[2].ySurface;
    appearance->waterLevels[1] = play->colCtx.colHeader->waterBoxes[1].ySurface;
    appearance->waterLevels[2] = play->colCtx.colHeader->waterBoxes[0].ySurface;
    appearance->fogR = play->lightCtx.fogColor[0];
    appearance->fogG = play->lightCtx.fogColor[1];
    appearance->fogB = play->lightCtx.fogColor[2];
    appearance->fogA = 0;
    appearance->fogNear = play->lightCtx.fogNear;
    appearance->fogFar = play->lightCtx.fogFar;

    /* The original fog-factor commands divide by (fogFar - fogNear). */
    return appearance->fogFar > appearance->fogNear;
}

s32 LakeWater_Draw(PlayState* play, f32 meanLevel, s32 lowWater, f32 actorX, f32 actorZ, Gfx** gfx) {
    LakeWaterCartridgeAppearance appearance;
    void* publishedField;

    sDrawAttempts++;
    if (gfx == NULL || !LakeWater_IsReplacementReady(play, lowWater, actorX, actorZ, &appearance, &publishedField,
                                                       NULL, NULL)) {
        return false;
    }
    if (!LakeWaterCartridge_Draw(play, meanLevel, lowWater, actorX, actorZ, publishedField, &appearance, gfx)) {
        /* Do not leave wave-height contacts active after a visible submission failure. */
        sDrawFailed = true;
        LakeWater_ResetSimulation(play);
        return false;
    }
    sDrawsSubmitted++;
    return true;
}

void LakeWater_SetRenderState(PlayState* play, s32 lowWater, f32 actorX, f32 actorZ) {
    if (play == NULL || sLakeWaterPlay != play || play->sceneNum != SCENE_LAKE_HYLIA) {
        return;
    }
    sLowWater = lowWater;
    sActorX = actorX;
    sActorZ = actorZ;
    if (!LakeWaterSimulation_ConfigureDomain(play, actorX, actorZ)) {
        sTickAccumulator = 0;
    }
    LakeWaterCartridgeAppearance appearance;
    if (!LakeWater_GetCartridgeAppearance(play, &appearance) ||
        !LakeWaterCartridge_Preflight(lowWater, actorX, actorZ, &appearance)) {
        sTickAccumulator = 0;
    }
    if (LakeWater_TraceEnabled() && (lowWater ? sLowFootprintState : sHighFootprintState) == 0) {
        const LakeWaterTriangle* triangles;
        u32 triangleCount;
        LakeWater_GetFootprint(lowWater, actorX, actorZ, &triangles, &triangleCount);
    }
}

s32 LakeWater_GetFootprint(s32 lowWater, f32 actorX, f32 actorZ, const LakeWaterTriangle** triangles,
                           u32* triangleCount) {
    LakeWaterTriangle* localTriangles;
    u32* localCount;
    s32* localState;
    u32 i;

    if (triangles == NULL || triangleCount == NULL) {
        return false;
    }
    localTriangles = lowWater ? sLowFootprint : sHighFootprint;
    localCount = lowWater ? &sLowTriangleCount : &sHighTriangleCount;
    localState = lowWater ? &sLowFootprintState : &sHighFootprintState;
    if (*localState == 0) {
        *localState = LakeWater_BuildFootprint(lowWater, localTriangles, localCount) ? 1 : -1;
    }
    if (*localState != 1 || *localCount > LAKE_WATER_MAX_FOOTPRINT_TRIANGLES) {
        return false;
    }
    for (i = 0; i < *localCount; i++) {
        u32 point;
        for (point = 0; point < 3; point++) {
            sWorldFootprint[i].points[point].x = localTriangles[i].points[point].x + actorX;
            sWorldFootprint[i].points[point].y = localTriangles[i].points[point].y;
            sWorldFootprint[i].points[point].z = localTriangles[i].points[point].z + actorZ;
        }
        sWorldFootprint[i].layer = localTriangles[i].layer;
    }
    *triangles = sWorldFootprint;
    *triangleCount = *localCount;
    return true;
}
