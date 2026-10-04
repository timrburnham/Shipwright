#ifndef SOH_LAKE_WATER_H
#define SOH_LAKE_WATER_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the Lake Hylia water adapter when its water-plane actor is created. */
void LakeWater_Initialize(PlayState* play);

/** Release scene-owned water state when the water-plane actor is destroyed. */
void LakeWater_Destroy(PlayState* play);

/** Advance/publish the adapter once from the game update timeline. */
void LakeWater_Update(PlayState* play);

/**
 * Sample an eligible Lake Hylia water box. Returns false when no authentic
 * Wave Race sample is available; callers must then retain Zelda's flat mean.
 */
s32 LakeWater_Sample(PlayState* play, WaterBox* waterBox, f32 x, f32 z, f32* ySurface);

/** Return the immutable-for-this-frame field shared by contact and draw code. */
void* LakeWater_GetPublishedWaveField(PlayState* play);

typedef struct LakeWaterCartridgeAppearance LakeWaterCartridgeAppearance;

/** Read the Original decal inputs and Zelda's final applied fog state. */
s32 LakeWater_GetCartridgeAppearance(PlayState* play, LakeWaterCartridgeAppearance* appearance);

typedef struct LakeWaterPoint3f {
    f32 x;
    /* Original water-mesh vertex Y relative to the water-plane actor. */
    f32 y;
    f32 z;
} LakeWaterPoint3f;

typedef enum LakeWaterLayer {
    LAKE_WATER_LAYER_MAIN = 0,
    LAKE_WATER_LAYER_LOWER_RIVER = 1,
    LAKE_WATER_LAYER_UPPER_RIVER = 2,
    /* Vertical/falling or transitional triangles are visible geometry only. */
    LAKE_WATER_LAYER_FALL = 3,
} LakeWaterLayer;

typedef struct LakeWaterTriangle {
    LakeWaterPoint3f points[3];
    u8 layer;
} LakeWaterTriangle;

enum { LAKE_WATER_MAX_FOOTPRINT_TRIANGLES = 40 };

/** Update the active mesh mode and actor origin for gameplay footprint tests. */
void LakeWater_SetRenderState(PlayState* play, s32 lowWater, f32 actorX, f32 actorZ);

/**
 * Return the exact high/low Zelda mesh triangles in world XZ with original
 * actor-local vertex Y and horizontal surface layer. The pointer is valid
 * until the next call; consumers retaining it must copy it into their
 * frame-owned storage. Returns false if the resource stream is unexpected.
 */
s32 LakeWater_GetFootprint(s32 lowWater, f32 actorX, f32 actorZ, const LakeWaterTriangle** triangles,
                           u32* triangleCount);

/** Submit the replacement surface when its cartridge renderer is ready. */
s32 LakeWater_Draw(PlayState* play, f32 meanLevel, s32 lowWater, f32 actorX, f32 actorZ, Gfx** gfx);

#ifdef __cplusplus
}
#endif

#endif
