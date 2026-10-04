#ifndef SOH_CARTRIDGE_WATER_ASSETS_H
#define SOH_CARTRIDGE_WATER_ASSETS_H

/* ROM-derived Original Wave Race draw contract. See tools/extract_wr64_cartridge_water.py. */
#include "libultraship/libultra/gbi.h"
#include <math.h>
#include <stddef.h>

enum {
    LAKE_WATER_WR64_SETUP_DL = 0x01008290,
    LAKE_WATER_WR64_ONE_PLAYER_DL = 0x0100B590,
    LAKE_WATER_WR64_TEXTURE_SEGMENTED_ADDRESS = 0x08000400,
    LAKE_WATER_WR64_TEXTURE_WIDTH = 32,
    LAKE_WATER_WR64_TEXTURE_HEIGHT = 32,
    LAKE_WATER_WR64_TEXTURE_SCALE_S = 0x8000,
    LAKE_WATER_WR64_TEXTURE_SCALE_T = 0x8000,
    LAKE_WATER_WR64_BODY_Y = -100,
    LAKE_WATER_WR64_BODY_R = 0,
    LAKE_WATER_WR64_BODY_G = 242,
    LAKE_WATER_WR64_BODY_B = 228,
};

/*
 * Emit the one-player Drake Lake race material from Draw_WaterEffects. Pass
 * the generated ROM texture as a host pointer, the live grayscale environment
 * value/alpha, the selected course fog RGBA, and the original near/far values
 * (unk98/unk9C). G_CC macros expand into multiple combiner fields, so emit the
 * actual GBI commands instead of trying to store them in a scalar descriptor.
 */
static inline Gfx* LakeWater_EmitOriginalDrakeLakeMaterial(Gfx* gfx, const u32* texture,
                                                            u8 environmentGray, u8 environmentAlpha,
                                                            u8 fogR, u8 fogG, u8 fogB, u8 fogA,
                                                            s32 fogNear, s32 fogFar) {
    if (gfx == NULL || texture == NULL || fogFar <= fogNear) {
        return gfx;
    }
    gSPClearGeometryMode(gfx++, G_ZBUFFER | G_TEXTURE_ENABLE | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING |
                                      G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LOD | G_SHADING_SMOOTH | 0xFFE0CDF8);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_FOG | G_SHADING_SMOOTH);
    gSPTexture(gfx++, LAKE_WATER_WR64_TEXTURE_SCALE_S, LAKE_WATER_WR64_TEXTURE_SCALE_T, 0, G_TX_RENDERTILE, G_ON);
    gDPPipeSync(gfx++);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                        LAKE_WATER_WR64_TEXTURE_WIDTH, LAKE_WATER_WR64_TEXTURE_HEIGHT, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetEnvColor(gfx++, environmentGray, environmentGray, environmentGray, environmentAlpha);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    gDPSetRenderMode(gfx++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_PASS2);
    gDPSetFogColor(gfx++, fogR, fogG, fogB, fogA);
    gSPFogFactor(gfx++, (0x1F4 * 0x100) / (fogFar - fogNear),
                 ((0x1F4 - fogNear) * 0x100) / (fogFar - fogNear));
    return gfx;
}

/* Sunny Beach's one-player race path is the common one-cycle DECALRGBA
 * material. The selected texture is the course-specific segment-8 +0x400
 * RGBA32 block; unlike Drake Lake, this branch has no fog or smooth-shading
 * setup. Keep this as a separate emitter so state cannot silently inherit the
 * Drake two-cycle fog policy. */
static inline Gfx* LakeWater_EmitOriginalSunnyBeachMaterial(Gfx* gfx, const u32* texture,
                                                             u8 environmentGray, u8 environmentAlpha) {
    if (gfx == NULL || texture == NULL) {
        return gfx;
    }
    gSPClearGeometryMode(gfx++, G_ZBUFFER | G_TEXTURE_ENABLE | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING |
                                      G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LOD | G_SHADING_SMOOTH | 0xFFE0CDF8);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE);
    gSPTexture(gfx++, LAKE_WATER_WR64_TEXTURE_SCALE_S, LAKE_WATER_WR64_TEXTURE_SCALE_T, 0, G_TX_RENDERTILE, G_ON);
    gDPPipeSync(gfx++);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                        LAKE_WATER_WR64_TEXTURE_WIDTH, LAKE_WATER_WR64_TEXTURE_HEIGHT, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    /* Exact sampler state initialized by D_1008290 for Sunny Beach. */
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    gDPSetTextureLOD(gfx++, G_TL_TILE);
    gDPSetTextureDetail(gfx++, G_TD_CLAMP);
    gDPSetTextureLUT(gfx++, G_TT_NONE);
    gDPSetTextureConvert(gfx++, G_TC_FILT);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    gSPSetOtherMode(gfx++, G_SETOTHERMODE_H, G_MDSFT_PIPELINE, 1, G_PM_NPRIMITIVE);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetEnvColor(gfx++, environmentGray, environmentGray, environmentGray, environmentAlpha);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    return gfx;
}

/*
 * Renderer-test fixture: fuse the ROM Sunny Beach body color into its decal pass.
 * Production uses the separate passes to retain underwater actor visibility.
 * This uses the same ROM texture, one-cycle XLU/Z-compare material, then
 * computes (TEXEL0 - PRIMITIVE) * TEXEL0_ALPHA + PRIMITIVE with a constant-one
 * output alpha. The opaque composite color removes the need to draw the coarse
 * D_106F930 underlay over the Zeldamapped water surface; the XLU render mode
 * still compares depth without updating it.
 */
static inline Gfx* LakeWater_EmitOriginalSunnyBeachCompositeMaterial(Gfx* gfx, const u32* texture) {
    gfx = LakeWater_EmitOriginalSunnyBeachMaterial(gfx, texture, 255, 255);
    if (gfx == NULL || texture == NULL) {
        return gfx;
    }
    gDPSetPrimColor(gfx++, 0, 0, LAKE_WATER_WR64_BODY_R, LAKE_WATER_WR64_BODY_G,
                    LAKE_WATER_WR64_BODY_B, 255);
    gDPSetCombineLERP(gfx++, TEXEL0, PRIMITIVE, TEXEL0_ALPHA, PRIMITIVE,
                      0, 0, 0, 1,
                      TEXEL0, PRIMITIVE, TEXEL0_ALPHA, PRIMITIVE,
                      0, 0, 0, 1);
    return gfx;
}

/*
 * Sunny Beach's course backdrop quad is drawn immediately before the animated
 * water list. The ROM display list D_106F8E8 clears then enables Z/shade/cull,
 * selects one cycle, sets the exact AA/Z mode (including depth updates) and uses the
 * shade combiner. D_106F930 supplies the fixed RGBA(0,242,228,255) vertices
 * at local Y=-100. Keep this separate from the textured water highlight pass.
 */
static inline Gfx* LakeWater_EmitOriginalSunnyBeachBodyMaterial(Gfx* gfx) {
    if (gfx == NULL) {
        return gfx;
    }
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_CULL_BACK);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    /* Exact Original B9 word 0x00552230, not the nearby stock AA_ZB_XLU_SURF preset. */
    gDPSetRenderMode(gfx++, 0x00552230, 0);
    gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    return gfx;
}

#include "CartridgeWaterMath.h"

#endif
