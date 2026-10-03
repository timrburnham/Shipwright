#include "fast/rt64/DisplayListTranslator.h"
#include "fast/rt64/TexturePatches.h"
#include "fast/resource/type/Texture.h"
#include "fast/lus_gbi.h"
#include "libultraship/libultra/gbi.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fast {
struct DisplayListTranslatorTestAccess {
    static uint32_t CopyTextureResource(DisplayListTranslator& translator, const std::shared_ptr<Texture>& texture,
                                        size_t nativeSize, uint32_t nativeWidth, uint32_t bitsPerTexel,
                                        uint8_t format, uint8_t sizeCode, const char* path) {
        return translator.CopyTextureResource(texture, nativeSize, nativeWidth, bitsPerTexel, format, sizeCode,
                                              path);
    }
    static uint32_t CopyMovemem(DisplayListTranslator& translator, const void* source, size_t size, uint8_t index) {
        return translator.CopyMovemem(source, size, index);
    }
};
} // namespace Fast

namespace {
Fast::F3DVtx nativeStaticVertex{};

struct RtCommand {
    uint32_t w0;
    uint32_t w1;
};

struct RtVertex {
    int16_t y;
    int16_t x;
    uint16_t flag;
    int16_t z;
    int16_t t;
    int16_t s;
    uint8_t a;
    uint8_t b;
    uint8_t g;
    uint8_t r;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

RtCommand ReadCommand(const Fast::DisplayListTranslator& translator, uint32_t address, size_t index) {
    RtCommand result{};
    const auto& rdram = translator.GetRdram();
    const size_t offset = static_cast<size_t>(address) + index * sizeof(RtCommand);
    Require(offset + sizeof(result) <= rdram.size(), "translated command is outside RDRAM");
    std::memcpy(&result, rdram.data() + offset, sizeof(result));
    return result;
}

std::vector<uint8_t> ReadBytes(const Fast::DisplayListTranslator& translator, uint32_t address, size_t size) {
    const auto& rdram = translator.GetRdram();
    Require(static_cast<size_t>(address) + size <= rdram.size(), "translated data is outside RDRAM");
    return {rdram.begin() + address, rdram.begin() + address + size};
}

void TestNativeVertexPacking() {
    Fast::F3DVtx vertex{};
    vertex.v.ob[0] = 0x1234;
    vertex.v.ob[1] = -2;
    vertex.v.ob[2] = 0x4567;
    vertex.v.flag = 0x89ab;
    vertex.v.tc[0] = 0x1357;
    vertex.v.tc[1] = -0x2468;
    vertex.v.cn[0] = 0x11;
    vertex.v.cn[1] = 0x22;
    vertex.v.cn[2] = 0x33;
    vertex.v.cn[3] = 0x44;

    Gfx commands[3]{};
    gDPSetTexturePersp(&commands[0], G_TP_PERSP);
    __gSPVertex(&commands[1], &vertex, 1, 0);
    gSPEndDisplayList(&commands[2]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand vertexCommand = ReadCommand(translator, root, 1);
    Require((vertexCommand.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_VTX),
            "native vertex command was not retained as F3DEX2 G_VTX");

    RtVertex packed{};
    const auto packedBytes = ReadBytes(translator, vertexCommand.w1, sizeof(packed));
    std::memcpy(&packed, packedBytes.data(), sizeof(packed));
    Require(packed.y == -2 && packed.x == 0x1234 && packed.flag == 0x89ab && packed.z == 0x4567,
            "RT64 vertex position did not decode as y,x / flag,z");
    Require(packed.t == -0x2468 && packed.s == 0x1357,
            "RT64 vertex texture coordinates did not decode as t,s");
    Require(packed.a == 0x44 && packed.b == 0x33 && packed.g == 0x22 && packed.r == 0x11,
            "RT64 vertex color did not decode as a,b,g,r");
}

void TestTriangleUvPreservationAcrossOtherModes() {
    const auto translateQuad = [](bool texturePerspective, bool fullOtherMode) {
        std::array<Fast::F3DVtx, 4> vertices{};
        vertices[0].v.tc[0] = INT16_MAX;
        vertices[0].v.tc[1] = INT16_MIN;
        vertices[1].v.tc[0] = 0x220;
        vertices[1].v.tc[1] = 0x40;
        vertices[2].v.tc[0] = INT16_MIN;
        vertices[2].v.tc[1] = INT16_MAX;
        vertices[3].v.tc[0] = 0x20;
        vertices[3].v.tc[1] = 0x240;

        Gfx commands[4]{};
        if (fullOtherMode) {
            gDPSetOtherMode(&commands[0], texturePerspective ? G_TP_PERSP : G_TP_NONE, 0);
        } else {
            gDPSetTexturePersp(&commands[0], texturePerspective ? G_TP_PERSP : G_TP_NONE);
        }
        __gSPVertex(&commands[1], vertices.data(), vertices.size(), 0);
        gSP1Quadrangle(&commands[2], 0, 2, 3, 1, 0);
        gSPEndDisplayList(&commands[3]);

        Fast::DisplayListTranslator translator;
        const uint32_t root = translator.Translate(commands);
        const RtCommand vertexCommand = ReadCommand(translator, root, 1);
        const auto bytes = ReadBytes(translator, vertexCommand.w1, sizeof(vertices));
        std::array<RtVertex, 4> packed{};
        std::memcpy(packed.data(), bytes.data(), bytes.size());
        return packed;
    };

    for (bool fullOtherMode : { false, true }) {
        for (bool texturePerspective : { false, true }) {
            const auto vertices = translateQuad(texturePerspective, fullOtherMode);
            Require(vertices[0].s == INT16_MAX && vertices[0].t == INT16_MIN &&
                        vertices[2].s == INT16_MIN && vertices[2].t == INT16_MAX,
                    fullOtherMode ? "full other-mode command changed native triangle UVs"
                                  : "partial other-mode command changed native triangle UVs");
        }
    }
}

void TestPointLightPacking() {
    Fast::DisplayListTranslator translator;
    const std::array<uint8_t, 16> native = {
        0x11, 0x22, 0x33, 0x44, // RGB, kc
        0x55, 0x66, 0x77, 0x88, // copied RGB, kl
        0x22, 0x11, 0x44, 0x33, // native x=0x1122, y=0x3344
        0x66, 0x55, 0x99, 0xAA  // native z=0x5566, kq, pad
    };
    const uint32_t address = Fast::DisplayListTranslatorTestAccess::CopyMovemem(
        translator, native.data(), native.size(), static_cast<uint8_t>(Fast::F3DEX2_G_MV_LIGHT));
    const auto& rdram = translator.GetRdram();
    const std::array<uint8_t, 16> expected = {
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x33, 0x44, 0x11, 0x22, 0xAA, 0x99, 0x55, 0x66
    };
    Require(std::memcmp(rdram.data() + address, expected.data(), expected.size()) == 0,
            "native point-light words were not preserved for deferred RSP decoding");
}

void TestDirectionalLightPacking() {
    Fast::DisplayListTranslator translator;
    const std::array<uint8_t, 16> native = {
        0x11, 0x22, 0x33, 0x7F, 0x11, 0x22, 0x33, 0x55,
        0x44, 0x55, 0x66, 0x31, 0, 0, 0, 0
    };
    const uint32_t address = Fast::DisplayListTranslatorTestAccess::CopyMovemem(
        translator, native.data(), native.size(), static_cast<uint8_t>(Fast::F3DEX2_G_MV_LIGHT));
    const auto& rdram = translator.GetRdram();
    const std::array<uint8_t, 16> expected = {
        0x7F, 0x33, 0x22, 0x11, 0x55, 0x33, 0x22, 0x11,
        0x31, 0x66, 0x55, 0x44, 0, 0, 0, 0
    };
    Require(std::memcmp(rdram.data() + address, expected.data(), expected.size()) == 0,
            "directional light words did not map to RT64 DirLight field order");
}

void TestTextureLutPartialOtherModeNormalization() {
    Gfx commands[5]{};
    gDPSetTextureLUT(&commands[0], 2);
    gDPSetTextureLUT(&commands[1], 3);
    gDPSetTextureLUT(&commands[2], 0);
    gDPSetTextureLUT(&commands[3], 2);
    commands[3].words.w1 = 0x8000;
    gSPEndDisplayList(&commands[4]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    Require(ReadCommand(translator, root, 0).w1 == 0x8000,
            "unshifted RGBA16 texture LUT mode was not moved into H bits 15:14");
    Require(ReadCommand(translator, root, 1).w1 == 0xc000,
            "unshifted IA16 texture LUT mode was not moved into H bits 15:14");
    Require(ReadCommand(translator, root, 2).w1 == 0,
            "none texture LUT mode did not remain zero");
    Require(ReadCommand(translator, root, 3).w1 == 0x8000,
            "already-shifted texture LUT mode was modified");
}

void TestSegmentedVertexAddress() {
    Fast::F3DVtx vertex{};
    vertex.v.ob[0] = 0x2468;
    vertex.v.ob[1] = -0x1234;
    vertex.v.cn[0] = 0x51;
    vertex.v.cn[1] = 0x62;
    vertex.v.cn[2] = 0x73;
    vertex.v.cn[3] = 0x84;

    Gfx commands[3]{};
    __gSPSegment(&commands[0], 1, &vertex);
    __gSPVertex(&commands[1], reinterpret_cast<Fast::F3DVtx*>(uintptr_t(0x01000001)), 1, 0);
    gSPEndDisplayList(&commands[2]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand segmentNoop = ReadCommand(translator, root, 0);
    const RtCommand vertexCommand = ReadCommand(translator, root, 1);
    Require((segmentNoop.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_SPNOOP),
            "segment setup should be consumed after fixing the segmented address");
    RtVertex packed{};
    const auto packedBytes = ReadBytes(translator, vertexCommand.w1, sizeof(packed));
    std::memcpy(&packed, packedBytes.data(), sizeof(packed));
    Require(packed.x == 0x2468 && packed.y == -0x1234 && packed.r == 0x51 && packed.a == 0x84,
            "tagged segmented vertex address did not resolve to its RT64 fields");

    Gfx untaggedCommands[2]{};
    __gSPVertex(&untaggedCommands[0], reinterpret_cast<Fast::F3DVtx*>(uintptr_t(0xDEAD0000)), 1, 0);
    gSPEndDisplayList(&untaggedCommands[1]);
    bool rejectedUntaggedAddress = false;
    try {
        Fast::DisplayListTranslator untaggedTranslator;
        (void)untaggedTranslator.Translate(untaggedCommands);
    } catch (const std::runtime_error&) {
        rejectedUntaggedAddress = true;
    }
    Require(rejectedUntaggedAddress, "unmapped untagged 32-bit pointer must not resolve as segmented RDRAM");
}

void TestTextureWordSwizzle() {
    const std::array<uint8_t, 8> texture = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87};
    Gfx commands[3]{};
    gDPSetTextureImage(&commands[0], G_IM_FMT_RGBA, G_IM_SIZ_8b, texture.size(), texture.data());
    gDPLoadBlock(&commands[1], G_TX_LOADTILE, 0, 0, texture.size() - 1, 0);
    gSPEndDisplayList(&commands[2]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand imageCommand = ReadCommand(translator, root, 0);
    Require((imageCommand.w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_SETTIMG),
            "texture image command did not use the RT64 RDP opcode");
    std::array<uint8_t, 8> expected{};
    for (size_t i = 0; i < texture.size(); ++i) {
        expected[i ^ 3u] = texture[i];
    }
    Require(ReadBytes(translator, imageCommand.w1, expected.size()) ==
                std::vector<uint8_t>(expected.begin(), expected.end()),
            "texture image bytes were not word-swizzled for RT64 RDRAM");
}

void TestNativeMatrixAndFloatReplacement() {
    MtxS matrix{};
    // guMtxF2L packs paired columns in each native u32. RT64 selects the
    // opposite halfword in each pair when exposing logical matrix columns.
    matrix.m[0][0] = 0x00010002; // logical integer columns 0,1 are 1,2
    matrix.m[2][0] = static_cast<int32_t>(0x80004000); // fraction columns 0,1 are .5,.25
    Gfx nativeCommands[2]{};
    gSPMatrix(&nativeCommands[0], &matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPEndDisplayList(&nativeCommands[1]);

    Fast::DisplayListTranslator nativeTranslator;
    const uint32_t nativeRoot = nativeTranslator.Translate(nativeCommands);
    const RtCommand nativeMatrixCommand = ReadCommand(nativeTranslator, nativeRoot, 0);
    const auto packedMatrix = ReadBytes(nativeTranslator, nativeMatrixCommand.w1, sizeof(MtxS));
    auto ReadPackedHalfword = [&](size_t segmentOffset, size_t row, size_t column) {
        uint16_t value = 0;
        const size_t offset = segmentOffset + (row * 4 + (column ^ 1u)) * sizeof(uint16_t);
        std::memcpy(&value, packedMatrix.data() + offset, sizeof(value));
        return value;
    };
    Require(ReadPackedHalfword(offsetof(MtxS, intPart), 0, 0) == 1 &&
                ReadPackedHalfword(offsetof(MtxS, intPart), 0, 1) == 2 &&
                ReadPackedHalfword(offsetof(MtxS, fracPart), 0, 0) == 0x8000 &&
                ReadPackedHalfword(offsetof(MtxS, fracPart), 0, 1) == 0x4000,
            "RT64 matrix columns did not decode guMtxF2L's paired fixed-point values");

    MtxS sourceMatrix{};
    MtxF replacement{};
    replacement.mf[0][0] = 1.5f;
    replacement.mf[1][2] = -2.25f;
    replacement.mf[2][0] = 0.00625f; // HUD orthographic X scale: fractional fixed-point unit.
    replacement.mf[2][1] = -0.00625f;
    replacement.mf[3][3] = 1.0f;
    Gfx replacementCommands[2]{};
    gSPMatrix(&replacementCommands[0], &sourceMatrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPEndDisplayList(&replacementCommands[1]);
    std::unordered_map<Mtx*, MtxF> replacements;
    replacements.emplace(&sourceMatrix, replacement);

    Fast::DisplayListTranslator replacementTranslator;
    const uint32_t replacementRoot = replacementTranslator.Translate(replacementCommands, replacements);
    const RtCommand replacementCommand = ReadCommand(replacementTranslator, replacementRoot, 0);
    MtxS fixedReplacement{};
    const auto bytes = ReadBytes(replacementTranslator, replacementCommand.w1, sizeof(fixedReplacement));
    std::memcpy(&fixedReplacement, bytes.data(), sizeof(fixedReplacement));
    Require(fixedReplacement.intPart[0][0 ^ 1] == 1 && fixedReplacement.fracPart[0][0 ^ 1] == 0x8000,
            "positive float matrix replacement did not convert to paired-column 16.16 fixed point");
    Require(fixedReplacement.intPart[1][3] == static_cast<uint16_t>(-3) &&
                fixedReplacement.fracPart[1][3] == 0xc000,
            "negative float matrix replacement did not convert to paired-column signed 16.16 fixed point");
    Require(fixedReplacement.intPart[2][1] == 0 && fixedReplacement.fracPart[2][1] == 409 &&
                fixedReplacement.intPart[2][0] == 0xffff && fixedReplacement.fracPart[2][0] == 0xfe67,
            "replacement matrices must truncate toward zero exactly like native guMtxF2L");
}

void TestViewportPacking() {
    Vp viewport{};
    viewport.vp.vscale[0] = 0x0102;
    viewport.vp.vscale[1] = 0x0304;
    viewport.vp.vscale[2] = 0x0506;
    viewport.vp.vscale[3] = 0x0708;
    viewport.vp.vtrans[0] = 0x1112;
    viewport.vp.vtrans[1] = 0x1314;
    viewport.vp.vtrans[2] = 0x1516;
    viewport.vp.vtrans[3] = 0x1718;

    Gfx commands[2]{};
    gSPViewport(&commands[0], &viewport);
    gSPEndDisplayList(&commands[1]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand viewportCommand = ReadCommand(translator, root, 0);
    const auto* source = reinterpret_cast<const uint8_t*>(&viewport);
    std::array<uint8_t, sizeof(Vp)> expected{};
    for (size_t i = 0; i < expected.size(); i += 4) {
        expected[i] = source[i + 2];
        expected[i + 1] = source[i + 3];
        expected[i + 2] = source[i];
        expected[i + 3] = source[i + 1];
    }
    Require(ReadBytes(translator, viewportCommand.w1, expected.size()) ==
                std::vector<uint8_t>(expected.begin(), expected.end()),
            "native viewport fields were not packed into RT64's expected order");
}

void TestNestedCallsAndBranches() {
    Gfx child[2]{};
    gSPNoOp(&child[0]);
    gSPEndDisplayList(&child[1]);

    Gfx commands[3]{};
    __gSPDisplayList(&commands[0], child);
    gSPBranchList(&commands[1], child);
    gSPEndDisplayList(&commands[2]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand call = ReadCommand(translator, root, 0);
    const RtCommand branch = ReadCommand(translator, root, 1);
    Require((call.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_DL) &&
                (branch.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_DL),
            "nested display-list commands did not lower to F3DEX2 G_DL");
    Require(((call.w0 >> 16) & 1) == G_DL_PUSH && ((branch.w0 >> 16) & 1) == G_DL_PUSH,
            "root-tail branch was not rewritten as a returning child call");
    Require(call.w1 == branch.w1, "repeated nested display-list references should share one lowered list");
    Require((ReadCommand(translator, root, 2).w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC) &&
                (ReadCommand(translator, root, 3).w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "root-tail branch did not append the synthetic full sync and end marker");
    Require((ReadCommand(translator, call.w1, 0).w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_SPNOOP),
            "nested display-list body was not lowered at the referenced address");
    Require((ReadCommand(translator, call.w1, 1).w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "nested display-list terminator was not preserved");

    Gfx leaf[1]{};
    gSPEndDisplayList(&leaf[0]);
    Gfx nestedBranch[2]{};
    gSPBranchList(&nestedBranch[0], leaf);
    nestedBranch[1].words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::OTR_G_LOAD_SHADER)) << 24;
    Gfx nestedRootCommands[2]{};
    __gSPDisplayList(&nestedRootCommands[0], nestedBranch);
    gSPEndDisplayList(&nestedRootCommands[1]);
    Fast::DisplayListTranslator nestedBranchTranslator;
    const uint32_t nestedRoot = nestedBranchTranslator.Translate(nestedRootCommands);
    const RtCommand nestedCall = ReadCommand(nestedBranchTranslator, nestedRoot, 0);
    const RtCommand nestedTailBranch = ReadCommand(nestedBranchTranslator, nestedCall.w1, 0);
    Require(((nestedTailBranch.w0 >> 16) & 1) == G_DL_NOPUSH &&
                (nestedTailBranch.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_DL),
            "nested child tail branch did not preserve native G_DL_NOPUSH control");
    Require((ReadCommand(nestedBranchTranslator, nestedTailBranch.w1, 0).w0 >> 24) ==
                static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "nested branch target was not lowered before its child tail");
}

void TestBranchStopsCurrentList() {
    Gfx child[1]{};
    gSPEndDisplayList(&child[0]);
    Gfx commands[2]{};
    gSPBranchList(&commands[0], child);
    commands[1].words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::OTR_G_LOAD_SHADER)) << 24;

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand branch = ReadCommand(translator, root, 0);
    Require((branch.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_DL) &&
                ((branch.w0 >> 16) & 1) == G_DL_PUSH,
            "root-tail G_DL_NOPUSH branch was not rewritten as a returning call");
    Require((ReadCommand(translator, branch.w1, 0).w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "branch target was not lowered before terminating the current list");
    Require((ReadCommand(translator, root, 1).w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC) &&
                (ReadCommand(translator, root, 2).w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "root-tail branch did not terminate at a synthetic full sync and end marker");
}

void TestWideRectangleLowering() {
    Gfx wideTextureCommands[4]{};
    Gfx* wideTextureCursor = wideTextureCommands;
    gSPWideTextureRectangle(wideTextureCursor++, -5000, 20, 40000, 96, 3, 0x1112, 0x1314, 0x1516, 0x1718);
    gSPEndDisplayList(wideTextureCursor);

    Fast::DisplayListTranslator textureTranslator;
    const uint32_t textureRoot = textureTranslator.Translate(wideTextureCommands);
    const RtCommand textureCommand = ReadCommand(textureTranslator, textureRoot, 0);
    Require((textureCommand.w0 >> 24) == Fast::DisplayListTranslator::NativeWideRectangleOpcode &&
                textureCommand.w1 == 0,
            "wide texture rectangle did not use its native-coordinate sidecar command");
    const auto& textureRectangles = textureTranslator.GetNativeWideRectangles();
    Require(textureRectangles.size() == 1 && !textureRectangles[0].fill &&
                textureRectangles[0].ulx == -5000 && textureRectangles[0].uly == 20 &&
                textureRectangles[0].lrx == 40000 && textureRectangles[0].lry == 96 &&
                textureRectangles[0].tile == 3 && textureRectangles[0].uls == 0x1112 &&
                textureRectangles[0].ult == 0x1314 && textureRectangles[0].dsdx == 0x1516 &&
                textureRectangles[0].dtdy == 0x1718,
            "wide texture rectangle sidecar lost signed coordinates or texture parameters");
    Require((ReadCommand(textureTranslator, textureRoot, 1).w0 >> 24) ==
                static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC),
            "wide texture rectangle payload was not consumed before display-list termination");

    Gfx wideFillCommands[3]{};
    Gfx* wideFillCursor = wideFillCommands;
    gDPFillWideRectangle(wideFillCursor++, 1200, 4, 2000, 30);
    gSPEndDisplayList(wideFillCursor);

    Fast::DisplayListTranslator fillTranslator;
    const uint32_t fillRoot = fillTranslator.Translate(wideFillCommands);
    const RtCommand actualFill = ReadCommand(fillTranslator, fillRoot, 0);
    Require((actualFill.w0 >> 24) == Fast::DisplayListTranslator::NativeWideRectangleOpcode &&
                actualFill.w1 == 0,
            "wide fill rectangle did not use its native-coordinate sidecar command");
    const auto& fillRectangles = fillTranslator.GetNativeWideRectangles();
    Require(fillRectangles.size() == 1 && fillRectangles[0].fill &&
                fillRectangles[0].ulx == 1200 * 4 && fillRectangles[0].uly == 4 * 4 &&
                fillRectangles[0].lrx == 2000 * 4 && fillRectangles[0].lry == 30 * 4,
            "wide fill rectangle sidecar lost coordinates outside the standard RDP range");
    Require((ReadCommand(fillTranslator, fillRoot, 1).w0 >> 24) ==
                static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC) &&
                (ReadCommand(fillTranslator, fillRoot, 2).w0 >> 24) ==
                    static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "wide fill payload was not consumed before display-list termination");
}

void TestLoadBlockDxtUsesRoundedRenderLine() {
    constexpr uint32_t width = 46;
    constexpr uint32_t height = 16;
    Gfx commands[8]{};
    gDPSetTextureImage(&commands[0], G_IM_FMT_IA, G_IM_SIZ_8b_LOAD_BLOCK, 1,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000)));
    gDPSetTile(&commands[1], G_IM_FMT_IA, G_IM_SIZ_8b_LOAD_BLOCK, 0, 0, G_TX_LOADTILE, 0,
               G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(&commands[2]);
    gDPLoadBlock(&commands[3], G_TX_LOADTILE, 0, 0,
                 (((width * height) + G_IM_SIZ_8b_INCR) >> G_IM_SIZ_8b_SHIFT) - 1,
                 CALC_DXT(width, G_IM_SIZ_8b_BYTES));
    gDPPipeSync(&commands[4]);
    gDPSetTile(&commands[5], G_IM_FMT_IA, G_IM_SIZ_8b,
               ((width * G_IM_SIZ_8b_LINE_BYTES) + 7) >> 3, 0, G_TX_RENDERTILE, 0,
               G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
    gDPSetTileSize(&commands[6], G_TX_RENDERTILE, 0, 0, (width - 1) << G_TEXTURE_IMAGE_FRAC,
                   (height - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPEndDisplayList(&commands[7]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand loadBlock = ReadCommand(translator, root, 3);
    const uint32_t originalDxt = CALC_DXT(width, G_IM_SIZ_8b_BYTES);
    const uint32_t roundedLineWords = ((width * G_IM_SIZ_8b_LINE_BYTES) + 7) >> 3;
    const uint32_t expectedDxt = ((1u << G_TX_DXT_FRAC) + roundedLineWords - 1) / roundedLineWords;
    Require((loadBlock.w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_LOADBLOCK) &&
                (loadBlock.w1 & 0xfffu) == expectedDxt && originalDxt != expectedDxt,
            "standard texture-block DXT did not match the rounded render-tile line");

    Gfx manualCommands[3]{};
    gDPLoadBlock(&manualCommands[0], G_TX_LOADTILE, 0, 0, 367, originalDxt);
    gSPEndDisplayList(&manualCommands[1]);
    Fast::DisplayListTranslator manualTranslator;
    const uint32_t manualRoot = manualTranslator.Translate(manualCommands);
    const RtCommand manualLoadBlock = ReadCommand(manualTranslator, manualRoot, 0);
    Require((manualLoadBlock.w1 & 0xfffu) == originalDxt,
            "manual LOADBLOCK without a render-tile pattern had its DXT modified");
}

void TestOtrTriangleLowering() {
    Gfx commands[] = {gsSP1TriangleOTR(3, 5, 7, 0), gsSPEndDisplayList()};
    const uint32_t encodedArgs = static_cast<uint32_t>(commands[0].words.w0) & 0x00ffffffu;

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand triangle = ReadCommand(translator, root, 0);
    Require((triangle.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_TRI1),
            "OTR triangle did not become an F3DEX2 G_TRI1 command");
    Require((triangle.w0 & 0x00ffffffu) == encodedArgs && triangle.w1 == 0,
            "OTR triangle's encoded vertex indices were not preserved in F3DEX2 slots");

    Gfx xmlCommands[2]{};
    xmlCommands[0].words.w0 = (static_cast<uint32_t>(static_cast<uint8_t>(Fast::OTR_G_TRI1_OTR)) << 24) | 3u;
    xmlCommands[0].words.w1 = (5u << 16) | 7u;
    gSPEndDisplayList(&xmlCommands[1]);
    Fast::DisplayListTranslator xmlTranslator;
    const uint32_t xmlRoot = xmlTranslator.Translate(xmlCommands);
    const RtCommand xmlTriangle = ReadCommand(xmlTranslator, xmlRoot, 0);
    Gfx standardTriangle = gsSP1Triangle(3, 5, 7, 0);
    const uint32_t expectedXmlW0 = static_cast<uint32_t>(standardTriangle.words.w0);
    Require(xmlTriangle.w0 == expectedXmlW0 && xmlTriangle.w1 == 0,
            "XML-exported OTR triangle indices were not expanded into F3DEX2 vertex slots");
}

void TestCompatibilityMetadata() {
    Gfx commands[3]{};
    commands[0].words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::OTR_G_PUSHCD)) << 24;
    commands[1].words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX2_G_MOVEMEM)) << 24;
    gSPEndDisplayList(&commands[2]);
    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    for (size_t index = 0; index < 2; ++index) {
        const auto packet = ReadCommand(translator, root, index);
        Require(packet.w0 == (static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX2_G_SPNOOP)) << 24) &&
                    packet.w1 == 0,
                "directory metadata or empty move-memory packet was passed to RT64");
    }
}

void TestFramebufferRegistryAndImageRectStrips() {
    Fast::DisplayListTranslator::DefineFramebuffer(3, 2048, 4);
    Fast::DisplayListTranslator::DefineFramebuffer(4, 320, 240);
    const auto registered = Fast::DisplayListTranslator::GetFramebuffer(3);
    Require(registered && registered->id == 3 && registered->width == 2048 && registered->height == 4 &&
                registered->address == 0x51000,
            "native framebuffer registry returned incorrect dimensions or RDRAM slot");
    Require(!Fast::DisplayListTranslator::GetFramebuffer(6), "unregistered native framebuffer should not resolve");

    bool rejectedInvalidId = false;
    try {
        Fast::DisplayListTranslator::DefineFramebuffer(6, 320, 240);
    } catch (const std::invalid_argument&) {
        rejectedInvalidId = true;
    }
    Require(rejectedInvalidId, "native framebuffer registry accepted an out-of-range framebuffer ID");

    Gfx imageRectCommands[5]{};
    Gfx* cursor = imageRectCommands;
    gDPSetTextureImageFB(cursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2048,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(3)));
    gDPImageRectangle(cursor++, 0, 0, 0, 0, 2048, 4, 2048, 4, 0, 2048, 4);
    gSPEndDisplayList(cursor);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(imageRectCommands);
    const RtCommand bindTexture = ReadCommand(translator, root, 0);
    Require((bindTexture.w0 >> 24) == Fast::DisplayListTranslator::NativeFramebufferOperationOpcode &&
                bindTexture.w1 == 0,
            "G_SETTIMG_FB did not emit a framebuffer-texture bind operation");
    const auto& operations = translator.GetFramebufferOperations();
    Require(operations.size() == 1 && operations[0].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Flush &&
                operations[0].sourceId == 3,
            "G_SETTIMG_FB before G_IMAGERECT did not flush the source framebuffer before its strip loads");

    // The image exceeds one 4 KiB TMEM load both vertically and horizontally.
    // Each 1024-pixel tile spans two source rows, producing four source offsets.
    const std::array<uint32_t, 4> expectedAddresses = {
        registered->address,
        registered->address + 2048,
        registered->address + 8192,
        registered->address + 10240,
    };
    for (size_t strip = 0; strip < expectedAddresses.size(); ++strip) {
        const size_t first = 1 + strip * 8;
        const RtCommand setImage = ReadCommand(translator, root, first);
        const RtCommand loadTile = ReadCommand(translator, root, first + 2);
        Require((setImage.w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_SETTIMG) &&
                    setImage.w1 == expectedAddresses[strip],
                "G_IMAGERECT strip did not point at its corresponding framebuffer source offset");
        Require((loadTile.w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_LOADTILE),
                "G_IMAGERECT strip did not emit a matching texture-memory load");
    }

    Gfx negativeImageRectCommands[5]{};
    Fast::DisplayListTranslator::DefineFramebuffer(1, 320, 240);
    Gfx* negativeCursor = negativeImageRectCommands;
    gDPSetTextureImageFB(negativeCursor++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
    gDPImageRectangle(negativeCursor++, -16 * 4, 0, 0, 0, 320 * 4, 240 * 4, 320, 240,
                      G_TX_RENDERTILE, 320, 240);
    gSPEndDisplayList(negativeCursor);
    Fast::DisplayListTranslator negativeTranslator;
    const uint32_t negativeRoot = negativeTranslator.Translate(negativeImageRectCommands);
    const auto& negativeRectangles = negativeTranslator.GetNativeWideRectangles();
    Require(!negativeRectangles.empty() && negativeRectangles[0].ulx == -64 &&
                negativeRectangles[0].uly == 0 && negativeRectangles[0].lrx == 1280 &&
                negativeRectangles[0].lry == 960 && negativeRectangles[0].tile == G_TX_RENDERTILE,
            "negative widescreen G_IMAGERECT destination was not preserved in the native-wide sidecar");
    const RtCommand firstWideImageRect = ReadCommand(negativeTranslator, negativeRoot, 4);
    Require((firstWideImageRect.w0 >> 24) == Fast::DisplayListTranslator::NativeWideRectangleOpcode &&
                firstWideImageRect.w1 == 0,
            "out-of-range G_IMAGERECT strip did not select native-wide rectangle lowering");

    Gfx directBindCommands[3]{};
    gDPSetTextureImageFB(&directBindCommands[0], G_IM_FMT_RGBA, G_IM_SIZ_16b, 64,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(3)));
    gSP1Triangle(&directBindCommands[1], 0, 1, 2, 0);
    gSPEndDisplayList(&directBindCommands[2]);
    translator.Translate(directBindCommands);
    const auto& bindOperations = translator.GetFramebufferOperations();
    Require(bindOperations.size() == 1 &&
                bindOperations[0].type == Fast::DisplayListTranslator::FramebufferOperation::Type::BindTexture &&
                bindOperations[0].sourceId == 3 && bindOperations[0].width == 64 &&
                bindOperations[0].sizeCode == G_IM_SIZ_16b && bindOperations[0].tile == G_TX_RENDERTILE,
            "direct G_SETTIMG_FB bind did not preserve its framebuffer and tile metadata");

    Gfx resetCommands[3]{};
    gsSPSetFB(&resetCommands[0], 3);
    gsSPResetFB(&resetCommands[1]);
    gSPEndDisplayList(&resetCommands[2]);
    Fast::DisplayListTranslator resetTranslator;
    const uint32_t resetRoot = resetTranslator.Translate(resetCommands);
    const RtCommand resetFlush = ReadCommand(resetTranslator, resetRoot, 3);
    Require((resetFlush.w0 >> 24) == Fast::DisplayListTranslator::NativeFramebufferOperationOpcode &&
                resetFlush.w1 == 1,
            "RESETFB did not flush the active framebuffer before restoring the main target");
    const auto& resetOperations = resetTranslator.GetFramebufferOperations();
    Require(resetOperations.size() == 2 &&
                resetOperations[0].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Invalidate &&
                resetOperations[0].sourceId == 3 &&
                resetOperations[1].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Flush &&
                resetOperations[1].sourceId == 3,
            "RESETFB flush did not identify the active framebuffer");

    std::array<uint16_t, 4> readback{};
    Gfx operationCommands[5]{};
    Gfx* operationCursor = operationCommands;
    gDPCopyFB(operationCursor++, 4, 3, false, nullptr);
    gDPReadFB(operationCursor++, 4, readback.data(), 7, 9, 2, 2, false);
    gSPEndDisplayList(operationCursor);
    const uint32_t operationRoot = translator.Translate(operationCommands);
    const RtCommand copy = ReadCommand(translator, operationRoot, 0);
    const RtCommand read = ReadCommand(translator, operationRoot, 1);
    Require((copy.w0 >> 24) == Fast::DisplayListTranslator::NativeFramebufferOperationOpcode &&
                (read.w0 >> 24) == Fast::DisplayListTranslator::NativeFramebufferOperationOpcode,
            "native framebuffer copy/read did not lower to the RT64 operation opcode");
    const auto& copyReadOperations = translator.GetFramebufferOperations();
    Require(copyReadOperations.size() == 2 && copyReadOperations[0].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Copy &&
                copyReadOperations[0].sourceId == 3 && copyReadOperations[0].destinationId == 4,
            "native framebuffer copy operation metadata was not preserved");
    Require(copyReadOperations[1].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Read &&
                copyReadOperations[1].sourceId == 4 && copyReadOperations[1].destination == reinterpret_cast<uintptr_t>(readback.data()) &&
                copyReadOperations[1].x == 7 && copyReadOperations[1].y == 9 && copyReadOperations[1].width == 2 &&
                copyReadOperations[1].height == 2,
            "native framebuffer read operation metadata was not preserved");
}

void TestFramebufferDepthClearInNestedList() {
    constexpr uintptr_t nativeDepthImage = 0x24000;
    constexpr uintptr_t nativeColorImage = 0x26000;
    Gfx child[5]{};
    gDPSetColorImage(&child[0], G_IM_FMT_RGBA, G_IM_SIZ_16b, 64,
                     reinterpret_cast<void*>(nativeDepthImage));
    gDPFillRectangle(&child[1], 0, 0, 63, 31);
    gDPSetColorImage(&child[2], G_IM_FMT_RGBA, G_IM_SIZ_16b, 64,
                     reinterpret_cast<void*>(nativeColorImage));
    gDPSetDepthImage(&child[3], reinterpret_cast<void*>(nativeDepthImage));
    gSPEndDisplayList(&child[4]);

    Gfx commands[4]{};
    gsSPSetFB(&commands[0], 3);
    __gSPDisplayList(&commands[1], child);
    gsSPResetFB(&commands[2]);
    gSPEndDisplayList(&commands[3]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand invalidate = ReadCommand(translator, root, 0);
    Require((invalidate.w0 >> 24) == Fast::DisplayListTranslator::NativeFramebufferOperationOpcode &&
                invalidate.w1 == 0,
            "SETFB did not invalidate the source framebuffer readiness marker before rendering");
    const auto& framebufferOperations = translator.GetFramebufferOperations();
    Require(!framebufferOperations.empty() &&
                framebufferOperations[0].type == Fast::DisplayListTranslator::FramebufferOperation::Type::Invalidate &&
                framebufferOperations[0].sourceId == 3,
            "SETFB invalidate operation did not identify the selected framebuffer");
    const RtCommand call = ReadCommand(translator, root, 3);
    Require((call.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_DL),
            "SETFB parent did not preserve its nested display-list call");
    const RtCommand depthClear = ReadCommand(translator, call.w1, 0);
    const RtCommand colorTarget = ReadCommand(translator, call.w1, 2);
    Require(depthClear.w1 == Fast::DisplayListTranslator::DepthAddress,
            "nested depth CIMG matching its later ZIMG was not redirected to synthetic depth");
    const auto framebuffer = Fast::DisplayListTranslator::GetFramebuffer(3);
    Require(framebuffer && colorTarget.w1 == framebuffer->address,
            "unrelated nested color CIMG was incorrectly redirected to synthetic depth");
}

void TestInterpolatedTileSizeSelectionAndQuantization() {
    Gfx commands[5]{};
    Gfx* cursor = commands;
    gDPSetInterpolation(cursor++, 2);
    cursor->words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::RDP_G_SETTILESIZE_INTERP)) << 24;
    cursor->words.w1 = 5u << 24;
    ++cursor;
    const float uls = 1.25f;
    const float ult = 2.0f;
    const float lrs = 30.25f;
    const float lrt = 44.5f;
    cursor->words.w0 = 0;
    std::memcpy(&cursor->words.w0, &uls, sizeof(uls));
    std::memcpy(&cursor->words.w1, &ult, sizeof(ult));
    ++cursor;
    std::memcpy(&cursor->words.w0, &lrs, sizeof(lrs));
    std::memcpy(&cursor->words.w1, &lrt, sizeof(lrt));
    ++cursor;
    gSPEndDisplayList(cursor);

    Fast::DisplayListTranslator selected;
    const uint32_t selectedRoot = selected.Translate(commands, {}, 2);
    const RtCommand tileSize = ReadCommand(selected, selectedRoot, 1);
    Require((tileSize.w0 >> 24) == static_cast<uint8_t>(Fast::RDP_G_SETTILESIZE),
            "selected interpolated tile size was not emitted as a standard SETTILESIZE");
    Require(((tileSize.w0 >> 12) & 0xfff) == 5 && (tileSize.w0 & 0xfff) == 8 &&
                (tileSize.w1 >> 24) == 5 && ((tileSize.w1 >> 12) & 0xfff) == 121 &&
                (tileSize.w1 & 0xfff) == 178,
            "interpolated tile coordinates were not rounded to N64 10.2 units");
    Require((ReadCommand(selected, selectedRoot, 2).w0 >> 24) ==
                static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC),
            "top-level interpolated list did not receive its synthetic full sync");
    const RtCommand end = ReadCommand(selected, selectedRoot, 3);
    Require((end.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "interpolation payload was not consumed as two data commands");

    Fast::DisplayListTranslator unselected;
    const uint32_t unselectedRoot = unselected.Translate(commands, {}, 1);
    Require((ReadCommand(unselected, unselectedRoot, 1).w0 >> 24) ==
                static_cast<uint8_t>(Fast::RDP_G_RDPFULLSYNC),
            "top-level unselected interpolated list did not receive its synthetic full sync");
    const RtCommand consumedEnd = ReadCommand(unselected, unselectedRoot, 2);
    Require((consumedEnd.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX2_G_ENDDL),
            "unselected interpolated tile size should consume its payload without emission");
}

void TestInterpolatedSegments() {
    Fast::F3DVtx fallback{};
    Fast::F3DVtx targetZero{};
    Fast::F3DVtx targetOne{};
    fallback.v.ob[0] = 0x1111;
    targetZero.v.ob[0] = 0x2222;
    targetOne.v.ob[0] = 0x3333;

    Gfx commands[7]{};
    __gSPSegment(&commands[0], 1, &fallback);
    __gSPSegment(&commands[1], 2, &fallback);
    __gSPSegmentInterp(&commands[2], 1, &targetZero); // target 0, segment 1
    __gSPVertex(&commands[3], reinterpret_cast<Fast::F3DVtx*>(uintptr_t(0x01000001)), 1, 0);
    __gSPSegmentInterp(&commands[4], 18, &targetOne); // target 1, segment 2
    __gSPVertex(&commands[5], reinterpret_cast<Fast::F3DVtx*>(uintptr_t(0x02000001)), 1, 0);
    gSPEndDisplayList(&commands[6]);

    const auto decodedX = [](const Fast::DisplayListTranslator& translator, uint32_t root, size_t commandIndex) {
        const RtCommand vertexCommand = ReadCommand(translator, root, commandIndex);
        RtVertex packed{};
        const auto bytes = ReadBytes(translator, vertexCommand.w1, sizeof(packed));
        std::memcpy(&packed, bytes.data(), sizeof(packed));
        return packed.x;
    };

    Fast::DisplayListTranslator targetZeroTranslator;
    const uint32_t targetZeroRoot = targetZeroTranslator.Translate(commands, {}, 0);
    Require(decodedX(targetZeroTranslator, targetZeroRoot, 3) == 0x2222 &&
                decodedX(targetZeroTranslator, targetZeroRoot, 5) == 0x1111,
            "interpolation target zero did not select its segment base while preserving the other base");

    Fast::DisplayListTranslator targetOneTranslator;
    const uint32_t targetOneRoot = targetOneTranslator.Translate(commands, {}, 1);
    Require(decodedX(targetOneTranslator, targetOneRoot, 3) == 0x1111 &&
                decodedX(targetOneTranslator, targetOneRoot, 5) == 0x3333,
            "interpolation target one did not select its segment base while preserving the other base");
}

void TestTexturePatchUsesResourceDimensions() {
    constexpr char path[] = "__OTR__tests/rt64/alias_width_rgba16";
    const std::array<uint8_t, 4> source = {0x12, 0x34, 0x56, 0x78};
    const std::array<uint8_t, 2> mask = {0, 1};
    const std::array<uint8_t, 4> replacement = {0x9a, 0xbc, 0xde, 0xf0};
    Fast::Rt64TexturePatches::Register(path, mask.data(), replacement.data());

    auto texture = std::make_shared<Fast::Texture>();
    texture->Type = Fast::TextureType::RGBA16bpp;
    texture->Width = 2;
    texture->Height = 1;
    texture->ImageDataSize = source.size();
    texture->ImageData = new uint8_t[source.size()];
    std::memcpy(texture->ImageData, source.data(), source.size());

    Fast::DisplayListTranslator translator;
    const uint32_t address = Fast::DisplayListTranslatorTestAccess::CopyTextureResource(
        translator, texture, 2, 1, 16, G_IM_FMT_RGBA, G_IM_SIZ_16b, path);
    Fast::Rt64TexturePatches::Unregister(path);

    const auto& rdram = translator.GetRdram();
    const std::array<uint8_t, 4> expectedSwizzled = {0xf0, 0xde, 0x34, 0x12};
    Require(address + expectedSwizzled.size() <= rdram.size() &&
                std::memcmp(rdram.data() + address, expectedSwizzled.data(), expectedSwizzled.size()) == 0,
            "resource dimensions and patch replacement were not preserved for a width-one G_SETTIMG alias");
}

void TestCanonicalRgba32NativeTextureRegistration() {
    constexpr uint16_t width = 160;
    constexpr uint16_t height = 160;
    constexpr size_t rgbaBytes = static_cast<size_t>(width) * height * 4;
    auto texture = std::make_shared<Fast::Texture>();
    texture->Type = Fast::TextureType::RGBA32bpp;
    texture->Width = width;
    texture->Height = height;
    texture->Flags = 0;
    texture->ImageDataSize = rgbaBytes;
    texture->ImageData = new uint8_t[rgbaBytes];
    for (size_t i = 0; i < rgbaBytes; ++i) {
        texture->ImageData[i] = static_cast<uint8_t>(i * 17u);
    }

    Fast::DisplayListTranslator translator;
    Fast::DisplayListTranslatorTestAccess::CopyTextureResource(
        translator, texture, rgbaBytes, 1, 32, G_IM_FMT_RGBA, G_IM_SIZ_32b, nullptr);
    Require(translator.GetNativeTextures().empty(),
            "width-one block-load aliases must keep the normal TMEM path");
    const uint32_t placeholder = Fast::DisplayListTranslatorTestAccess::CopyTextureResource(
        translator, texture, 4096, width, 32, G_IM_FMT_RGBA, G_IM_SIZ_32b, nullptr);
    const auto& nativeTextures = translator.GetNativeTextures();
    Require(nativeTextures.size() == 1 && nativeTextures[0].address == placeholder &&
                nativeTextures[0].rgba == texture->ImageData && nativeTextures[0].byteCount == rgbaBytes &&
                nativeTextures[0].width == width && nativeTextures[0].height == height &&
                nativeTextures[0].nativeWidth == width && nativeTextures[0].nativeHeight == height,
            "unflagged canonical RGBA32 image did not register its full native texture dimensions");
    Require(Fast::DisplayListTranslatorTestAccess::CopyTextureResource(
                translator, texture, 4096, width, 32, G_IM_FMT_RGBA, G_IM_SIZ_32b, nullptr) == placeholder &&
                translator.GetNativeTextures().size() == 1,
            "repeated whole-image loads must reuse the registered native texture");
}

void TestLowAddressNativeStaticVertex() {
    const uintptr_t nativeAddress = reinterpret_cast<uintptr_t>(&nativeStaticVertex);
    Require(nativeAddress <= UINT32_MAX, "translator test static vertex must live below 4 GiB (non-PIE target)");
    nativeStaticVertex = {};
    nativeStaticVertex.v.ob[0] = 0x1357;
    nativeStaticVertex.v.ob[1] = -0x2468;
    nativeStaticVertex.v.cn[0] = 0x91;
    nativeStaticVertex.v.cn[3] = 0xA4;

    Gfx commands[2]{};
    __gSPVertex(&commands[0], &nativeStaticVertex, 1, 0);
    gSPEndDisplayList(&commands[1]);

    Fast::DisplayListTranslator translator;
    const uint32_t root = translator.Translate(commands);
    const RtCommand vertexCommand = ReadCommand(translator, root, 0);
    RtVertex packed{};
    const auto packedBytes = ReadBytes(translator, vertexCommand.w1, sizeof(packed));
    std::memcpy(&packed, packedBytes.data(), sizeof(packed));
    Require(packed.x == 0x1357 && packed.y == -0x2468 && packed.r == 0x91 && packed.a == 0xA4,
            "low-address native static vertex pointer was treated as segmented RDRAM");
}
} // namespace

int main() {
    try {
        const auto run = [](const char* name, auto test) {
            std::cerr << "Running " << name << '\n';
            test();
        };
        run("native vertex packing", TestNativeVertexPacking);
        run("triangle UV preservation across other modes", TestTriangleUvPreservationAcrossOtherModes);
        run("point-light packing", TestPointLightPacking);
        run("directional light packing", TestDirectionalLightPacking);
        run("texture LUT partial other mode", TestTextureLutPartialOtherModeNormalization);
        run("low-address native static vertex", TestLowAddressNativeStaticVertex);
        run("segmented vertex address", TestSegmentedVertexAddress);
        run("texture word swizzle", TestTextureWordSwizzle);
        run("native and replacement matrices", TestNativeMatrixAndFloatReplacement);
        run("viewport packing", TestViewportPacking);
        run("nested calls and branches", TestNestedCallsAndBranches);
        run("branch terminates current list", TestBranchStopsCurrentList);
        run("wide rectangles", TestWideRectangleLowering);
        run("load-block DXT uses rounded render line", TestLoadBlockDxtUsesRoundedRenderLine);
        run("OTR triangle", TestOtrTriangleLowering);
        run("compatibility metadata", TestCompatibilityMetadata);
        run("framebuffer registry and image rectangles", TestFramebufferRegistryAndImageRectStrips);
        run("nested framebuffer depth clear", TestFramebufferDepthClearInNestedList);
        run("interpolated tile size", TestInterpolatedTileSizeSelectionAndQuantization);
        run("interpolated segments", TestInterpolatedSegments);
        run("resource dimensions in texture patch", TestTexturePatchUsesResourceDimensions);
        run("canonical RGBA32 native texture registration", TestCanonicalRgba32NativeTextureRegistration);
    } catch (const std::exception& exception) {
        std::cerr << "RT64 translator test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "RT64 translator tests passed\n";
    return 0;
}
