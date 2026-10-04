#ifndef SOH_CARTRIDGE_WATER_MATH_H
#define SOH_CARTRIDGE_WATER_MATH_H

#include <stdint.h>
#include <math.h>

typedef struct LakeWaterCartridgeUV {
    int16_t s;
    int16_t t;
} LakeWaterCartridgeUV;

/* func_80050204 samples (u-1,v-1), (u,v+1), (u+1,v) for each
 * interior vertex. Integer differences and float rounding precede truncation. */
static inline LakeWaterCartridgeUV LakeWater_ComputeCartridgeUV(int16_t heightA, int16_t heightB,
                                                               int16_t heightC) {
    const int32_t a = heightA >> 7;
    const int32_t b = heightB >> 7;
    const int32_t c = heightC >> 7;
    const int32_t dx = (a - b) * 55;
    const int32_t dz = (((a + b) >> 1) - c) * 64;
    const float inverseSlopeLength = 992.0f / sqrtf((float)(dx * dx + dz * dz + 0x00C00000));
    LakeWaterCartridgeUV uv = {
        (int16_t)(1024 + (int32_t)(dz * inverseSlopeLength)),
        (int16_t)(1024 + (int32_t)(dx * inverseSlopeLength)),
    };
    return uv;
}

static inline int16_t LakeWater_ComputeCartridgeVertexY(int16_t heightFixed, int32_t meanLevel) {
    return (int16_t)((heightFixed >> 8) + meanLevel);
}

#endif
