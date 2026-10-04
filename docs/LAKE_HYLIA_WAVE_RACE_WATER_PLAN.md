# Lake Hylia water replacement using Wave Race 64

Research and implementation plan, 2026-10-03, revised to make Original/cartridge graphics the baseline. The water-plane replacement (main lake, river tiers and connecting falls) is integrated and enabled by default as of 2026-10-04; see the [current implementation and validation record](LAKE_WATER_IMPLEMENTATION.md) for verified paths and remaining scope. This document retains the initial research and full validation target.

## Intended result

Replace Lake Hylia's visible water surfaces with actual Wave Race 64 wave motion and the water graphics the supplied recomp calls **Original** or **cartridge**. Link still uses Zelda's swimming, diving, Iron Boots, drowning, collision, camera, items, effects, and progression. The lake still drains and refills according to Zelda's existing actor and randomizer logic. Rendering and water-contact queries must agree about where the surface is.

Baseline requirement: reuse the cartridge's wave-field algorithm, ROM-derived wave parameters, water textures, vertex attributes, texture-coordinate/highlight generation, color combiner, transparency, fog, and depth/blend behavior. Reproduce those graphics through RT64's ordinary N64 rendering path where practical. A dedicated RT64 shader is acceptable only if it reproduces this Original/cartridge appearance and behavior; the implementation technique does not define the visual target.

The donor's `Quality::Original` is a true bypass of its modern water renderer. **`Style::Classic` with Enhanced/Best quality is not Original**: it preserves aspects of the cartridge look but adds effects. Enhanced/Best/Ultra, Deep/Aqua/Classic enhancement styles, procedural fine ripples, modern absorption/refraction, SSR, added shoreline foam/wake fields, and added spray are outside the baseline. They are not required follow-on work and should be added only if requested later. Cartridge-authored highlights, transparency, reflections, and effects remain part of the Original target wherever the selected course actually uses them.

A shader over Zelda's original flat polygons, or an unrelated sine-wave approximation, does not meet the target. Original Wave Race graphics are the main deliverable, not a comparison mode. “Original Zelda” below means the existing lake fallback and is distinct from “Original Wave Race.”

Lake scope includes the main lake and its horizontal river water inside `SCENE_LAKE_HYLIA`. Inventory the river cascade separately: a waterfall is not a horizontal wave surface and needs an adapted falling-water material to replace its appearance. The fishing pond, laboratory, Water Temple, Gerudo Valley scene, and other scenes retain their own water. Existing bubbles, splashes, and ripple actors remain functional; their contact positions must match the replacement.

## Findings from this checkout

| Evidence | Implication |
| --- | --- |
| Supplied ROM SHA-1 is `508dfc2d4caa42b6f6de5263d0aed5e44ac7966a`. | It matches the USA Rev A / v1.1 required by the donor tooling. |
| Donor `reference/wr64-decomp`, generated `RecompiledFuncs`, and populated runtime/RT64 submodules are absent. | The repository describes the original wave functions, but those functions are not yet callable here. Restore the reference/toolchain and generate them from the supplied ROM. |
| Donor RT64 gitlink is `5473732a822a4423b5696e7cb18fecc425a59875`. Shipwright RT64 HEAD is `a79498e4015daf3247cf47d82c1f6a917004fcfc`. | These are different integration trees. Preserve Shipwright's renderer fixes while porting water. |
| A read-only `git apply --check` of `rt64-water.patch` against Shipwright RT64 fails in `CMakeLists.txt`, `src/hle/rt64_state.h`, and `src/render/rt64_framebuffer_renderer.cpp`. | The modern renderer is not required for this baseline. If useful interpolation plumbing is extracted, adapt only the needed portions deliberately. The check did not modify files. |
| `BgSpot06Objects_DrawLakeHyliaWater` selects `gLakeHyliaLowWaterDL` or `gLakeHyliaHighWaterDL` and submits to `POLY_XLU_DISP`. | This is the principal replacement insertion point. Retain a complete original fallback. |
| The lake actor names four water boxes: upper river, lower river, main 1, main 2. | Treat separate water levels/domains explicitly; avoid overlapping duplicate surfaces. |
| Main mean heights are raised `-1313`, lowered `-1993`; the actor also uses a `-681` offset and distinct low/high geometry. | Do not infer rendered height from actor origin alone. Moving-state geometry and collision must be measured. |
| `WaterBox_GetSurfaceImpl` returns the selected box's flat `ySurface`; `WaterBox_GetSurface2` compares directly against it. Actor and camera code consume these queries. | Keeping these entirely flat while rendering large waves would leave Link and effects visibly detached. |
| Lake scene draw config is `func_8009E54C` / `SDC_LAKE_HYLIA` (5). It sets underwater ground texture/environment state. | Preserve the ground effects and randomizer's `roomCtx.unk_74[0]` behavior independently of replacing the surface. |
| Shipwright lowers native pointer-based `Gfx` through `Fast::DisplayListTranslator` into an 8 MiB RDRAM image. | Translate the cartridge water commands/assets into host `Gfx` and RT64 RDRAM addresses explicitly; any optional interpolation metadata also needs a compatible transport. |

Revision context: Shipwright HEAD `39d5c79a148729648149ef95eb74ad98b5435008`; libultraship HEAD `72b3e91e89c4a208f1aa891bd36188fa1f7760f6`. Reconfirm these before implementation.

### What the donor actually provides

The original wave field is a wrapping triangular lattice, 384 rows × 128 columns, with signed 16-bit height and velocity pairs: 192 KiB. Cell pitch is 64 Wave Race units. Height readers use arithmetic `height >> 8`, not floating-point division by 256. The second component is velocity despite older comments calling it age.

The donor documentation identifies these original functions:

- `func_8004F9E0`: initializes scene-specific wave parameters and window geometry.
- `func_8004E614`: builds the simulation window's indexing/shape lists.
- `func_80050F8C`: recenters the simulated window.
- `func_8004EAA4`: damped neighbor-coupled height/velocity integration.
- `func_80071E70`, `func_8006E3A8`, `func_8004F520`, `func_8004F3D4`: load, animate, and stamp course wave-generator lines.
- `func_80050204`: builds the original drawable wave mesh.
- `func_8004D30C`: returns a normalized plane quantity and slope information. **Its return is not directly the vertical surface height.**

The ordinary one-player simulation advances a camera-centered hexagon of radius 12 cells (469 cells), rather than the entire field. Course swell comes from up to eight generator lines. Outside the active window, old field values can remain frozen. The field also aliases world locations at about 14,189 units. Sampling the full field is therefore not enough to animate a whole Zelda lake.

The modern renderer is available as `tools/patches/rt64-water.patch` plus subsequent corrections in `tools/patch_rt64_water.py`. It adds a material command, draw tracking, water interpolation, GPU resources, and HLSL passes for scene capture, depth reduction, normals, water depth, shading, wake/foam fields, and optional spray. `src/water.cpp` supplies material snapshots and profiles; it does not implement the cartridge wave integrator.

The Original rendering reference must come from ROM-derived `Draw_WaterEffects`, its selected water display list (one-player `0x0100B590` is the initial reference), the mesh builder, and every texture/segment/state dependency they use. Capture state both inside the list and inherited from its callers; a list by itself may not describe the whole appearance. Inspect course-specific differences before choosing reusable parameters. Modern `profiles.json` values are not cartridge material definitions.

## Recommended architecture

Create a scene-owned `LakeWaterSystem` in Shipwright with an isolated Wave Race simulation adapter. Run the original wave functions in a bounded private memory/context initially; do not boot another game inside Zelda. Generate only the needed dependency closure for the production adapter after proving parity. If native equivalents are later introduced, require comparisons against the generated original before replacing it. Avoid linking the entire Wave Race frontend, audio, controller stack, or N64ModernRuntime into Shipwright merely to obtain water.

Keep ROM-derived code/data in generated, ignored build/cache outputs and provide a repeatable generation process using the supplied ROM. Reuse the donor's relevant original port/interpolation code with its notices. Keep a manifest identifying the donor revision, ROM hash, generated function set, and source of parameters. Do not commit the ROM or extracted cartridge assets.

Data flow:

```text
Zelda scene lifecycle + water-plane actor + water boxes
                      |
               LakeWaterSystem (game thread)
                      |
          fixed-rate Wave Race field + domain masks
                  /                 \
     Zelda surface queries       immutable frame snapshot
       and wet contacts             |
                             lake-clipped wave mesh
                                    |
                  cartridge water draw commands/assets
                                    |
                         DisplayListTranslator
                                    |
                       RT64 ordinary N64 rasterization
                   (or an Original-equivalent shader)
```

Own three distinct concepts:

1. **Mean level and eligibility:** Zelda's water boxes, room/property rules, scene setup, and lake actor remain authoritative.
2. **Surface displacement:** Wave Race height/velocity state, sampled at world-fixed XZ and adapted to the lake's scale/boundaries.
3. **Appearance:** the selected cartridge water draw state, textures, generated UVs/highlights, colors, alpha, fog, and depth behavior. Adapt environment/camera inputs to Zelda while retaining the Original visual model.

### Surface contract

Provide `SampleSurface(x, z, selectedWaterBox, tick)` returning height, normal, and validity. The mean comes from the selected live Zelda water box. Displacement comes from the Wave Race field and the same shoreline attenuation used by the drawable mesh. Keep the water box's stored `ySurface` as the mean; one scalar cannot hold a spatial wave field.

Use a documented conversion between Zelda and Wave Race world units. Start at 1:1 for parity, then choose spatial and height scales by measuring Link, shoreline geometry, and swim thresholds. Scale field extents, texture/highlight coordinates, contact radii, and attenuation distances consistently. Any adjustment to generator placement, simulation coverage, scale, or shore attenuation must be recorded as a Zelda adaptation rather than claimed as byte-identical course behavior.

Within each triangle use its actual vertical plane/barycentric height. Do not reuse the donor ring's uncorrected `func_8004D30C` return: donor docs acknowledge a slope-dependent height error there. Verify the conversion against the original mesh builder as well as the original plane-query output. Define negative-coordinate modulo, shifts, rounding, and signed overflow explicitly to match the generated MIPS behavior.

Render interpolation may lie between two gameplay snapshots, just as Link's rendered transform does. Gameplay uses the latest committed simulation state, never presentation-thread state. Mesh and Link must use the same presentation fraction. Do not add enhanced procedural shader ripples. Preserve the cartridge's own animated vertex and texture attributes across the same snapshots.

### Coverage and boundaries

Derive the visible wet footprint from the lake's existing high/low meshes and collision terrain, with water-box eligibility preserved. Water boxes are coarse behavioral rectangles, not sufficient shoreline polygons by themselves. Extract a high/low surface census before choosing masks. Handle islands, banks, entrances, bridges, and the distinct river levels.

Use a world-fixed triangular mesh clipped to the wet footprint; subdivide to Wave Race cell density where gameplay occurs. Start with one uniform mesh to prove sampling, then add distant LOD only after continuity is verified. Use chunk-local coordinates if host vertex limits require them. Never stretch the original 500-vertex patch to cover the lake.

Choose simulation coverage after measuring the footprint in donor units. Prefer a bounded active domain covering the lake with the original update rules, stepping each cell once and preserving neighbor coupling. If this exceeds the field's alias-safe extent/window limits, partition into domains with explicit neighbor exchange or use a measured coordinate scale. Do not run independent overlapping fields or raise N alone: the original parameter tables and indexing lists also depend on N. All visible water must advance, including views far from Link. Whole-lake coverage is an explicit extension of the original camera-window scheduling.

Taper displacement near shores and fixed transition structures using one shared CPU mask; account for moving mean level so water neither floats over dry land nor exposes gaps. Keep upper/lower river domains distinct and join at their physical cascade. Rising/lowering water changes footprint and mean continuously.

## Implementation phases and exit conditions

### 1. Establish references and measure the lake

Restore the missing donor dependencies/reference checkout at pinned revisions. Use its documented ROM-generation flow to obtain disassembly, ELF, and generated functions. Build a Wave Race reference run with water quality explicitly set to Original and capture a calm course (Drake Lake is a candidate), plus a stronger-wave reference. Record the selected course's actual generator parameters, water textures, complete inherited/list render state, and per-frame vertex/UV/color/alpha outputs from the ROM. Use Original draw distance initially so extension-ring behavior does not obscure the cartridge reference. `assets/water/profiles.json` supplies artist-authored enhanced shading values and is not a baseline input.

Instrument Lake Hylia to log all submitted water-related display lists, actual transformed vertices, four water-box bounds/levels/properties, terrain intersections, actor state, setup index, and render ordering. Cover child high water, adult drained water, restored water, vanilla refill cutscene, and randomizer rise/lower. Identify river/cascade polygons that live in scene room lists rather than the water-plane actor. Record explicit resource/draw identities; do not classify every blue or translucent polygon as water.

**Exit:** verified donor executable/fixtures, complete surface inventory, measured scene bounds, chosen reference profile, dependency closure, and documented coverage/scale decision. Missing dependencies are currently a prerequisite, not evidence of an impossible port.

### 2. Extract and validate the Wave Race simulation

Build a small headless adapter that initializes and steps the original water functions without gameplay, graphics tasks, or audio. Inventory every dependency/global read, loaded table, random input, and memory write. Isolate all scratch memory; ensure calls cannot access live Zelda memory.

Compare field heights/velocities, generator stamps, window recentering, and mesh samples against a real donor run for deterministic seeds/ticks. Include nonflat fixtures, negative XZ, triangle diagonals, cell boundaries, wrap seams, and multiple parameter sets. Verify both original normalized plane output and true vertical height. Establish the original 20 Hz race stepping before adding lake coverage.

Implement the chosen lake coverage extension and boundary mask. Prove that cells do not freeze when the camera moves away and that aliasing does not cross-connect unrelated shores. Treat collision banks as an adaptation; retain original integrator and source waveform behavior in the interior.

**Exit:** reproducible parity fixtures and continuous lake-scale animation. An implementation consisting only of procedural shader ripples fails this phase.

### 3. Reproduce the Original/cartridge rendering path

Extract the selected cartridge water display list, texture assets, state dependencies, and the mesh builder's nonposition attributes. Preserve texture format/palette where applicable, tile addressing/filtering, texture coordinate generation, shade/primitive/environment colors, combiner, cycle type, fog, alpha, and depth/blend policy. Verify how camera, slope, lights, and course inputs generate broad highlights or authored reflections; replace those inputs with Zelda equivalents rather than substituting modern lighting equations.

Render a synthetic water patch through Shipwright's existing RT64 N64 rasterization path first. Compare it against the donor's Original at matched camera/tick and equivalent draw state. Preserve native textures, framebuffer operations, UV correction, pause capture, and prior shutdown fixes. Correct missing commands or texture/state translation only where evidence requires it. Port a dedicated shader only if needed for Original equivalence, with the same comparison gate; do not port the enhanced water pipeline merely to make Original work.

Preserve the cartridge's depth and translucency policy while adapting draw order to Zelda. Test overlapping near/far patches and opaque/translucent objects rather than automatically adding the modern renderer's private depth or scene-capture passes. Underwater views need an explicit adaptation because Wave Race's racing camera is primarily above water: preserve Zelda underwater fog/camera behavior and provide a consistent view of the surface from below without introducing enhanced refraction/Fresnel effects.

For interpolation, fixed world mesh identities can use a smaller custom path. If world-XZ sampling from the donor is necessary, extract the needed interpolation code and metadata support from `rt64-water.patch` and `patch_rt64_water.py`. The donor can send a quality-zero material for interpolation even though Original bypasses enhanced shading; this is a transport option, not a requirement to install all water shaders. Review transform availability and reject incompatible history. Preserve current renderer integrations.

**Exit:** matched Original reference captures and draw-state/attribute comparisons, above/below-water synthetic tests, native/higher resolution and multiple MSAA levels, and all existing renderer tests passing. No enhanced water passes/effects run in the baseline. Measure any added baseline CPU/GPU costs on this machine.

### 4. Add the Shipwright cartridge-water display-list bridge

Represent the extracted draw state and generated water vertices as native Shipwright commands/resources. Have `DisplayListTranslator` lower them into its RDRAM arena, resolving texture, vertex, matrix, segment, and nested display-list references. Test the donor water microcode/command semantics against Shipwright's supported path; translate to equivalent supported commands or add targeted support where necessary. Verify endian layout, texture resource lifetime, arena limits, command sizes, and complete state restoration after the water draw.

The owned snapshot carries previous/current simulation times and generations, mean level/domain identity, vertex positions and cartridge-generated attributes, and camera/environment inputs. Copy data while its owning game-frame resources are valid and retain it through asynchronous workloads and interpolated submissions. Use a unique frame generation/sequence rather than a reused raw display-list pointer as the sole identity. Include all affected state in translation-cache identity or bracket explicitly outside cached lists.

If custom water interpolation metadata is needed, add a small begin/end command with explicit translation. Reusing the donor's 1584-byte v2 material at quality zero is acceptable if it is the simplest verified solution, but is not a mandatory ABI. Clear any metadata after water and on reset/error paths so it cannot affect Link, effects, equipment previews, or HUD. Do not introduce enhanced profile, wake-field, or spray payload requirements.

**Exit:** translator tests verify cartridge command/texture/attribute equivalence, no state leakage, repeated cached lists, resource lifetime, arena bounds, and retranslation at different interpolation fractions. A synthetic ordinary draw immediately after water is unchanged.

### 5. Replace every inventoried Lake Hylia surface

Implement `LakeWaterSystem`, a C-callable interface for actor/query code, and mesh submission. At the water-plane draw, emit the replacement and skip the corresponding original surface only after cartridge resources/draw state are ready. Replace inventoried horizontal river polygons using explicit resource/range patches. Preserve other room geometry, ground caustic textures, gates, lock, and ice block. Adapt the inventoried cascade material as needed.

Drive mean levels from the live actor/water-box state, accounting for separate low-mesh origins and transient refill/lowering offsets. Advance simulation on Zelda's active game timeline, with a fixed 20 Hz donor tick accumulator; no enhanced 60 Hz GPU wake/foam simulation is required. Do not step from draw calls or monitor refresh. Freeze coherently on pause, handle fast/slow game timing and frame stepping, and reset history on scene changes, teleports/camera cuts, save loads, and discontinuous level/domain changes. Continuous refilling must not reset every frame.

Use fixed vertex identities; if LOD or clipped topology changes, interpolate by world XZ or reject incompatible history. Keep coarse and fine domains in separate transforms so coarse triangles cannot supply the near-surface's previous height. Do not transplant the donor's camera-following extension ring and its documented shimmer.

**Exit:** all lake surface states show animated water, no original flat overlays remain, no seams/land flooding, correct refilling, and no change to other scenes. Original Zelda rendering remains available as an A/B mode and a resource-failure fallback.

### 6. Integrate Zelda contact behavior

In `WaterBox_GetSurfaceImpl`, preserve original box selection, strict bounds, room filtering, property bits, pointer identity, lighting/camera metadata, and return semantics; substitute sampled height only for eligible Lake Hylia domains. In `WaterBox_GetSurface2`, use sampled height for its vertical-distance test while preserving its index/pointer contract. Audit `func_800425B0` and all direct `waterBox->ySurface` consumers before deciding which need the same adapter. Other scenes and special water-box cases remain unchanged.

Step/publish the water state before its first gameplay consumer. Review `Play_Update` actor/camera ordering and water-level actor updates; ensure one committed mean/displacement state per update, with no partial-state queries. Keep Zelda's swim state machine and gravity/buoyancy rules. Do not transplant jet-ski dynamics or create a solid collision floor at the surface.

Audit Link's swim/dive thresholds, water entry splashes, ripple spawn heights, floating objects, fish, thrown signs, and the temple lock's hard-coded float heights. Reposition water-contact effects through the shared sampler. For camera water classification, test crest/trough transitions; introduce narrowly scoped hysteresis only if needed to prevent repeated above/below-water flicker. Preserve Iron Boots sinking, underwater timer/tunic logic, bottom collision, ledge exits, hookshot behavior, and scene entrances. Default wave amplitude must stay within a tested envelope for those mechanics.

Keep Zelda's splash, bubble, and ripple events functional and aligned with the shared surface. If the selected cartridge graphics depend on original disturbance stamping or contact-driven attributes, adapt only those original mechanisms to relevant Zelda wet contacts, with swimmer-sized inputs. Do not install the recomp's enhanced four-tile wake/foam fields or added spray. Ordinary Zelda effects need not become Wave Race racing effects.

**Exit:** Link and wet objects meet the visible surface, every existing water mechanic works, and gameplay outside Lake Hylia is unchanged. Visual-only water with flat contact height is an intermediate diagnostic, not the completed feature.

### 7. Tune appearance, validate, and package

Start from the selected course's extracted Original graphics and actual wave parameters. Adapt camera-dependent highlights and Zelda day/night/fog/environment inputs using the cartridge visual model. Keep the original texture/combiner/color/transparency behavior as the appearance reference; document any lake-specific deviation. Verify underwater views, rainy/dark conditions where available, banks, bridge supports, and silhouettes. Do not tune modern material profiles or introduce modern absorption, refraction, SSR, foam, or spray.

Provide an in-game comparison toggle between Original Wave Race replacement and original Zelda water. Once validation passes, enable Original Wave Race water for Lake Hylia on the RT64 path; do not leave it permanently hidden behind a debug flag. Enhanced quality/style controls are outside this baseline. Unsupported backends or missing generated data fall back coherently to original Zelda rendering and flat queries together. Reset simulation/history on toggling. Save states must serialize the adapter state or perform a documented deterministic rebuild without mismatched samples.

Build integration belongs in Shipwright and any changed libultraship/RT64 commits with updated submodule pointers. Stage locally extracted cartridge water assets and provide ROM generation/cache validation. Keep ROM/generated proprietary outputs and capture fixtures excluded from distributed source. Update building/validation documentation with actual commands, results, screenshots, limitations, and provenance.

**Exit:** complete validation matrix, recorded performance and memory costs, clean startup/shutdown and toggling, repeatable build from supplied ROM, and no outstanding water-function regressions.

## Validation matrix

| Area | Required evidence |
| --- | --- |
| Cartridge appearance | Donor Quality::Original reference captures; texture, generated UV/highlight/color/alpha, combiner, fog, transparency and depth/state parity; prove enhanced passes/effects are inactive. |
| Simulation authenticity | Original versus adapter deterministic field/mesh fixtures; correct nonflat height conversion, boundaries, signed arithmetic, sources, and fixed-rate stepping. |
| Lake states | Child full lake; adult drained; adult restored; refill cutscene; randomizer lowering/raising; repeated leave/re-enter and save reload. |
| Zelda mechanics | Swim idle/motion, enter/exit from shore, jump/dive, scale upgrades, Iron Boots sink/remove, underwater timer/tunic, bottom walking, temple gate/lock/hookshot, fish/signs, bottle/item interactions, entry/warp transitions. |
| Surface consistency | Debug height markers at Link, effects and camera; CPU mesh-height agreement; interpolated Link/water alignment; seam and shoreline captures at crest/trough and moving level. |
| Rendering | Above/below surface; day/night; foreground silhouettes; islands/bridges; full-lake distant views; nonwater geometry/HUD; pause/equipment framebuffer capture. |
| Timing/lifecycle | Same simulated elapsed time at 20/30/60/120 presentation FPS; pause, frame-step, camera cut, teleport, scene destruction, toggle, save-state restore, repeated shutdown. |
| Vulkan resources | Native and enlarged resolution, MSAA off/2x/4x/8x where supported, resize and repeated AA changes, GPU validation, no stale or missing transforms during framebuffer flush. |
| Performance | Water CPU step/mesh/upload and GPU pass timings, median/p95; first-use stall; memory use; Original Wave Race versus original Zelda costs at equal camera/tick/resolution. |

Retain the existing `rt64_translator_test`, `rt64_texture_patches_test`, and desktop Vulkan `rt64_renderer_smoke` checks. Use `cmake --build --preset linux-clang-rt64 --parallel 8` and `ctest --preset linux-clang-rt64` after relevant changes. Add meaningful simulation/bridge/renderer tests, and deterministic gameplay capture fixtures keyed to game ticks; the current `SHIP_RT64_TEST_KEYS` scheduling alone is insufficient to prove swimming correctness. Manual interaction/playthrough checks supplement those fixtures.

The implementation order is 1 → 2 → 3 → 4 → 5 → 6 → 7. Simulation parity and Original/cartridge rendering equivalence are the first go/no-go milestones. Do not declare completion after the first attractive lake screenshot.

## Source map

Local sources supporting this plan:

- [Donor game internals](../../wave-race-64-recomp/docs/GAME-INTERNALS.md): wave field, simulation window, generators, normalized query, original mesh and aliasing.
- [Donor water implementation](../../wave-race-64-recomp/docs/WATER-IMPLEMENTATION.md) and [porting recipe](../../wave-race-64-recomp/docs/WATER.md): material ABI, passes, costs, failure modes.
- [RT64 water patch](../../wave-race-64-recomp/tools/patches/rt64-water.patch) and [follow-up patch script](../../wave-race-64-recomp/tools/patch_rt64_water.py): enhanced renderer source and reusable interpolation corrections; the enhanced renderer is outside the baseline.
- [Donor material builder](../../wave-race-64-recomp/src/water.cpp), [display-list rewriter](../../wave-race-64-recomp/src/dlrewrite.cpp), [ring sampler](../../wave-race-64-recomp/src/waterring.cpp), and [ROM generator](../../wave-race-64-recomp/tools/generate_game.py).
- [Lake actor](../soh/src/overlays/actors/ovl_Bg_Spot06_Objects/z_bg_spot06_objects.c), [object extraction map](../soh/assets/xml/N64_NTSC_10/objects/object_spot06_objects.xml), and [scene draw configuration](../soh/src/code/z_scene_table.c).
- [Water-box queries](../soh/src/code/z_bgcheck.c), [actor water contact](../soh/src/code/z_actor.c), [player behavior](../soh/src/overlays/actors/ovl_player_actor/z_player.c), [camera](../soh/src/code/z_camera.c), and [game update/draw order](../soh/src/code/z_play.c).
- [Native display-list translator](../libultraship/src/fast/rt64/DisplayListTranslator.cpp), [renderer submission](../libultraship/src/fast/rt64/Renderer.cpp), [existing tests](../tests/rt64/CMakeLists.txt), and [current validation record](RT64_VALIDATION.md).

Unresolved until phase 1/2: exact simulation dependency closure, extracted lake footprint, preferred donor wave/course parameters, coverage partition, and calibrated scale/amplitude. Unverified until runtime work: cartridge draw-state compatibility, Original appearance parity, underwater surface rendering, gameplay threshold stability, and Vulkan baseline water cost on this machine. These are explicit implementation investigations, not assumed successes.
