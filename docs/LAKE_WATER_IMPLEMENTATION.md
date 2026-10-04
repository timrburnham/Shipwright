# Lake Hylia cartridge water implementation

Status on 2026-10-04: **integrated and enabled by default**. The Graphics menu's
“Wave Race Original Lake Water” checkbox controls `gEnhancements.WaveRaceLakeWater`.
The baseline uses the ROM's Original/cartridge texture, material, wave source,
fixed-point integrator and UV formula. Enhanced water profiles and effects are
not used. Both graphics and Zelda contacts fall back together if required data,
footprints or renderer preflight fail.

## Implemented

- Scene lifecycle, update-before-actor hook, live Zelda water-box mean levels,
  main-lake query hooks, and ripple surface hook. Pause/freeze avoids updates.
  No save data is changed by the adapter.
- Runtime footprint parser for the existing Zelda OTR resources: 37 high-water
  triangles and 12 low-water triangles, translated by the water actor's XZ.
  Original Y is retained: high-water tiers are 0/200/440, low-water tiers
  are -680/120/440 relative to the actor. The high mesh has 28 main, 4 lower-river,
  1 upper-river and 4 waterfall triangles; low has 5/4/1/2. Horizontal regions
  sample their matching water boxes (main 2/3, lower 1, upper 0). Waterfall faces
  are rendered but excluded from horizontal contact masks.
  Runtime high-water local bounds are X `-3618..4114`, Z `-2808..4231`.
- Native field adapter with cartridge lattice indexing, signed fixed-point
  stamps, triangular sampling, raw field access and one-player shifts `{9,5,6}`.
  The radius-12 update now preserves the original interleaved height integration
  and alternating top-edge pair orientation. `kCartridgeParityVerified=true`.
  A separate fixed-domain adaptation accepts even radii up to 126; the original
  radius-12 behavior remains the default. Its remote-cell regression checks
  that distant patches evolve and reset returns to the reference domain.
  Lake Hylia now uses a fixed hex domain covering both Zelda water footprints,
  padded by three cells for UV neighbors (world center `(39,103)`, radius 94
  in the measured scene; 27 source records in nine repeated Sunny Beach layouts).
  The camera cannot relocate the field. Sources repeat as a complete three-record
  layout with shared integer-lattice translations, preserving geography, phase
  and source order;
  stamps outside the domain are rejected before torus modulo and per corner.
- A focused big-endian MIPS reference interpreter executes the supplied ROM's
  setup, camera-offset, stamp, integrator, mesh, sine initializer and source-loader
  instructions. Unsupported instructions fail; only libc `sqrtf` is substituted
  for the compared math paths. The camera wrapper's mesh leaf is skipped while
  comparing the integrator; mesh attributes are compared by calling it separately.
- Exact original secondary source update (`func_8004CC7C`), including the
  cartridge Taylor-series sine table and triangle hard-set path. The enabled
  source is Sunny Beach, normal profile, difficulty 0: three 64-byte secondary
  records and no primary records, selected through `D_800D68AC/D_800D68D4`.
  All three sources match the ROM executor for 128 ticks. The complete layout
  repeats across the fixed Zelda domain. Independently tiling each record
  everywhere was removed: it made originally disjoint generators overwrite
  one another over much of the lake. The separate Drake Lake
  profile-8 fixture remains available for regression comparisons.
- Hash-checked ROM extraction of setup/mesh lists, Sunny Beach's actual 32x32
  RGBA32 slope-shading map, source records, and course background provenance.
  Segment 1 uses the two DMA ranges in `SysMain_Thread`, starting at ROM
  `0xF6090` and splitting at segment `0x01008290` / ROM `0xFE320`.
  Extracted bytes remain in ignored `build/cartridge-water/`.
- Sunny Beach's Original one-cycle DECALRGBA state and ROM-verified interior
  UV stencil `(u-1,v-1), (u,v+1), (u+1,v)`. The 32x32 map is sampled from
  height gradients; it is a shading lookup, not a scrolling world texture.
  The cartridge's background quad has RGBA `(0,242,228,255)` and Y `-100`.
  The adapter draws the original cyan body material on Zelda's horizontal wet
  footprints at each live water level minus 100, followed by the translucent
  Original highlight pass. The background writes depth; highlights compare
  depth without writing it. The former opaque fused composite hid submerged
  actors and has been removed from the enabled path.
  Sampler state is explicit, including Original bilinear filtering and the
  half-scale texture coordinates. No enhanced foam or lighting is added.
- The identity modelview is converted through `guMtxF2L`. Shipwright's
  `guMtxIdent` writes floats into its matrix union, while RT64 expects packed
  fixed-point words. Earlier gray-water screenshots taken before this fix
  showed the lake bed rather than a correctly placed custom wave mesh; they
  cannot establish whether the separate background sits below shallow beds.
- The world-space wave lattice clips against Zelda's footprint, including its
  tier connections and waterfall faces. Host vertices/commands follow Zelda's
  two graphics pools and avoid overflowing `Graph_Alloc`. Static exact-bit
  recipe deduplication preserves all triangle corners and order; each unique
  recipe is evaluated once per game frame, then packed into 32-vertex caches.
  Indexed TRI2 pairs reduce input command and vertex-transform overhead.
  High water: 25,721 triangles, 14,635 recipes, 858 vertex loads, 27,171 loaded
  vertices, five child lists (maximum 2,994 commands). Low water: 3,402
  triangles, 2,155 recipes, 111 loads, 3,502 loaded vertices, one child list.
  Child lists remain below the translator's 4,096-command limit. A trace-only
  byte oracle checks every original triangle-corner Vtx against the shared
  output for both topologies before the first traced submission.
  Falls split at every crossed lattice boundary; interpolated live-level
  weights and field samples keep their edges aligned with horizontal water.
  Fully degenerate source triangles are skipped. Surface queries interpolate
  the same emitted integer vertex heights used by the renderer.
- Zelda shore adaptation derives exposed edges from the union of horizontal
  wet triangles, removes internal and partially shared seams, preserves holes
  and concavities, and exempts waterfall flow joins. A smooth 128-unit falloff
  brings wave displacement to zero at physical banks. Original UV slope inputs
  use the same shore attenuation; the raw cartridge simulation is unchanged.
  Clipped bank vertices also return to the neutral Original UV center,
  avoiding bright highlights on a geometrically flat edge.
  Static recipe and node factors are cached during preflight. Gameplay samples
  the clipped rendered triangles rather than applying a separate shore formula.
- Debug builds retain symbols but compile the RT64 RSP, F3DEX2 dispatch,
  interpreter and native display-list translator with strict `-O2`, without
  fast-math or FP contraction. The rest of the game retains its Debug policy.
- Hash-checked runtime texture/source loading, local CMake extraction/staging,
  fixed 20 Hz active-game timeline, and scene/toggle resets.
  Render readiness gates both simulation publication and water queries, so a
  verified field cannot change gameplay before its graphics are ready.
- Isolated capture tool that can load a copy of File3 into temporary slot 1,
  enter the existing debug warp screen, and capture Lake Hylia. Original user
  saves and normal game configuration are never written by this tool.

## Validation

From `Shipwright/`:

```sh
cmake --preset linux-clang-rt64
cmake --build --preset linux-clang-rt64 --parallel 8
ctest --preset linux-clang-rt64
python3 tools/verify_wr64_water.py \
  '../Wave Race 64 (U) (V1.1) [!].z64' \
  --port build/linux-clang-rt64/tests/rt64/rt64_wave_field_trace
python3 tools/extract_wr64_cartridge_water.py \
  '../Wave Race 64 (U) (V1.1) [!].z64'
python3 tools/capture_lake_water.py \
  --output build/linux-clang-rt64/lake-water-file3-shore-zelda \
  --water zelda --age adult --entrance 4 --frames 480 \
  --save build/linux-clang-rt64/soh/Save/file3.sav
```

The integrated build passes. Eight CTest checks are configured: cartridge
parity, renderer smoke, translator, texture patches, lake domain, indexed mesh
reconstruction, shoreline profile, and field arithmetic. The seven targeted
checks passed after the shoreline changes, including desktop Vulkan; the
unchanged source/field math had already passed the ROM-backed comparison.
With `WR64_WATER_ROM` configured, CTest includes the ROM-backed comparison.
The renderer fixture checks RGBA32 texel color/alpha, one-cycle decal and
Drake Lake two-cycle fog, transparency, and depth testing without depth writes.
The smoke sequence also checks a subsequent wide framebuffer copy after a
resolution decrease. These checks do not establish full appearance parity.

The ROM-backed comparison checks all 192 KiB of field state after each of 60
ticks across five centers, with overlapping stamps, negative coordinates, wrap
seams and frozen out-of-window cells. It also compares all 397 interior vertex
UVs at each center. The native sine table matches all 4096 cartridge entries;
the selected source and integrator match all field bytes for 128 ticks, one full
stored phase cycle. These are instruction-level comparisons, not donor screenshots.
The implementation explicitly disables fast-math and floating-point contraction
for cartridge math, including Release configurations.

The standalone field fixtures assert raw multi-tick height and velocity, boundary behavior,
reset, invalid inputs, and planar sampling. Address/undefined sanitizers pass
with leak detection disabled because LeakSanitizer cannot run under this
sandbox's ptrace environment. The ROM-backed comparisons are the independent
oracle for update ordering and source behavior.

The earlier 2026-10-03 fallback-only adult File3 laboratory shoreline capture shows the restored lake.
The runtime parser reports all 37 high-water triangles and all 12 low-water
triangles in a separate drained-adult temporary-save run. Enabled-but-unready
and disabled captures are pixel-identical (PPM SHA-256
`792f16a5b54b881b22f2df870728108557ca43cf22a1258dbfd46f31dd78427d`).
The subsequent source-integrated capture has the same hash. This validates
coherent fallback only, not animated replacement water or swimming.

## Integration validation and practical limits

The enabled adult File3 laboratory-shoreline run submitted 116 replacement draws
in the first reporting interval and accepted 2,193 main-lake contact samples.
The sampled surface varied from `-1328.45` to `-1255.00` around Zelda's mean
`-1313`. The enabled drained adult run selected the 12-triangle low footprint
and retained the mean `-1993`; both meshes submit successfully. A restored-lake
shortcut-entry run with 4x MSAA reached the swimming surface (Dive HUD), submitted
116 replacement draws and accepted 2,373 surface queries. The capture tool only
writes isolated settings and temporary saves.

`ctest --preset linux-clang-rt64` passes all six RT64 tests, including the
ROM-backed full-field/source/UV comparisons, fixed-domain/source tiling,
translator and renderer material/depth/framebuffer-resize checks. The unrelated
upstream `prism` registration is outside the RT64 preset and has no executable
in this build.

The final GDB gameplay probe at the underwater shortcut with 4x MSAA completed 547 lake updates,
accepted main-lake contacts, observed swimming and underwater flags, and measured
132.7 units of sinking with Iron Boots followed by 153.4 units of rise with normal
boots. It manipulates only the temporary player's equipment/position, never File3.
Run it with `--entrance 5 --frames 1000 --msaa 4 --gdb-script tests/rt64/lake_water_smoke.gdb`.

The normal pause-menu regression observed 160 paused updates with the simulation
step count fixed at 210, then resumed to 370 steps. This uses the regular Start
button input, not a synthetic pause state. Run it with `--frames 900 --gdb-script
tests/rt64/lake_water_pause.gdb --keys
'60:Space,120:Space,180:Space,500:Space,650:Space'`.

Water motion intentionally follows the original 20 Hz simulation without
between-tick interpolation. Zelda owns the water-box means, drained/restored
state, collision eligibility, equipment and actor behavior. The river water
boxes preserve their native means and sample the corresponding animated tiers.
Randomizer-specific lake level
transitions, every shoreline and presentation combination still need broader
play testing; the automated checks below record the paths actually exercised.

The final restored river-entry run accepted 851 main, 9,084 lower-river and 26
upper-river samples in its first interval, retaining means -1313/-1113/-873.
The drained river run accepted 65/3,809/26 respectively, retaining means
-1993/-1193/-873. Both also submit the subdivided waterfall faces.

A child laboratory-entry run exposed a pre-existing empty vertex-hash command
in `gZoraRipplesModelDL`. The strict malformed-resource repair reuses the user's
fishing-ripple repair and leaves custom/valid resources alone. The repaired run
completed and submitted the cartridge mesh.

Save-state loads explicitly rebuild the host wave state from the initial
cartridge phase, in either time direction. Zelda heap and save data retain the
normal save-state policy. The field is not serialized: contacts and drawing
share Zelda fallback until publication, then resume on the rebuilt field. The normal F5/F7 regression passed with one
save, two loads, two bridge rebuilds and 224 subsequent simulation steps.

The targeted river probe accepted 397 upper-river and 443 lower-river samples.
The lower-river position entered `IN_WATER`; the upper probe accepted surface
contacts but did not enter that state at its selected point. The shared ripple
repair also passed the existing fishing regression (818 ripple draws).

## 2026-10-04 rendering and shoreline follow-up

The 13:11 user recording shows the actual packed-matrix wave mesh, with dense
bright crests and opaque water. Restoring separate Original passes retains
texel alpha, while explicit sampler state prevents inherited Zelda filtering
from controlling the shading map. The cyan background follows high/low wet
footprints at each live tier minus 100, with the ROM's negative XZ winding
(upward normals) and exact depth-writing body mode.

The trace-only surface oracle compares contact queries against centers of
25,553 nondegenerate restored-water triangles and 3,332 drained-water
triangles, using their emitted integer XYZ. Maximum errors were
0.00024414 and 0.00012207 world units. Exact-bit vertex deduplication also
passes for both meshes after adding shoreline factors.

Isolated adult File3 laboratory captures are under
`build/lake-water-visible-before/` and `build/lake-water-shore-translucent/`.
The baseline measured RT64 translation + processing at 3.248 + 12.117 ms;
the updated build measured 3.338 + 12.238 ms per submission, at native 320x240,
MSAA 1, after entering Lake Hylia. These are CPU submission averages over
120 calls, not GPU/frame-time or high-resolution performance claims.
The initial shoreline capture shows a flatter edge and visible shallow bed,
but retained dense bright crests in deeper water. A subsequent source-layout
audit found that the original three source patches are pairwise disjoint:
the first two are separated by over 2,194 X units and the third lies over
444 Z units beyond the first. Independently extending each source everywhere
discarded that geography and caused thousands of hard-set overwrites.

`TileCartridgeSourceLayout` now repeats the whole course layout. Repeat periods
are the combined source bounds in oblique lattice cells plus a two-cell margin;
translations preserve relative positions and phases, with the Zelda water actor
providing the world origin. This repeat spacing is a Zelda coverage adaptation,
not a cartridge course parameter. Original source amplitudes, speeds, pitches,
phase ranges, the integrator and shading formula remain unchanged. Synthetic
regressions check retained source offsets, ordering and phase, and invalid inputs.

`build/lake-water-course-layout/` shows substantially less dense white coverage.
At native 320x240/MSAA1 its three complete gameplay windows measured combined
RT64 translation/processing at 15.62, 15.60 and 15.34 ms per submission.
This is close to the 15.37 ms visible baseline. Full matched-camera appearance
parity and broader shoreline play testing are still unfinished.

The isolated underwater shortcut probe at MSAA 4 passed on the updated
surface: 819 game updates, 11,162 accepted queries, both `IN_WATER` and
`UNDERWATER` observed, Iron Boots sinking 152.3 units, and normal-boots
resurfacing 158.1 units. The capture shows submerged Link through the
translucent pass. Evidence: `build/lake-water-underwater-translucent/`.

With the final layout, the underwater regression passed again: 819 updates,
11,196 accepted queries, swimming and underwater states observed, Iron Boots
sinking 132.7 units, and normal-boots resurfacing 150.5 units. The final river
probe accepted 325 upper and 1,964 lower contacts, and both tiers entered
`IN_WATER`. Evidence: `build/lake-water-underwater-layout/` and
`build/lake-water-river-layout/`. The original File3 remains untouched.

The final drained adult laboratory capture completed with the low topology,
27 source records, live means -873/-1193/-1993, and both surface/deduplication
oracles passing. This entrance mostly stands above the exposed lake bed; four
eligible main-water queries were accepted before Link settled on dry ground.
Evidence: `build/lake-water-low-layout/`. A separate temporary child run used
the restored topology, as Zelda normally does for child Link.

## Provenance

- Shipwright base: `39d5c79a148729648149ef95eb74ad98b5435008`.
- Wave Race recomp: `4926225b89a92ea4a2ee1cdf396cab03df9c7329`.
- Reference decomp: `a51b38a2aaef68da10ea1e47247e70be3b1d4c70`.
- Supplied ROM SHA-1: `508dfc2d4caa42b6f6de5263d0aed5e44ac7966a`.

ROM disassembly, generated assets, saves, screenshots, and capture logs are local
build/reference data. No ROM-derived texture or vertex coordinate arrays are
embedded in distributed source.
