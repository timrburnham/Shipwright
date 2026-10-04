#pragma once

#include <cstdint>

namespace LakeWater {

// The source field is the cartridge's 384 x 128 wrapping oblique lattice.
// Surface height is expressed in Zelda/world units after the cartridge's
// signed-height >> 8 conversion. Normals point toward +Y.
struct SurfaceSample {
    bool valid = false;
    float height = 0.0f;
    float normalX = 0.0f;
    float normalY = 1.0f;
    float normalZ = 0.0f;
};

struct RawCellState {
    int16_t heightFixed;
    int16_t velocity;
};

// Integer shifts copied from the cartridge scene setup. They are explicit
// because Wave Race selects them by scene; they must not be guessed by users
// of the adapter. The cartridge's ordinary one-player race uses {9, 5, 6},
// as shown by func_8004F9E0 in the hash-matched ROM.
struct IntegratorShifts {
    unsigned restoreShift;
    unsigned couplingShift;
    unsigned dampingShift;
};

class WaveField {
  public:
    // Compared byte-for-byte with ROM instructions by verify_wr64_water.py.
    // Course-source and renderer readiness are separate requirements.
    static constexpr bool kCartridgeParityVerified = true;
    static constexpr unsigned kRows = 384;
    static constexpr unsigned kColumns = 128;
    static constexpr unsigned kCells = kRows * kColumns;
    static constexpr float kCellPitch = 64.0f;

    WaveField();
    explicit WaveField(IntegratorShifts shifts);

    // Clears the cartridge field. The caller owns scene lifecycle and domain
    // eligibility.
    void Reset();

    // Advances one fixed Wave Race simulation tick. Source generator lines
    // are stamped separately through Stamp(), as in the original update path.
    void Step();

    // Recenters the radius-12 one-player simulation window on the same
    // world-fixed lattice used by func_80050F8C. Values outside that window
    // remain frozen, matching the cartridge field's boundary behavior.
    void SetWindowCenter(float worldX, float worldZ);

    // Lake coverage adaptation: keep the cartridge row walk, with a fixed
    // larger hex. An even radius below 128 cannot alias the packed torus.
    // Reset restores the ordinary radius-12 reference window.
    bool SetDomain(int32_t centerU, int32_t centerV, unsigned radius);

    // Ports func_8004F3D4's source stamp: amplitude is in whole world units;
    // the selected corner receives its conditional signed fixed-point delta,
    // with velocity cleared when the source condition succeeds.
    void Stamp(float worldX, float worldZ, int32_t amplitudeUnits);

    // Generator hard-set path: sets the containing triangle's three corners
    // and clears their velocities (func_8004CC7C / func_8004F520).
    void SetSourceTriangle(float worldX, float worldZ, int32_t amplitudeUnits);

    // Samples the piecewise-planar triangular lattice. Mean water level is
    // supplied by Zelda's selected water box, never stored in the field.
    SurfaceSample Sample(float worldX, float worldZ, float meanHeight) const;

    // Reads exact lattice state in the cartridge's 1/256-unit signed storage.
    // Useful for deterministic parity fixtures and cartridge mesh generation.
    RawCellState ReadCell(int32_t uCell, int32_t vCell) const;

    // Reads a cell from the cartridge's packed D_80162420 field using the
    // byte offset carried by a D_80160A98 mesh slot. The N64 array stores one
    // signed height/velocity pair in four bytes per cell.
    bool ReadCellByFieldByteOffset(uint32_t fieldByteOffset, RawCellState& state) const;

  private:
    struct Cell {
        int16_t height;
        int16_t velocity;
    };

    static unsigned Index(int32_t uCell, int32_t vCell);
    static int16_t WrapS16(int32_t value);

    IntegratorShifts shifts_;
    Cell cells_[kCells]{};
    int32_t windowU_ = 0;
    int32_t windowV_ = 0;
    unsigned radius_ = 12;
    bool clampSources_ = false;
};

} // namespace LakeWater
