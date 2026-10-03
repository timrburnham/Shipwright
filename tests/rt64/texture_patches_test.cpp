#include "fast/rt64/TexturePatches.h"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestRgba16MaskAndReplacement() {
    constexpr char path[] = "__OTR__/textures/window.rgba16";
    const std::array<uint8_t, 2> mask = {1, 0};
    const std::array<uint8_t, 4> original = {0xf8, 0x01, 0x07, 0xc1};

    Fast::Rt64TexturePatches::Register(path, mask.data(), nullptr);
    auto transparent = Fast::Rt64TexturePatches::Apply(path, original.data(), original.size(), 0, 2, 2, 1);
    Require(transparent.has_value(), "RGBA16 mask patch was not applied");
    Require((*transparent)[0] == 0 && (*transparent)[1] == 0 &&
                (*transparent)[2] == original[2] && (*transparent)[3] == original[3],
            "RGBA5551 mask did not clear the selected texel and preserve unmasked data");

    const std::array<uint8_t, 4> replacement = {0x00, 0x3f, 0xff, 0xff};
    Fast::Rt64TexturePatches::Register(path, mask.data(), replacement.data());
    auto replaced = Fast::Rt64TexturePatches::Apply(path, original.data(), original.size(), 0, 2, 2, 1);
    Require(replaced.has_value() && (*replaced)[0] == replacement[0] && (*replaced)[1] == replacement[1] &&
                (*replaced)[2] == original[2] && (*replaced)[3] == original[3],
            "RGBA16 replacement did not preserve unmasked pixels");
    Fast::Rt64TexturePatches::Unregister(path);
    Require(!Fast::Rt64TexturePatches::Apply(path, original.data(), original.size(), 0, 2, 2, 1),
            "unregistered patch remained active");
}

void TestRgba32AndIaAlpha() {
    constexpr char rgbaPath[] = "textures/logo.rgba32";
    const std::array<uint8_t, 1> mask = {1};
    const std::array<uint8_t, 2> rgbaMask = {1, 0};
    const std::array<uint8_t, 8> rgba = {10, 20, 30, 255, 40, 50, 60, 255};
    Fast::Rt64TexturePatches::Register(rgbaPath, rgbaMask.data(), nullptr);
    auto rgbaResult = Fast::Rt64TexturePatches::Apply(rgbaPath, rgba.data(), rgba.size(), 0, 3, 2, 1);
    Require(rgbaResult.has_value() && (*rgbaResult)[0] == 0 && (*rgbaResult)[3] == 0 &&
                (*rgbaResult)[4] == 40 && (*rgbaResult)[5] == 50 && (*rgbaResult)[6] == 60 &&
                (*rgbaResult)[7] == 255,
            "RGBA32 mask did not clear the selected texel and preserve unmasked pixels");

    constexpr char iaPath[] = "textures/ia4";
    const std::array<uint8_t, 2> iaMask = {1, 0,};
    const std::array<uint8_t, 1> ia4 = {0xfb};
    Fast::Rt64TexturePatches::Register(iaPath, iaMask.data(), nullptr);
    auto iaResult = Fast::Rt64TexturePatches::Apply(iaPath, ia4.data(), ia4.size(), 3, 0, 2, 1);
    Require(iaResult.has_value() && (*iaResult)[0] == 0x0b,
            "IA4 mask did not clear the selected texel and preserve unmasked data");

    constexpr char ia8Path[] = "textures/ia8";
    const std::array<uint8_t, 2> ia8 = {0xab, 0xcd};
    Fast::Rt64TexturePatches::Register(ia8Path, rgbaMask.data(), nullptr);
    auto ia8Result = Fast::Rt64TexturePatches::Apply(ia8Path, ia8.data(), ia8.size(), 3, 1, 2, 1);
    Require(ia8Result.has_value() && (*ia8Result)[0] == 0 && (*ia8Result)[1] == 0xcd,
            "IA8 mask did not clear the selected texel and preserve unmasked data");

    constexpr char ia16Path[] = "textures/ia16";
    const std::array<uint8_t, 4> ia16 = {0x11, 0xff, 0x22, 0xee};
    Fast::Rt64TexturePatches::Register(ia16Path, rgbaMask.data(), nullptr);
    auto ia16Result = Fast::Rt64TexturePatches::Apply(ia16Path, ia16.data(), ia16.size(), 3, 2, 2, 1);
    Require(ia16Result.has_value() && (*ia16Result)[0] == 0 && (*ia16Result)[1] == 0 &&
                (*ia16Result)[2] == 0x22 && (*ia16Result)[3] == 0xee,
            "IA16 mask did not clear the selected texel and preserve unmasked data");
}

void TestReplacementForNoAlphaFormatAndBounds() {
    constexpr char path[] = "textures/intensity";
    const std::array<uint8_t, 2> mask = {0, 1};
    const std::array<uint8_t, 2> original = {0x12, 0x34};
    const std::array<uint8_t, 2> replacement = {0xab, 0xcd};
    Fast::Rt64TexturePatches::Register(path, mask.data(), nullptr);
    Require(!Fast::Rt64TexturePatches::Apply(path, original.data(), original.size(), 4, 1, 2, 1),
            "intensity-only mask silently claimed transparency support");

    Fast::Rt64TexturePatches::Register(path, mask.data(), replacement.data());
    auto result = Fast::Rt64TexturePatches::Apply(path, original.data(), original.size(), 4, 1, 2, 1);
    Require(result.has_value() && (*result)[0] == original[0] && (*result)[1] == replacement[1],
            "packed replacement was rejected for an intensity-only texture");
    Require(!Fast::Rt64TexturePatches::Apply(path, original.data(), 1, 4, 1, 2, 1),
            "truncated source texture was accepted");
    Fast::Rt64TexturePatches::Unregister(path);
}
} // namespace

int main() {
    try {
        TestRgba16MaskAndReplacement();
        TestRgba32AndIaAlpha();
        TestReplacementForNoAlphaFormatAndBounds();
        std::cout << "RT64 texture patch tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
