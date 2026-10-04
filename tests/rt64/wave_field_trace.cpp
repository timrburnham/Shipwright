#include "../../soh/soh/Enhancements/Graphics/LakeWater/WaveField.h"
#include "../../soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaterMath.h"
#include "../../soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaveSource.h"

#include <cstdlib>
#include <fstream>
#include <cstring>
#include <iterator>
#include <vector>

static int TraceSource(int argc, char** argv) {
    if (argc != 7) return 2;
    std::ifstream input(argv[3], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.is_open() || input.bad() || bytes.empty() || (bytes.size() % 64) != 0) return 2;
    std::vector<LakeWater::CartridgeWaveSource> sources(bytes.size() / 64);
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!LakeWater::CartridgeWaveSource::Decode(bytes.data() + i * 64, 64, sources[i])) return 2;
    }
    LakeWater::WaveField field;
    field.SetWindowCenter(std::strtof(argv[4], nullptr), std::strtof(argv[5], nullptr));
    std::ofstream out(argv[2], std::ios::binary);
    std::ofstream sineOut(std::string(argv[2]) + ".sine", std::ios::binary);
    for (float value : LakeWater::CartridgeSineTable()) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        const char encoded[] = {char(bits >> 24), char(bits >> 16), char(bits >> 8), char(bits)};
        sineOut.write(encoded, 4);
    }
    for (int tick = 0; tick < std::atoi(argv[6]); ++tick) {
        for (auto& source : sources) source.Step(field);
        field.Step();
        for (unsigned offset = 0; offset < LakeWater::WaveField::kCells * 4; offset += 4) {
            LakeWater::RawCellState state;
            field.ReadCellByFieldByteOffset(offset, state);
            for (int16_t value : {state.heightFixed, state.velocity}) {
                const char encoded[] = {char(uint16_t(value) >> 8), char(value)};
                out.write(encoded, 2);
            }
        }
    }
    return out && sineOut ? 0 : 1;
}

// ROM-independent side of the cartridge comparison. Emit packed big-endian
// field snapshots after every tick; the Python oracle executes ROM instructions.
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--source") == 0) return TraceSource(argc, argv);
    if (argc < 5 || (argc - 5) % 3 != 0) {
        return 2;
    }
    LakeWater::WaveField field;
    field.SetWindowCenter(std::strtof(argv[2], nullptr), std::strtof(argv[3], nullptr));
    for (int i = 5; i < argc; i += 3) {
        field.Stamp(std::strtof(argv[i], nullptr), std::strtof(argv[i + 1], nullptr), std::atoi(argv[i + 2]));
    }
    std::ofstream out(argv[1], std::ios::binary);
    std::ofstream uvOut(std::string(argv[1]) + ".uv", std::ios::binary);
    for (int tick = 0; tick < std::atoi(argv[4]); ++tick) {
        field.Step();
        for (unsigned offset = 0; offset < LakeWater::WaveField::kCells * 4; offset += 4) {
            LakeWater::RawCellState state;
            field.ReadCellByFieldByteOffset(offset, state);
            for (int16_t value : {state.heightFixed, state.velocity}) {
                const char bytes[] = {static_cast<char>(uint16_t(value) >> 8), static_cast<char>(value)};
                out.write(bytes, 2);
            }
        }
        for (int v = 0; v < 384; ++v) {
            for (int u = 0; u < 128; ++u) {
                const auto uv = LakeWater_ComputeCartridgeUV(field.ReadCell(u - 1, v - 1).heightFixed,
                    field.ReadCell(u, v + 1).heightFixed, field.ReadCell(u + 1, v).heightFixed);
                for (int16_t value : {uv.s, uv.t}) {
                    const char bytes[] = {static_cast<char>(uint16_t(value) >> 8), static_cast<char>(value)};
                    uvOut.write(bytes, 2);
                }
            }
        }
    }
    return out && uvOut ? 0 : 1;
}
