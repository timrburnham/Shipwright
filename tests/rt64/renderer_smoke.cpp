#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include "fast/rt64/Renderer.h"
#include "fast/rt64/DisplayListTranslator.h"
#include "fast/rt64/SdlVulkanWindow.h"
#include "fast/lus_gbi.h"
#include "libultraship/libultra/gbi.h"
#include "imgui.h"
#include <SDL.h>
#include <cmath>
#include "common/rt64_math.h"

namespace {
bool VerifyAxisAlignedProjection() {
    // A combined view/projection with zero camera yaw used to yield 0/0
    // during decomposition, corrupting geometry when widescreen is enabled.
    hlslpp::float4x4 projection(0.0f);
    projection[0][0] = 1.0f;
    projection[1][1] = 1.5f;
    projection[2][2] = -1.01f;
    projection[2][3] = -1.0f;
    projection[3][2] = -2.01f;
    for (float yaw : {0.0f, 0.000001f, 1.5707963f}) {
        hlslpp::float4x4 view = hlslpp::float4x4::identity();
        const auto rotation = RT64::matrixRotationY(yaw);
        for (uint32_t r = 0; r < 3; ++r) for (uint32_t c = 0; c < 3; ++c) view[r][c] = rotation[r][c];
        view[3][2] = -10.0f;
        const auto combined = hlslpp::mul(view, projection);
        hlslpp::float4x4 decodedView, decodedProjection;
        RT64::matrixDecomposeViewProj(combined, decodedView, decodedProjection);
        const auto reconstructed = hlslpp::mul(decodedView, decodedProjection);
        if (RT64::matrixIsNaN(reconstructed)) return false;
        for (uint32_t r = 0; r < 4; ++r) for (uint32_t c = 0; c < 4; ++c) {
            if (std::abs(float(reconstructed[r][c]) - float(combined[r][c])) > 0.0001f) return false;
        }
    }
    return true;
}
bool HasDisplay() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* videoDriver = std::getenv("SDL_VIDEODRIVER");
    if (videoDriver != nullptr && std::string(videoDriver) == "dummy") return false;
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display != nullptr && display[0] != '\0') || (wayland != nullptr && wayland[0] != '\0');
#endif
}

Gfx syntheticFrame[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_FILL),
    gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress),
    gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, 320, 240),
    gsDPSetFillColor((0xF801u << 16) | 0xF801u),
    gsDPFillRectangle(0, 0, 319, 239),
    gsDPFullSync(),
    gsSPEndDisplayList(),
};

Gfx grayscaleFrame[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress),
    gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, 320, 240),
    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),
    gsDPSetPrimColor(0, 0, 255, 0, 0, 255),
    gsDPSetGrayscaleColor(255, 255, 255, 255),
    gsSPGrayscale(true),
    gsDPFillRectangle(0, 0, 319, 239),
    gsSPGrayscale(false),
    gsDPFullSync(),
    gsSPEndDisplayList(),
};

bool VerifyFramebufferCopyReadback(Fast::Rt64Renderer& renderer) {
    Fast::GfxRenderingAPI* guiApi = renderer.GetGuiApi();
    const int sourceId = guiApi->CreateFramebuffer();
    const int destinationId = guiApi->CreateFramebuffer();
    guiApi->UpdateFramebufferParameters(sourceId, 320, 240, 1, false, true, false, false);
    guiApi->UpdateFramebufferParameters(destinationId, 320, 240, 1, false, true, false, false);

    std::array<uint16_t, 1> redSample{};
    std::array<uint16_t, 1> blueSample{};
    Gfx commands[28]{};
    Gfx* cursor = commands;
    gsSPSetFB(cursor++, sourceId);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetFillColor(cursor++, (0xF801u << 16) | 0xF801u);
    gDPFillRectangle(cursor++, 0, 0, 319, 119);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, 120, 319, 239);
    gDPFullSync(cursor++);
    gDPCopyFB(cursor++, destinationId, sourceId, false, nullptr);
    gDPReadFB(cursor++, destinationId, redSample.data(), 160, 60, 1, 1, false);
    gDPReadFB(cursor++, destinationId, blueSample.data(), 160, 180, 1, 1, false);
    gsSPResetFB(cursor++);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCombineMode(cursor++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureImageFB(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(sourceId)));
    gDPImageRectangle(cursor++, 0, 0, 0, 0, 320 * 4, 240 * 4, 320, 240, 0, 320, 240);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();
    return redSample[0] == 0xF801 && blueSample[0] == 0x003F &&
           renderer.GetNativeColor(160, 60) == 0xF801 && renderer.GetNativeColor(160, 180) == 0x003F;
}

bool VerifyWideFramebufferCopy(Fast::Rt64Renderer& renderer) {
    // A native readback contains the center 320 pixels. Stretching that slice
    // to the wide destination moves the stripe boundaries; a full GPU copy
    // keeps them at their original world-screen coordinates.
    Gfx commands[40]{};
    Gfx* cursor = commands;
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetFillColor(cursor++, 0x003F003Fu);
    gDPFillWideRectangle(cursor++, -53, 0, 372, 239);
    gDPSetFillColor(cursor++, 0xFFFFFFFFu);
    gDPFillWideRectangle(cursor++, 80, 0, 239, 239);
    gDPCopyFB(cursor++, 2, 0, false, nullptr);
    gDPSetFillColor(cursor++, 0x00010001u);
    gDPFillWideRectangle(cursor++, -53, 0, 372, 239);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetTextureFilter(cursor++, G_TF_POINT);
    gDPSetCombineMode(cursor++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureImageFB(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(2)));
    gDPImageRectangle(cursor++, -53 * 4, 0, 0, 0, 373 * 4, 240 * 4, 320, 240,
                      G_TX_RENDERTILE, 320, 240);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);
    renderer.Run(commands, {});
    renderer.Present();
    return renderer.GetNativeColor(70, 120) == 0x003F &&
           renderer.GetNativeColor(90, 120) == 0xFFFF &&
           renderer.GetNativeColor(250, 120) == 0x003F;
}

bool VerifyOversizedNativeTextureUpload(Fast::Rt64Renderer& renderer) {
    constexpr uint32_t width = 160;
    constexpr uint32_t height = 160;
    Fast::GfxRenderingAPI* guiApi = renderer.GetGuiApi();
    // Reuse the first framebuffer allocated by VerifyFramebufferCopyReadback
    // so this smoke case stays within the runtime's five native-FB slots.
    constexpr int sourceId = 1;
    guiApi->UpdateFramebufferParameters(sourceId, width, height, 1, false, true, true, false);

    Gfx commands[32]{};
    Gfx* cursor = commands;
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, 0x00010001u);
    gDPFillRectangle(cursor++, 0, 0, 319, 239);

    // The preview source is 160x160 RGBA16 (50 KiB), far beyond the RDP's
    // 4 KiB TMEM. A blue lower half proves RT64 samples data beyond the first
    // 4 KiB when the framebuffer is registered as a native texture.
    gsSPSetFB(cursor++, sourceId);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0xF801u << 16) | 0xF801u);
    gDPFillRectangle(cursor++, 0, 0, width - 1, (height / 2) - 1);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, height / 2, width - 1, height - 1);
    gDPFullSync(cursor++);
    gsSPResetFB(cursor++);

    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_AA_TEX_EDGE, G_RM_AA_TEX_EDGE2);
    gDPSetOtherMode(cursor++, G_AD_DISABLE | G_CD_DISABLE | G_TF_POINT | G_TT_NONE |
                       G_TP_NONE | G_CYC_1CYCLE,
                       G_AC_NONE | G_ZS_PIXEL | G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
    gDPSetCombineMode(cursor++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTile(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width / 4, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(cursor++, G_TX_RENDERTILE, 0, 0, (width - 1) << 2, (height - 1) << 2);
    gSPTexture(cursor++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureImageFB(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(sourceId)));
    gSPTextureRectangle(cursor++, 80 << 2, 40 << 2, 240 << 2, 200 << 2, G_TX_RENDERTILE,
                        0, 0, 1 << 10, 1 << 10);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();
    return renderer.GetNativeColor(160, 80) == 0xF801 && renderer.GetNativeColor(160, 160) == 0x003F;
}

bool VerifyUnalignedTextureBlockDxt(Fast::Rt64Renderer& renderer) {
    constexpr uint32_t width = 46;
    constexpr uint32_t paddedWidth = 48;
    constexpr uint32_t height = 16;
    std::array<uint8_t, paddedWidth * height> ia8{};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < paddedWidth; ++x) {
            // A distinct intensity on each padded source row makes a bad DXT
            // row stride show up as repeated or out-of-order horizontal bands.
            ia8[y * paddedWidth + x] = static_cast<uint8_t>(((y & 0xfu) << 4) | 0xfu);
        }
    }

    Gfx commands[24]{};
    Gfx* cursor = commands;
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, 0x00010001u);
    gDPFillRectangle(cursor++, 0, 0, 319, 239);
    gDPPipeSync(cursor++);
    gDPSetOtherMode(cursor++, G_AD_DISABLE | G_CD_DISABLE | G_TF_POINT | G_TT_NONE |
                       G_TP_NONE | G_CYC_1CYCLE,
                       G_AC_NONE | G_ZS_PIXEL | G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
    gDPSetCombineMode(cursor++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(cursor++, 0, 0, 255, 255, 255, 255);
    gDPLoadTextureBlock(cursor++, ia8.data(), G_IM_FMT_IA, G_IM_SIZ_8b, width, height, 0,
                        G_TX_WRAP, G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTexture(cursor++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    gSPWideTextureRectangle(cursor++, 60 << 2, 60 << 2, (60 + width) << 2, (60 + height) << 2,
                            G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();

    uint16_t previous = 0;
    uint32_t distinctRows = 0;
    for (uint32_t y = 2; y < height - 1; ++y) {
        const uint16_t pixel = renderer.GetNativeColor(80, 60 + y);
        const uint16_t intensity = (pixel >> 11) & 0x1fu;
        if (y == 2 || intensity != previous) {
            ++distinctRows;
        }
        if (y > 2 && intensity < previous) {
            return false;
        }
        previous = intensity;
    }
    return distinctRows >= 12;
}

bool VerifyPausePreviewFramebuffer(Fast::Rt64Renderer& renderer) {
    Fast::GfxRenderingAPI* guiApi = renderer.GetGuiApi();
    const int previewId = guiApi->CreateFramebuffer();
    guiApi->UpdateFramebufferParameters(previewId, 64, 112, 1, false, true, true, false);

    // Player_DrawPauseImpl clears depth by temporarily naming its native depth
    // pointer as the color image, then clears color and sets the depth image.
    // Keep these distinct host pointers to exercise that exact CIMG/ZIMG path.
    std::array<uint16_t, 64 * 112> nativeColorStorage{};
    std::array<uint16_t, 64 * 112> nativeDepthStorage{};

    MtxS identity{};
    for (size_t diagonal = 0; diagonal < 4; ++diagonal) {
        identity.intPart[diagonal][diagonal ^ 1u] = 1;
    }
    MtxS projection{};
    projection.fracPart[0][1] = 2621; // guMtxF2L(0.04f), logical x scale
    projection.fracPart[1][0] = 2621; // guMtxF2L(0.04f), logical y scale
    projection.intPart[2][3] = 1;
    projection.intPart[3][2] = 1;

    Vp viewport{};
    viewport.vp.vscale[0] = viewport.vp.vtrans[0] = 128;
    viewport.vp.vscale[1] = viewport.vp.vtrans[1] = 224;
    viewport.vp.vscale[2] = viewport.vp.vtrans[2] = 511;

    Fast::F3DVtx directionalVertices[3]{};
    directionalVertices[0].v.ob[0] = -20;
    directionalVertices[0].v.ob[1] = -18;
    directionalVertices[1].v.ob[0] = -4;
    directionalVertices[1].v.ob[1] = -18;
    directionalVertices[2].v.ob[0] = -12;
    directionalVertices[2].v.ob[1] = 22;
    Fast::F3DVtx pointVertices[3]{};
    pointVertices[0].v.ob[0] = 4;
    pointVertices[0].v.ob[1] = -18;
    pointVertices[1].v.ob[0] = 20;
    pointVertices[1].v.ob[1] = -18;
    pointVertices[2].v.ob[0] = 12;
    pointVertices[2].v.ob[1] = 22;
    for (auto* vertices : {directionalVertices, pointVertices}) {
        for (size_t index = 0; index < 3; ++index) {
            auto& vertex = vertices[index];
            vertex.v.cn[0] = 0;
            vertex.v.cn[1] = 0;
            vertex.v.cn[2] = 127;
            vertex.v.cn[3] = 255;
        }
    }
    Fast::F3DVtx fogVertices[3]{};
    fogVertices[0].v.ob[0] = -4;
    fogVertices[0].v.ob[1] = -8;
    fogVertices[1].v.ob[0] = 4;
    fogVertices[1].v.ob[1] = -8;
    fogVertices[2].v.ob[1] = 8;
    for (auto& vertex : fogVertices) {
        vertex.v.cn[0] = 255;
        vertex.v.cn[1] = 255;
        vertex.v.cn[2] = 255;
        vertex.v.cn[3] = 255;
    }
    Fast::F3DVtx primitiveVertices[3]{};
    primitiveVertices[0].v.ob[0] = -5;
    primitiveVertices[0].v.ob[1] = -6;
    primitiveVertices[1].v.ob[0] = 5;
    primitiveVertices[1].v.ob[1] = -6;
    primitiveVertices[2].v.ob[1] = -22;
    for (auto& vertex : primitiveVertices) {
        vertex.v.tc[0] = 32 << 5;
        vertex.v.tc[1] = 16 << 5;
    }
    MtxS uiProjection{};
    uiProjection.fracPart[0][1] = 410; // 1 / 160
    uiProjection.fracPart[1][0] = 546; // 1 / 120
    uiProjection.intPart[2][3] = 1;
    uiProjection.intPart[3][2] = 1;
    Vp uiViewport{};
    uiViewport.vp.vscale[0] = uiViewport.vp.vtrans[0] = 640;
    uiViewport.vp.vscale[1] = uiViewport.vp.vtrans[1] = 480;
    uiViewport.vp.vscale[2] = uiViewport.vp.vtrans[2] = 511;
    Fast::F3DVtx uiQuadVertices[4]{};
    const int16_t quadPositions[4][2] = {{-64, 112}, {64, 112}, {64, -112}, {-64, -112}};
    for (size_t index = 0; index < 4; ++index) {
        auto& vertex = uiQuadVertices[index];
        vertex.v.ob[0] = quadPositions[index][0];
        vertex.v.ob[1] = quadPositions[index][1];
        vertex.v.tc[0] = (index == 1 || index == 2) ? 64 << 5 : 0;
        vertex.v.tc[1] = index >= 2 ? 32 << 5 : 0;
        vertex.v.cn[3] = 255;
    }
    Lights1 directionalLights = gdSPDefLights1(20, 40, 60, 180, 120, 60, 0, 0, 127);
    // Real game Light_t records may carry arbitrary kc/kl bytes here. These
    // fields overlap the PointLight_t view but are padding in a directional
    // light, so they must not change how the light is decoded.
    directionalLights.l[0].p.unk3 = 0x6b;
    directionalLights.l[0].p.unk7 = 0xa5;
    Lights1 pointLights{};
    pointLights.a.l.col[0] = pointLights.a.l.colc[0] = 8;
    pointLights.a.l.col[1] = pointLights.a.l.colc[1] = 16;
    pointLights.a.l.col[2] = pointLights.a.l.colc[2] = 24;
    pointLights.l[0].p.col[0] = pointLights.l[0].p.colc[0] = 180;
    pointLights.l[0].p.col[1] = pointLights.l[0].p.colc[1] = 80;
    pointLights.l[0].p.col[2] = pointLights.l[0].p.colc[2] = 220;
    pointLights.l[0].p.unk3 = 1; // kc enables point-light handling
    pointLights.l[0].p.unk7 = 0;
    pointLights.l[0].p.pos[0] = 12;
    pointLights.l[0].p.pos[1] = 0;
    pointLights.l[0].p.pos[2] = 100;
    pointLights.l[0].p.unkE = 0;

    Gfx commands[128]{};
    Gfx* cursor = commands;
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, 0, 319, 239);
    gDPFullSync(cursor++);

    gsSPSetFB(cursor++, previewId);
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 64, nativeDepthStorage.data());
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0xfffcU << 16) | 0xfffcU);
    gDPFillRectangle(cursor++, 0, 0, 63, 111);
    gDPPipeSync(cursor++);
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 64, nativeColorStorage.data());
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, 0, 63, 111);
    gDPPipeSync(cursor++);
    gDPSetDepthImage(cursor++, nativeDepthStorage.data());
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 64, 112);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(cursor++, G_CC_SHADE, G_CC_SHADE);
    gSPSetLights1(cursor++, directionalLights);
    gSPClearGeometryMode(cursor++, G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR |
                                      0x00400000u);
    gSPSetGeometryMode(cursor++, G_ZBUFFER | G_SHADE | G_LIGHTING);
    gSPViewport(cursor++, &viewport);
    gSPMatrix(cursor++, &projection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(cursor++, &identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    __gSPVertex(cursor++, directionalVertices, 3, 0);
    gSP1Triangle(cursor++, 0, 1, 2, 0);
    gSPSetLights1(cursor++, pointLights);
    gSPSetGeometryMode(cursor++, 0x00400000u); // RT64 G_POINT_LIGHTING
    __gSPVertex(cursor++, pointVertices, 3, 0);
    gSP1Triangle(cursor++, 0, 1, 2, 0);
    // A full RDP mode command must update the F3DEX2 RSP shadow too. The
    // following partial H/L updates would otherwise rebuild from stale state.
    gDPSetOtherMode(cursor++, G_CYC_2CYCLE, G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2);
    gDPSetCycleType(cursor++, G_CYC_2CYCLE);
    gDPSetRenderMode(cursor++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(cursor++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(cursor++, 0, 0, 255, 0, 0, 255);
    __gSPVertex(cursor++, primitiveVertices, 3, 0);
    gSP1Triangle(cursor++, 0, 1, 2, 0);
    gDPSetFogColor(cursor++, 0, 255, 0, 255);
    gSPFogPosition(cursor++, 0, 255);
    gDPSetCycleType(cursor++, G_CYC_2CYCLE);
    gDPSetRenderMode(cursor++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(cursor++, G_CC_SHADE, G_CC_SHADE);
    gSPSetLights1(cursor++, directionalLights);
    gSPClearGeometryMode(cursor++, 0x00400000u);
    gSPSetGeometryMode(cursor++, G_FOG);
    __gSPVertex(cursor++, fogVertices, 3, 0);
    gSP1Triangle(cursor++, 0, 1, 2, 0);

    gsSPResetFB(cursor++);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetTexturePersp(cursor++, G_TP_PERSP);
    gDPSetRenderMode(cursor++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gSPClearGeometryMode(cursor++, G_FOG | G_LIGHTING | G_ZBUFFER | 0x00400000u);
    gDPSetCombineMode(cursor++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gSPViewport(cursor++, &uiViewport);
    gSPMatrix(cursor++, &uiProjection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(cursor++, &identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPSetTile(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(cursor++, G_TX_RENDERTILE, 0, 0, (64 - 1) << 2, (32 - 1) << 2);
    // Equipment loads its quad before binding the full 64x112 preview into
    // a logical 64x32 tile. Its framebuffer sync must preserve cached vertices.
    gSPTexture(cursor++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    __gSPVertex(cursor++, uiQuadVertices, 4, 0);
    gDPSetTextureImageFB(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 64,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(previewId)));
    gSP1Quadrangle(cursor++, 0, 1, 2, 3, 0);
    gSPTextureRectangle(cursor++, 240 << 2, 20 << 2, 252 << 2, 32 << 2, G_TX_RENDERTILE,
                        32 << 5, 16 << 5, 0, 0);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();
    const uint16_t referenceDirectional = renderer.GetNativeColor(126, 120);
    const auto directionalMatchesExpected = [](uint16_t pixel) {
        const uint32_t red = (pixel >> 11) & 0x1f;
        const uint32_t green = (pixel >> 6) & 0x1f;
        const uint32_t blue = (pixel >> 1) & 0x1f;
        const auto closeTo = [](uint32_t actual, uint32_t expected) {
            return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 2;
        };
        return closeTo(red, 24) && closeTo(green, 19) && closeTo(blue, 14);
    };
    if (!directionalMatchesExpected(referenceDirectional)) {
        return false;
    }
    // Re-run the same frame with alternating nonzero values in the native
    // directional-light padding. Neither byte is a point-light discriminator
    // for a GBI that has not enabled RT64 point lighting.
    constexpr std::array<std::array<uint8_t, 2>, 2> lightPadding = {{{0x11, 0x73}, {0xd1, 0x2b}}};
    for (const auto& padding : lightPadding) {
        directionalLights.l[0].p.unk3 = padding[0];
        directionalLights.l[0].p.unk7 = padding[1];
        renderer.Run(commands, {});
        renderer.Present();
        const uint16_t repeatedDirectional = renderer.GetNativeColor(126, 120);
        if (!directionalMatchesExpected(repeatedDirectional) || repeatedDirectional != referenceDirectional) {
            return false;
        }
    }
    const uint16_t directional = renderer.GetNativeColor(126, 120);
    const uint16_t point = renderer.GetNativeColor(188, 120);
    const uint16_t primitive = renderer.GetNativeColor(160, 180);
    const uint16_t fog = renderer.GetNativeColor(160, 120);
    const uint16_t boundPreview = renderer.GetNativeColor(246, 26);
    const uint32_t red = (directional >> 11) & 0x1f;
    const uint32_t green = (directional >> 6) & 0x1f;
    const uint32_t blue = (directional >> 1) & 0x1f;
    const uint32_t pointRed = (point >> 11) & 0x1f;
    const uint32_t pointGreen = (point >> 6) & 0x1f;
    const uint32_t pointBlue = (point >> 1) & 0x1f;
    const uint32_t primitiveRed = (primitive >> 11) & 0x1f;
    const uint32_t primitiveGreen = (primitive >> 6) & 0x1f;
    const uint32_t primitiveBlue = (primitive >> 1) & 0x1f;
    const uint32_t fogRed = (fog >> 11) & 0x1f;
    const uint32_t fogGreen = (fog >> 6) & 0x1f;
    const uint32_t fogBlue = (fog >> 1) & 0x1f;
    const uint32_t boundRed = (boundPreview >> 11) & 0x1f;
    const uint32_t boundGreen = (boundPreview >> 6) & 0x1f;
    const uint32_t boundBlue = (boundPreview >> 1) & 0x1f;
    const auto closeTo = [](uint32_t actual, uint32_t expected) {
        return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 2;
    };
    return closeTo(red, 24) && closeTo(green, 19) && closeTo(blue, 14) && pointRed > pointGreen + 4 &&
           pointBlue > pointGreen + 4 && primitiveRed >= 29 && primitiveGreen <= 1 && primitiveBlue <= 1 &&
           fogGreen >= 29 && fogRed <= 1 && fogBlue <= 1 && boundGreen >= 29 && boundRed <= 1 && boundBlue <= 1 &&
           renderer.GetNativeColor(98, 10) == 0x003F;
}

bool VerifyHudTexturePerspectiveAndAlpha(Fast::Rt64Renderer& renderer) {
    // The HUD commonly draws IA textures with vertex UVs under G_TP_NONE.
    // RT64's texture sampler applies a half-UV correction in that mode, so
    // the native RSP texture coordinates must compensate for it. Compare two
    // identical quads rendered side by side, one with each perspective mode.
    std::array<uint8_t, 16 * 16> icon{};
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int dx = std::abs(x - 7);
            const int dy = std::abs(y - 7);
            const int radius = std::max(dx, dy);
            const uint8_t alpha = radius <= 3 ? 0xF : (radius == 4 ? 0x8 : 0x0);
            // IA8 packs intensity in the upper nibble and alpha in the lower.
            icon[static_cast<size_t>(y * 16 + x)] = static_cast<uint8_t>((0xF << 4) | alpha);
        }
    }

    MtxS identity{};
    for (size_t diagonal = 0; diagonal < 4; ++diagonal) {
        identity.intPart[diagonal][diagonal ^ 1u] = 1;
    }
    MtxS projection{};
    projection.fracPart[0][1] = 512; // exact 1 / 128
    projection.fracPart[1][0] = 512; // exact 1 / 128
    projection.intPart[2][3] = 1;
    projection.intPart[3][2] = 1;
    Vp viewport{};
    viewport.vp.vscale[0] = 512;
    viewport.vp.vtrans[0] = 640;
    viewport.vp.vscale[1] = 512;
    viewport.vp.vtrans[1] = 480;
    viewport.vp.vscale[2] = viewport.vp.vtrans[2] = 511;

    Fast::F3DVtx vertices[8]{};
    const int16_t centers[2] = {-48, 48};
    for (size_t quad = 0; quad < 2; ++quad) {
        const int16_t left = static_cast<int16_t>(centers[quad] - 16);
        const int16_t right = static_cast<int16_t>(centers[quad] + 16);
        const int16_t positions[4][2] = {{left, 16}, {right, 16}, {left, -16}, {right, -16}};
        for (size_t i = 0; i < 4; ++i) {
            auto& vertex = vertices[quad * 4 + i];
            vertex.v.ob[0] = positions[i][0];
            vertex.v.ob[1] = positions[i][1];
            vertex.v.tc[0] = (i == 1 || i == 3) ? 16 << 5 : 0;
            vertex.v.tc[1] = i >= 2 ? 16 << 5 : 0;
            vertex.v.cn[3] = 255;
        }
    }

    Gfx commands[48]{};
    Gfx* cursor = commands;
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, 0, 319, 239);
    gDPPipeSync(cursor++);
    gDPSetOtherMode(cursor++, G_AD_DISABLE | G_CD_DISABLE | G_TF_POINT | G_TT_NONE |
                       G_TP_PERSP | G_CYC_1CYCLE,
                       G_AC_NONE | G_ZS_PIXEL | G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
    gDPSetCombineMode(cursor++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(cursor++, 0, 0, 255, 255, 255, 255);
    gSPLoadGeometryMode(cursor++, G_SHADING_SMOOTH);
    gSPViewport(cursor++, &viewport);
    gSPMatrix(cursor++, &projection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(cursor++, &identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPLoadTextureBlock(cursor++, icon.data(), G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                        G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    gSPTexture(cursor++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTexturePersp(cursor++, G_TP_NONE);
    __gSPVertex(cursor++, vertices, 4, 0);
    gSP1Quadrangle(cursor++, 0, 2, 3, 1, 0);
    gDPSetTexturePersp(cursor++, G_TP_PERSP);
    __gSPVertex(cursor++, &vertices[4], 4, 0);
    gSP1Quadrangle(cursor++, 0, 2, 3, 1, 0);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();

    // Compare every native pixel in corresponding 64x64 regions, including
    // transparent pixels and antialiased alpha edges. The two quads differ
    // only by the texture-perspective mode.
    for (uint32_t y = 88; y < 152; ++y) {
        for (uint32_t x = 80; x < 144; ++x) {
            if (renderer.GetNativeColor(x, y) != renderer.GetNativeColor(x + 96, y)) {
                std::cerr << "HUD mismatch " << x << "," << y << " none=" << renderer.GetNativeColor(x,y) << " persp=" << renderer.GetNativeColor(x+96,y) << "\n";
                return false;
            }
        }
    }
    uint32_t coloredPixels = 0;
    for (uint32_t y = 88; y < 152; ++y) {
        for (uint32_t x = 80; x < 144; ++x) {
            const uint16_t pixel = renderer.GetNativeColor(x, y);
            if (((pixel >> 11) & 0x1f) > 20 && ((pixel >> 6) & 0x1f) > 20 && ((pixel >> 1) & 0x1f) > 20) {
                ++coloredPixels;
            }
        }
    }
    if (coloredPixels < 40 || coloredPixels > 600) std::cerr << "HUD coverage " << coloredPixels << "\n";
    return coloredPixels >= 40 && coloredPixels <= 600;
}

bool VerifyNativeTriangle(Fast::Rt64Renderer& renderer, bool perspective = false) {
    MtxS identity{};
    for (size_t diagonal = 0; diagonal < 4; ++diagonal) {
        // Native guMtxF2L layout packs adjacent matrix columns into one word;
        // RT64 reads each logical column through the paired-column XOR.
        identity.intPart[diagonal][diagonal ^ 1u] = 1;
    }
    MtxS projection{};
    projection.fracPart[0][1] = 655; // guMtxF2L(0.01f), logical column 0
    projection.fracPart[1][0] = 655; // guMtxF2L(0.01f), logical column 1
    projection.intPart[2][3] = 1;
    projection.intPart[3][2] = 1;

    if (perspective) {
        projection = {};
        projection.intPart[0][1] = 1;
        projection.intPart[1][0] = 1;
        projection.intPart[2][3] = -1;
        projection.intPart[2][2] = -1;
        projection.intPart[3][2] = 200;
    }

    Vp viewport{};
    viewport.vp.vscale[0] = 640;
    viewport.vp.vscale[1] = 480;
    viewport.vp.vscale[2] = 511;
    viewport.vp.vscale[3] = 0;
    viewport.vp.vtrans[0] = 640;
    viewport.vp.vtrans[1] = 480;
    viewport.vp.vtrans[2] = 511;
    viewport.vp.vtrans[3] = 0;

    Fast::F3DVtx vertices[3]{};
    vertices[0].v.ob[0] = -60;
    vertices[0].v.ob[1] = -50;
    vertices[1].v.ob[0] = 60;
    vertices[1].v.ob[1] = -50;
    vertices[2].v.ob[0] = 0;
    vertices[2].v.ob[1] = 60;
    for (auto& vertex : vertices) {
        vertex.v.cn[0] = 255;
        vertex.v.cn[1] = 255;
        vertex.v.cn[2] = 255;
        vertex.v.cn[3] = 255;
    }

    Gfx commands[32]{};
    Gfx* cursor = commands;
    gDPPipeSync(cursor++);
    gDPSetDepthImage(cursor++, reinterpret_cast<void*>(static_cast<uintptr_t>(Fast::DisplayListTranslator::DepthAddress)));
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320,
                     reinterpret_cast<void*>(static_cast<uintptr_t>(Fast::DisplayListTranslator::DepthAddress)));
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0xfffcU << 16) | 0xfffcU);
    if (perspective) {
        gDPFillWideRectangle(cursor++, -80, 0, 399, 239);
    } else {
        gDPFillRectangle(cursor++, 0, 0, 319, 239);
    }
    gDPFullSync(cursor++);
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetDepthImage(cursor++, reinterpret_cast<void*>(static_cast<uintptr_t>(Fast::DisplayListTranslator::DepthAddress)));
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    if (perspective) {
        gDPFillWideRectangle(cursor++, -80, 0, 399, 239);
    } else {
        gDPFillRectangle(cursor++, 0, 0, 319, 239);
    }
    gDPPipeSync(cursor++);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(cursor++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(cursor++, 0, 0, 255, 0, 0, 255);
    gSPClearGeometryMode(cursor++, G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(cursor++, G_ZBUFFER | G_SHADE);
    gSPViewport(cursor++, &viewport);
    gSPMatrix(cursor++, &projection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(cursor++, &identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    __gSPVertex(cursor++, vertices, 3, 0);
    gSP1Triangle(cursor++, 0, 1, 2, 0);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();
    const uint16_t inside = renderer.GetNativeColor(160, 120);
    const uint16_t outside = renderer.GetNativeColor(20, 20);
    const uint16_t depth = renderer.GetDepth(160, 120);
    return inside == 0xF801 && outside == 0x003F && std::abs(static_cast<int>(depth) - 32766) < 256;
}

bool VerifyWideEdgeRectangle(Fast::Rt64Renderer& renderer) {
    Gfx commands[16]{};
    Gfx* cursor = commands;
    gDPSetColorImage(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, Fast::DisplayListTranslator::ColorAddress);
    gDPSetScissor(cursor++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPSetCycleType(cursor++, G_CYC_FILL);
    gDPSetRenderMode(cursor++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(cursor++, (0x003Fu << 16) | 0x003Fu);
    gDPFillRectangle(cursor++, 0, 0, 319, 239);
    gDPPipeSync(cursor++);
    gDPSetCycleType(cursor++, G_CYC_1CYCLE);
    gDPSetRenderMode(cursor++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(cursor++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(cursor++, 0, 0, 255, 255, 255, 255);
    // A negative start must preserve the visible part within the native
    // readback image rather than wrap around the standard RDP coordinate field.
    gDPFillWideRectangle(cursor++, -20, 20, 20, 40);
    gDPFullSync(cursor++);
    gSPEndDisplayList(cursor);

    renderer.Run(commands, {});
    renderer.Present();
    return renderer.GetNativeColor(8, 30) == 0xFFFF && renderer.GetNativeColor(160, 30) == 0x003F;
}

std::string FindNativeRedBounds(Fast::Rt64Renderer& renderer) {
    uint32_t count = 0;
    uint32_t minX = 320, minY = 240, maxX = 0, maxY = 0;
    for (uint32_t y = 0; y < 240; ++y) {
        for (uint32_t x = 0; x < 320; ++x) {
            const uint16_t pixel = renderer.GetNativeColor(x, y);
            const uint32_t red = (pixel >> 11) & 0x1f;
            const uint32_t green = (pixel >> 6) & 0x1f;
            const uint32_t blue = (pixel >> 1) & 0x1f;
            if (red > green + 4 && red > blue + 4) {
                ++count;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    std::ostringstream result;
    result << " redPixels=" << count;
    if (count > 0) {
        result << " bounds=[" << minX << ',' << minY << ".." << maxX << ',' << maxY << ']';
    }
    return result.str();
}
} // namespace

int main() {
    if (!VerifyAxisAlignedProjection()) {
        std::cerr << "RT64 axis-aligned view/projection decomposition failed\n";
        return 1;
    }
    if (!HasDisplay()) {
        std::cout << "SKIP: no desktop display is available for the RT64 Vulkan smoke test\n";
        return 77;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cout << "SKIP: SDL has no usable video device for the RT64 Vulkan smoke test: " << SDL_GetError()
                  << '\n';
        return 77;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    Fast::SdlVulkanWindow window;
    try {
        window.Init("RT64 renderer smoke", "Vulkan", false, 640, 480, 100, 100);
        if (!window.IsRunning() || window.GetWindow() == nullptr) {
            std::cerr << "SDL could not create a Vulkan window" << '\n';
            return 1;
        }

        Fast::Rt64Renderer renderer(window.GetWindow());
        if (renderer.GetGuiApi() == nullptr) {
            std::cerr << "RT64 did not expose its GUI rendering API" << '\n';
            return 1;
        }
        const std::unordered_map<Mtx*, MtxF> matrixReplacements;
        for (int frame = 0; frame < 3; ++frame) {
            window.HandleEvents();
            renderer.Run(frame == 0 ? syntheticFrame : grayscaleFrame, matrixReplacements);
            if (frame == 0 && renderer.GetNativeColor(160, 120) != 0xF801) {
                std::cerr << "RT64 did not write the synthetic fill color to native RDRAM" << '\n';
                return 1;
            }
            if (frame == 1) {
                const uint16_t pixel = renderer.GetNativeColor(160, 120);
                const float red = ((pixel >> 11) & 0x1f) / 31.0f;
                const float green = ((pixel >> 6) & 0x1f) / 31.0f;
                const float blue = ((pixel >> 1) & 0x1f) / 31.0f;
                if (std::abs(red - 1.0f / 3.0f) > 0.08f || std::abs(green - 1.0f / 3.0f) > 0.08f ||
                    std::abs(blue - 1.0f / 3.0f) > 0.08f) {
                    std::cerr << "RT64 grayscale shader did not tint the red primitive to equal channels; pixel=0x"
                              << std::hex << pixel << std::dec << '\n';
                    return 1;
                }
            }
            renderer.BeginGuiFrame();
            ImGui::Begin("RT64 smoke test");
            ImGui::Text("Synthetic frame %d", frame + 1);
            ImGui::End();
            renderer.EndGuiFrame();
            renderer.Present();
        }
        if (!VerifyFramebufferCopyReadback(renderer)) {
            std::cerr << "RT64 framebuffer create/copy/read did not preserve the synthetic RGBA16 pixel" << '\n';
            return 1;
        }
        if (!VerifyOversizedNativeTextureUpload(renderer)) {
            std::cerr << "RT64 oversized 160x160 native texture upload did not preserve pixels past the first 4 KiB: top=0x"
                      << std::hex << renderer.GetNativeColor(160, 80) << " bottom=0x"
                      << renderer.GetNativeColor(160, 160) << std::dec << '\n';
            return 1;
        }
        if (!VerifyNativeTriangle(renderer)) {
            std::cerr << "RT64 native fixed-point matrix/vertex triangle readback failed: inside=0x"
                      << std::hex << renderer.GetNativeColor(160, 120) << " outside=0x"
                      << renderer.GetNativeColor(20, 20) << " depth=0x" << renderer.GetDepth(160, 120)
                      << std::dec << '\n';
            return 1;
        }
        renderer.SetResolution(2.0f);
        if (!VerifyNativeTriangle(renderer)) {
            std::cerr << "RT64 2x-resolution native triangle depth readback failed: inside=0x"
                      << std::hex << renderer.GetNativeColor(160, 120) << " outside=0x"
                      << renderer.GetNativeColor(20, 20) << " depth=0x" << renderer.GetDepth(160, 120)
                      << std::dec << FindNativeRedBounds(renderer) << '\n';
            return 1;
        }
        renderer.SetResolution(1.0f);
        renderer.SetMsaa(4);
        if (!VerifyNativeTriangle(renderer)) {
            std::cerr << "RT64 4x-MSAA native triangle depth readback failed: inside=0x"
                      << std::hex << renderer.GetNativeColor(160, 120) << " outside=0x"
                      << renderer.GetNativeColor(20, 20) << " depth=0x" << renderer.GetDepth(160, 120)
                      << std::dec << '\n';
            return 1;
        }
        renderer.SetResolution(2.0f);
        if (!VerifyNativeTriangle(renderer)) {
            std::cerr << "RT64 2x-resolution/4x-MSAA native triangle depth readback failed: inside=0x"
                      << std::hex << renderer.GetNativeColor(160, 120) << " outside=0x"
                      << renderer.GetNativeColor(20, 20) << " depth=0x" << renderer.GetDepth(160, 120)
                      << std::dec << FindNativeRedBounds(renderer) << '\n';
            return 1;
        }
        if (!VerifyFramebufferCopyReadback(renderer)) {
            std::cerr << "RT64 scaled framebuffer copy/read/image-rectangle readback failed" << '\n';
            return 1;
        }
        if (!VerifyPausePreviewFramebuffer(renderer)) {
            std::cerr << "RT64 pause-preview framebuffer CIMG/ZIMG rendering or image sampling failed: center=0x"
                      << std::hex << renderer.GetNativeColor(160, 120) << " background=0x"
                      << renderer.GetNativeColor(98, 10) << " directional=0x" << renderer.GetNativeColor(126, 120)
                      << " point=0x" << renderer.GetNativeColor(188, 120) << " fog=0x"
                      << renderer.GetNativeColor(160, 120) << " primitive=0x" << renderer.GetNativeColor(160, 180)
                      << " direct-bind=0x" << renderer.GetNativeColor(246, 26) << std::dec << '\n';
            return 1;
        }
        if (!VerifyHudTexturePerspectiveAndAlpha(renderer)) {
            std::cerr << "RT64 HUD IA8 texture-perspective/alpha-edge regression failed: none=0x"
                      << std::hex << renderer.GetNativeColor(112, 120) << " persp=0x"
                      << renderer.GetNativeColor(208, 120) << std::dec << '\n';
            return 1;
        }
        renderer.SetRenderSize(426, 240);
        if (!VerifyNativeTriangle(renderer) || !VerifyNativeTriangle(renderer, true)) {
            std::cerr << "RT64 widescreen native triangle failed center=" << renderer.GetNativeColor(160, 120) << '\n';
            return 1;
        }
        if (!VerifyWideEdgeRectangle(renderer)) {
            std::cerr << "RT64 widescreen native wide rectangle missed the left edge or painted outside its bounds: edge=0x"
                      << std::hex << renderer.GetNativeColor(8, 30) << " center=0x"
                      << renderer.GetNativeColor(160, 30) << std::dec << '\n';
            return 1;
        }
        if (!VerifyWideFramebufferCopy(renderer)) {
            std::cerr << "RT64 widescreen framebuffer copy changed image proportions: " << std::hex
                      << renderer.GetNativeColor(70, 120) << ' ' << renderer.GetNativeColor(90, 120)
                      << ' ' << renderer.GetNativeColor(250, 120) << std::dec << '\n';
            return 1;
        }
        renderer.SetRenderSize(640, 480);
        // Recreating the shader cache must remain safe while its asynchronous
        // workers are starting or finishing an optimized shader compilation.
        for (int change = 0; change < 4; ++change) {
            renderer.SetMsaa(change % 2 == 0 ? 1 : 4);
            if (!VerifyNativeTriangle(renderer)) {
                std::cerr << "RT64 repeated MSAA switching failed at change " << change << '\n';
                return 1;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << "RT64 renderer smoke test failed: " << exception.what() << '\n';
        return 1;
    }

    window.Destroy();
    std::cout << "PASS: native/scaled rendering, depth, grayscale, GUI, framebuffer operations and MSAA switching\n";
    return 0;
}
