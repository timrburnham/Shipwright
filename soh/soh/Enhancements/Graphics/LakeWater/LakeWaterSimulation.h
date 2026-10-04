#ifndef SOH_LAKE_WATER_SIMULATION_H
#define SOH_LAKE_WATER_SIMULATION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void LakeWaterSimulation_Initialize(PlayState* play);
void LakeWaterSimulation_Reset(PlayState* play);
s32 LakeWaterSimulation_ConfigureDomain(PlayState* play, f32 actorX, f32 actorZ);
s32 LakeWaterSimulation_CanRun(PlayState* play);
s32 LakeWaterSimulation_IsReady(PlayState* play);
void LakeWaterSimulation_Step(PlayState* play, f32 cameraX, f32 cameraZ);
s32 LakeWaterSimulation_Sample(PlayState* play, f32 x, f32 z, f32 meanHeight, f32* ySurface);
void* LakeWaterSimulation_GetPublishedField(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
