#ifndef SOH_CARTRIDGE_WATER_RENDERER_H
#define SOH_CARTRIDGE_WATER_RENDERER_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zelda-side adaptation inputs for the cartridge Sunny Beach material. Values
 * must come from the evaluated environment, not from guessed course presets.
 * `environmentGray`/`environmentAlpha` are retained for the original command
 * sequence; the one-player decal combiner primarily uses texel alpha/color.
 */
typedef struct LakeWaterCartridgeAppearance {
    u8 environmentGray;
    u8 environmentAlpha;
    u8 fogR;
    u8 fogG;
    u8 fogB;
    u8 fogA;
    s16 fogNear;
    s16 fogFar;
    /* Evaluated main, lower-river, and upper-river water-box surfaces. */
    f32 waterLevels[3];
} LakeWaterCartridgeAppearance;

/* True only after ROM assets, appearance inputs, and both wet topologies pass
 * Preflight and the double-buffered renderer storage is reserved. */
s32 LakeWaterCartridge_IsReady(void);

/* Validates the extracted ROM assets and builds/caches both high/low wet
 * topologies. Safe to call every frame; it rebuilds only when actor origin
 * changes and must succeed before gameplay sampling is enabled. */
s32 LakeWaterCartridge_Preflight(s32 lowWater, f32 actorX, f32 actorZ,
                                 const LakeWaterCartridgeAppearance* appearance);

/* Read and hash-check the locally extracted 3-record (192-byte) source list. */
s32 LakeWaterCartridge_LoadSourceRecords(u8* records, u32 byteCount);

/* Query the same clipped, shore-adapted integer vertices used for drawing.
 * The caller supplies the freshly published field and the selected Zelda mean. */
s32 LakeWaterCartridge_SampleSurface(const void* publishedField, s32 lowWater,
                                    f32 actorX, f32 actorZ, f32 x, f32 z, u8 layer,
                                    f32 mean, f32* ySurface);

/* `publishedField` is a LakeWater::WaveField owned by the active published
 * simulation frame. Generated geometry is retained in host buffers following
 * Zelda's two graphics pools; it does not consume the small graphics arena.
 * Returns false without submitting if any prerequisite
 * is unavailable or malformed. */
s32 LakeWaterCartridge_Draw(PlayState* play, f32 meanLevel, s32 lowWater, f32 actorX, f32 actorZ,
                            const void* publishedField, const LakeWaterCartridgeAppearance* appearance, Gfx** gfx);

#ifdef __cplusplus
}
#endif

#endif
