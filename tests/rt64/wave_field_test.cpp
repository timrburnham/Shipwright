#include "../../soh/soh/Enhancements/Graphics/LakeWater/WaveField.h"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    LakeWater::WaveField field;

    const auto still = field.Sample(10.0f, -20.0f, -1313.0f);
    assert(still.valid);
    assert(still.height == -1313.0f);
    assert(still.normalX == 0.0f && still.normalY == 1.0f && still.normalZ == 0.0f);

    // At (u,v)=(0,0), equality selects (cu+1,cv). The source adds -2*256
    // to that corner and the read exposes the signed high byte as -2 units.
    field.Stamp(0.0f, 0.0f, -2);
    LakeWater::RawCellState meshSlotCell{};
    assert(field.ReadCellByFieldByteOffset(4, meshSlotCell));
    assert(meshSlotCell.heightFixed == -512);
    assert(!field.ReadCellByFieldByteOffset(1, meshSlotCell));
    assert(!field.ReadCellByFieldByteOffset(LakeWater::WaveField::kCells * 4, meshSlotCell));
    const float targetX = 64.0f;
    const float targetZ = 0.0f;
    const auto displaced = field.Sample(targetX, targetZ, -1313.0f);
    assert(displaced.valid);
    assert(displaced.height == -1315.0f);

    field.Reset();
    assert(field.Sample(targetX, targetZ, -1313.0f).height == -1313.0f);

    // One-tick cartridge fixture. At 0x8004EB2C-0x8004EB5C
    // the pair kernel uses arithmetic shifts 9 and 5 and applies signed
    // mean/difference terms; at 0x8004EC60-0x8004EC74 it adds velocity into
    // height, then damps at 0x8004EC64-0x8004EC74. With one -512 source cell
    // and six zero neighbors, velocity becomes 102 then 101; raw height is
    // -410 (high byte remains -2). verify_wr64_water.py separately compares
    // the whole field with executed cartridge instructions over multiple ticks.
    field.Stamp(0.0f, 0.0f, -2);
    field.SetWindowCenter(0.0f, 0.0f);
    field.Step();
    assert(field.ReadCell(1, 0).heightFixed == -410);
    assert(field.ReadCell(1, 0).velocity == 101);
    assert(field.Sample(targetX, targetZ, -1313.0f).height == -1315.0f);

    field.Reset();
    // Multi-source, three-tick deterministic fixture from the documented
    // integer equations at func_8004EAA4. Source points select cells (1,0),
    // (2,0), (3,0), respectively. Expected raw values include every pair's
    // signed shift and 16-bit wrap. The separate ROM-backed test also covers
    // nonzero overlapping stamps, window boundaries, and coordinate wrap seams.
    field.Stamp(0.0f, 0.0f, -2);
    field.Stamp(64.0f, 0.0f, -1);
    field.Stamp(128.0f, 0.0f, -3);
    field.SetWindowCenter(0.0f, 0.0f);
    field.Step();
    field.Step();
    field.Step();
    assert(field.ReadCell(1, 0).heightFixed == -41);
    assert(field.ReadCell(1, 0).velocity == 204);
    assert(field.ReadCell(2, 0).heightFixed == -155);
    assert(field.ReadCell(2, 0).velocity == 52);
    assert(field.ReadCell(3, 0).heightFixed == -43);
    assert(field.ReadCell(3, 0).velocity == 311);

    field.Reset();
    // The hex tip (12,0) has only three in-window lattice neighbors. Its
    // boundary update is separately fixed here to catch accidental wrap-edge
    // coupling or whole-torus stepping.
    field.Stamp(11.0f * 64.0f, 0.0f, -2); // stamps lattice cell (12,0)
    field.Step();
    assert(field.ReadCell(12, 0).heightFixed == -461);
    assert(field.ReadCell(12, 0).velocity == 51);

    field.Reset();
    // A stamped cell outside the camera-centered N=12 hex is frozen, as the
    // cartridge integrator only touches its generated active-window lists.
    field.Stamp(2048.0f, 0.0f, -2);
    field.Step();
    assert(field.Sample(2112.0f, 0.0f, -1313.0f).height == -1315.0f);

    field.Reset();
    // Lake coverage is fixed in world lattice coordinates, so a remote
    // stamped patch evolves even when it is far outside the race camera hex.
    assert(field.SetDomain(0, 0, 96));
    field.Stamp(79.0f * 64.0f, 0.0f, -2);
    field.Step();
    assert(field.ReadCell(80, 0).heightFixed != -512);
    // A far source that aliases through the original torus must not write a
    // lake cell. A source at the boundary must only set in-domain corners.
    field.Reset();
    assert(field.SetDomain(0, 0, 96));
    field.SetSourceTriangle(24576.0f, 0.0f, 60);
    assert(field.ReadCell(0, 0).heightFixed == 0);
    assert(field.ReadCell(1, 0).heightFixed == 0);
    field.SetSourceTriangle(96.0f * 64.0f, 0.0f, 60);
    assert(field.ReadCell(96, 0).heightFixed == 60 * 256);
    assert(field.ReadCell(97, 0).heightFixed == 0);
    assert(!field.SetDomain(0, 0, 128)); // would allow torus aliasing
    assert(!field.SetDomain(0, 0, 95));
    field.Reset();
    field.Stamp(79.0f * 64.0f, 0.0f, -2);
    field.Step();
    assert(field.ReadCell(80, 0).heightFixed == -512); // reset restores reference domain

    field.Reset();
    assert(!field.Sample(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f).valid);
    assert(!field.Sample(0.0f, 0.0f, std::numeric_limits<float>::infinity()).valid);
}
