# RT64 Linux validation

Validated on 2026-10-02 with Ship of Harkinian 9.2.3 on `dev`, Clang
22.1.8, and an AMD Radeon RX 9060 XT using RADV 26.2.3. The RT64 base
revision is `4337374`; libultraship is based on `fdcaf633`. Both submodules
contain integration changes that must be saved with the parent changes.

The full `linux-clang-rt64` build succeeded. The three RT64 tests each passed
ten consecutive runs using:

```sh
cmake --build --preset linux-clang-rt64 --parallel 8
ctest --preset linux-clang-rt64 --repeat until-fail:10
```

The renderer test ran on the desktop Vulkan GPU without being skipped. It
checks native triangles and fixed-point matrices, decoded depth, grayscale,
GUI presentation, framebuffer copies/readback/texturing, 2x resolution,
4x MSAA, their combination, and repeated MSAA changes. The CPU tests check
native/OTR command translation and texture patches. The repeated runs cover
the shader worker startup/shutdown fix added after an intermittent MSAA
switching hang was observed.

The app extracted `oot.o2r` from the supplied USA ROM; `GenerateSohOtr`
generated `soh.o2r`. Application checks rendered the ship logo, a 1,800-frame
opening/title sequence, Kokiri gameplay with the HUD, file select, and the
pause inventory with its captured gameplay background. These bounded runs
exited successfully. A further 120-frame run after the shader-cache fix
also exited successfully.

Local evidence is under `build/linux-clang-rt64`: `rt64-final-build.log`,
`rt64-final-ctest.log`, `title-final.png`, `kokiri-smoke/kokiri.png`,
`file-select-smoke/file-select.png`, and `pause-smoke/pause.png`. The last
application log is `soh/rt64-rebuilt-game-test.log`. These build artifacts
and the extracted game archives are excluded from version control.

This is targeted renderer and scene validation, not a complete playthrough.
The logical surface currently remains 320x240 at 4:3. Custom GLSL and indexed
display-list commands, and masks for scaled high-resolution replacement
textures, remain unsupported. See [BUILDING.md](BUILDING.md#linux-clang-build-with-rt64)
for launch instructions and the bridge's supported behavior.

## Rendering fixes after screenshot review

The reported black character skin, solid pink equipment preview, and gray
Kokiri walls exposed compatibility defects in palette-mode translation,
full/partial render-mode synchronization, depth-clear target selection,
and framebuffer texture binding. Point-light packing was also corrected.
The bridge now resolves the equipment framebuffer before returning to the
main target, preserves cached RSP vertex/transform/light data across private
readback synchronization, and uploads the whole preview rather than using
a partial TMEM framebuffer copy.

A 120-frame Kokiri run, a 107-frame run turning Link toward the camera, and
a 360-frame equipment run (`SHIP_RT64_TEST_KEYS=60:Space,240:Z`) exited
successfully. Visual inspection confirmed colored skin, olive wall tint,
and a complete equipment model. Evidence: `kokiri-smoke/kokiri-color-fixes.png`,
`kokiri-smoke/kokiri-face-fixes.png`, and
`equipment-smoke/equipment-after.png`, under `build/linux-clang-rt64`.

The final full Linux/Clang build succeeded and all three CTest targets passed,
including the desktop Vulkan test at 2x resolution with 4x MSAA. Added checks
cover directional/point lights, fog, full/partial other-mode synchronization,
offscreen depth clearing, and the equipment quad loaded before framebuffer
binding. CPU checks cover raw and shifted palette modes and framebuffer
invalidate/flush ordering. Final logs: `rt64-render-fixes-build.log` and
`rt64-render-fixes-ctest.log`.


## HUD clipping and lighting stability fixes (2026-10-03)

The static hearts and pause-screen A icon use native textured geometry with
`G_TP_NONE`. RT64 simulates a hardware half-UV correction in this mode,
while Ship expects the native UV range. The native RSP now compensates in
floating point and keeps the original sampling bounds; signed 16-bit UV
extremes remain intact. Modified vertex UVs use the same conversion.

Directional lights can contain uninitialized padding bytes. The bridge
previously treated a nonzero padding byte as a positional-light layout,
which corrupted directional vectors as frame allocations were reused.
Translation now preserves the directional view and RT64 separately decodes
native positional records. The geometry point-lighting bit selects the
layout when vertices are loaded, ambient lights stay ambient, and changing
point-lighting mode refreshes the cached encoding.

The full Linux/Clang build and all three tests passed. The Vulkan test
compares matching IA8 icon quads under both perspective modes and repeats
lighting draws with alternating nonzero directional padding bytes, checking
identical pixels. Existing preview, point-light, fog, framebuffer, resolution,
and MSAA checks still pass. Logs: `rt64-hud-flicker-build.log` and
`rt64-hud-flicker-ctest.log`, under `build/linux-clang-rt64`.

Isolated 60-FPS Kokiri gameplay captures from frames 100 through 120 showed
complete hearts and stable terrain lighting. A 360-frame run with
`SHIP_RT64_TEST_KEYS=180:Space,300:C` verified the complete A icon and
"to Decide" text in the save prompt. Images are
`hud-flicker-smoke/hud-final.png` and
`hud-flicker-smoke/decide-final2.png` under the build directory. A further
child-Link warp check exited normally. The exact crawling camera movement
in the supplied screencast has not been replayed; the lighting regression
covers the identified padding-driven instability.

## Whole-image title logo (2026-10-03)

The title screen loads its 160x160 RGBA32 logo as one image, exceeding the
N64's 4 KiB texture memory. Canonical RGBA32 resources larger than TMEM now
register a native image when loaded with an explicit image width. A previous
small alias of the same resource cannot bypass that registration. Native
images also bypass RT64's raw-TMEM overflow fallback, which otherwise samples
the zero-filled compatibility allocation instead of the full image.

A bounded 1200-frame title run on desktop Vulkan verified the complete Zelda
logo, shield, and sword. Evidence: `title-logo-smoke/title-logo-fixed.png`
under `build/linux-clang-rt64`. The Linux/Clang build and all three tests pass;
the translator regression covers the small-alias-to-whole-image transition.
The Vulkan regression also renders both halves of a 160x160 native framebuffer
image, verifying samples beyond the 4 KiB TMEM boundary.
Logs: `rt64-title-logo-build.log` and `rt64-title-logo-ctest.log`.

## SoH graphics settings and C-button symbols (2026-10-03)

RT64 now receives the GPU dimensions and presentation rectangle computed by
SoH's existing graphics settings. Aspect ratio Off fills the game window;
forced ratios, the internal resolution multiplier, fixed vertical resolution,
and integer scaling use the same calculations as the Fast3D path. Native
framebuffer storage and readback remain 320x240 for game effects and pause
backgrounds, independently of the GPU resolution.

Widescreen rectangles retain signed coordinates rather than wrapping through
12-bit RDP fields. Framebuffer clears are clipped to Vulkan attachment bounds,
and native HUD rectangle scissors preserve the full widescreen output. Negative
pause-background image rectangles are also preserved. Projection decomposition
now handles an axis-aligned camera without division by a zero axis component.

The 46x16 IA8 C-button symbols use a rounded six-word render tile line but the
legacy LOADBLOCK macro supplies a five-word DXT. The native translator corrects
DXT for recognized texture-block sequences using the render tile stride.
Manual texture loads retain their explicit DXT.

The Linux/Clang build and all three RT64 tests pass. New regressions cover the
unaligned IA8 stride, signed wide rectangles, negative image rectangles,
widescreen depth clears/HUD scissors, and axis-aligned projection decomposition.
Desktop Vulkan runs verified aspect Off, forced 4:3, fixed 240p and 480p,
window resolution at 100%, and manual 1x integer scaling. The pause screen
opens normally in widescreen and the three C symbols before "to Equip" render
correctly. Evidence under `build/linux-clang-rt64`:

- `graphics-off/equip-final-window.png`
- `graphics-43/validated-window.png`
- `graphics-wide480/validated-window.png`
- `graphics-window100/validated-final-window.png`
- `graphics-integer/validated-final-window.png`

Build/test logs: `rt64-graphics-settings-build.log` and
`rt64-graphics-settings-ctest.log`. This validation does not establish support
for every other graphics control, such as VSync or match-refresh pacing.

## Pause snapshots and prerendered scenes (2026-10-03)

Framebuffer copies used for pause backgrounds now retain the display
workload's complete GPU image, including widescreen framing and rendering
resolution. The native 320x240 RDRAM copy remains available for game CPU
readbacks. Native image rectangles bind that complete image directly rather
than rebuilding it from TMEM-sized strips. A GPU stripe regression checks that
copying and redrawing a widescreen framebuffer leaves its horizontal boundaries
in place; stretching the native center slice fails this check.

Rooms with image meshes and panoramic background skyboxes automatically use a
4:3 render and presentation aspect. This includes child-era Market and the
prerendered interiors, while ordinary 3D rooms keep the user's aspect setting.
The rule also applies with advanced resolution settings disabled or aspect
correction otherwise ignored. It does not change saved graphics preferences.

The Linux/Clang build and all three RT64 tests pass. Desktop Vulkan runs
verified full-resolution widescreen pause imagery in Kokiri Forest and automatic
pillarboxing in child-era Market. Evidence under `build/linux-clang-rt64`:
`pause-background/validated-gameplay.png`,
`pause-background/validated-pause.png`, and
`market-background/child-fixed-gameplay.png`.
Logs: `rt64-background-build.log` and `rt64-background-ctest.log`.

The initial implementation added image-to-buffer copy support in the nested
Plume submodule. That temporary modification has since been reverted; see
"Pause capture with unmodified Plume" below. No Plume fork or patch is required.

## Frozen pause-background stability (2026-10-03)

The supplied `Screencast_20261003_185221.webm` shows small temporal RGB
changes in the frozen scene. `FB_DrawFromFramebuffer` enabled `G_CD_NOISE`
on every redraw, causing RT64 to add fresh post-blend dither noise to the
already captured image. RT64 pause-background draws now disable color and
alpha dithering; other framebuffer effects retain their existing modes.

A bounded desktop Vulkan pause run captured four frames one second apart.
The sampled ground and central background regions were pixel-identical across
all four frames. A top-wall region differed by at most one channel value,
with a mean temporal spread below 0.004. Menu animations remain active.
Evidence: `pause-background/stable-1.png` through `stable-4.png` under
`build/linux-clang-rt64`. Build and test logs:
`rt64-pause-stability-build.log`, `rt64-pause-stability-ctest.log`.

## Action-button interpolation isolation (2026-10-03)

The A button and its label are transformed 3D quads. Their matrix operations
previously lived in the root frame-interpolation recording alongside unrelated
draws, making their old/new operation matching depend on drawing order. Each
now has a stable named interpolation child, preserving the intended label-flip
animation while separating its history from world rendering.

The Linux/Clang build and all three RT64 tests pass. A 60 FPS child-era Hyrule
Field run completed normally and captured 63 consecutive frames under
`action-button/button-fixed.*.ppm`. The user's violent shaking has not been
reproduced in this stationary fixture, so this addresses the identified
interpolation-history risk without establishing that it was the sole cause of
the reported behavior. Logs: `rt64-action-button-build.log` and
`rt64-action-button-ctest.log` under `build/linux-clang-rt64`.

## Action-button fixed-point quantization (2026-10-03)

The follow-up `Screencast_20261003_191333.webm` established that interpolation
isolation alone did not resolve the reported shaking. Consecutive native
framebuffer captures showed a repeating three-frame pattern in the blue
button edges even with a stationary model transform. The translator rounded
float matrix replacements to 16.16, whereas `guMtxF2L` truncates toward zero.
For example, the HUD orthographic X scale 0.00625 becomes 409 natively but
410 with rounding. Alternating original and interpolated draws therefore
changed the button's horizontal projection and texture sampling.

Replacement matrix conversion now truncates identically to native conversion.
A regression test checks positive and negative fractional fixed-point units.
No projection-scope experiment or temporary matrix diagnostics remain.
A bounded desktop Vulkan run at 60 FPS captured 62 consecutive frames in
`graphics-43/button-quantization.*.ppm`. Opaque blue button pixels were
pixel-identical across the entire sequence (maximum RGB difference zero),
compared with a maximum channel difference of 17 before the conversion fix.
The surrounding crop's mean consecutive-frame difference dropped from
0.282 to 0.036; its remaining differences include animated scenery.

The Linux/Clang build and all three RT64 tests, including the Vulkan smoke
test, pass. Build log: `rt64-hud-quantization-build-final.log`.

## Pause capture with unmodified Plume (2026-10-03)

Full-resolution pause capture now reuses RT64's existing `fbWriteColor` compute
shader. It samples the resolved display texture at one source pixel per output
pixel, writes RGBA32 into a formatted GPU buffer, and copies that buffer into
CPU-readable memory through Plume's existing buffer-copy API. Shader output is
already RGBA byte order, so the capture no longer requires texture readback,
row-pitch padding, or a separate CPU conversion for 16-bit channels. The
existing persistent framebuffer snapshots and native game RAM readback remain
in place. No new shader or RT64 shader-library change was necessary.

All three RT64 tests passed before reverting Plume. The single temporary
`plume_vulkan.cpp` modification was then reverted to the recorded revision.
A fresh Linux/Clang build and all three tests, including the Vulkan smoke test,
passed again against unmodified Plume. Its worktree and parent submodule diff
are clean. Logs: `rt64-compute-capture-build.log`,
`rt64-compute-capture-ctest.log`, `rt64-stock-plume-build.log`, and
`rt64-stock-plume-ctest.log` under `build/linux-clang-rt64`.

A bounded desktop Vulkan game run captured the widescreen pause menu at
1280x720. Ground, visible background through the menu, and wall samples were
pixel-identical between two captures one second apart. Evidence:
`pause-background/compute-pause-1.png` and `compute-pause-2.png`.

## Bow crash with missing overflow texture (2026-10-04)

The reported bow crash dump identifies an uncaught texture-load exception for
`__OTR__objects/gameplay_keep/gHilite2Tex_Overflow`. The loaded archive lacks
that asset, but the authentic graphics patch installed it in both near and far
arrow display lists. Some ROM extraction definitions omit this texture.

The arrow patch now uses a complete, correctly sized 16x16 load of `gHilite2Tex`
when the overflow resource is unavailable. Archives containing the overflow
texture still use it with `FixTexturesOOB` disabled. The corrected load replaces
all eight original command slots, including the two-slot OTR image command;
switching back to the overflow path restores all eight slots.

The Linux/Clang build and CPU regressions passed, and the Vulkan renderer smoke
test passed on the desktop GPU. An isolated adult Lake Hylia run completed 900
frames normally, with five released shots, 35 near-model draws, and 315 far-model
draws. Saves and settings were isolated from the normal game. Horseback and
enemy-targeting interactions were not separately replayed.

The manual GDB regression checks successful process exit, multiple released
shots, and both model distances. From the repository root, with the normal
keyboard Left-to-C-left mapping and extracted game archives available:

```sh
python3 tools/capture_lake_water.py --output build/linux-clang-rt64/bow-smoke --age adult --water zelda --prepare-only
SHIP_HOME="$PWD/build/linux-clang-rt64/bow-smoke" \
SHIP_RT64_TEST_FRAMES=900 \
SHIP_RT64_TEST_KEYS='60:Space,180:Left,240:Left,300:Left,360:Left,420:Left,480:Left,540:Left,600:Left' \
gdb -batch -x tests/rt64/bow_smoke.gdb --args build/linux-clang-rt64/soh/soh.elf
```

Evidence: `bow-smoke/launch.log`, `rt64-bow-build-final.log`,
`rt64-bow-ctest.log`, and `rt64-bow-vulkan-ctest.log` under the build directory.

## Fishing Pond ripple vertex repair (2026-10-04)

The reported pond-entry crash is an uncaught vertex-load exception. The archive's
`gFishingRippleModelDL` contains `0x32000000, 0` in place of its vertex load,
so the translator reads the following triangle as a nonexistent resource hash.
The original USA ROM instead loads four vertices from gameplay_keep at
`0x0401A5C0`, shared with `gEffShockwaveDL`. That vertex resource is already
present in the archive and correctly referenced by the shockwave display list.

The authentic graphics patches now recognize the exact malformed ripple model
and replace only its vertex command with a native load of those shared vertices.
The triangles remain intact, and an owned vertex copy survives resource cache
eviction. Valid models and custom display lists are excluded from the repair.
The extracted archive does not need to be replaced.

The Linux/Clang build and all four targeted RT64 renderer/CPU tests passed,
including the desktop Vulkan smoke test. An isolated adult pond run completed
360 frames normally and drew the ripple model 666 times. A child pond run also
completed 360 frames normally, with 831 ripple draws. The manual regression
in `tests/rt64/fishing_smoke.gdb` checks the pond scene, actual ripple draws, and
successful process exit. Use isolated debug-warp settings with current scene
25 (Lake Hylia Buildings), entrance 1 (Fishing Pond), a 360-frame RT64 limit,
and `SHIP_RT64_TEST_KEYS=60:Space`.

Evidence under the build directory: `fishing-smoke/launch.log`,
`fishing-smoke/frame.png`, `fishing-child-smoke/launch.log`, `rt64-fishing-build-final.log`, and
`rt64-fishing-ctest.log`. This verifies entry and ripple rendering; the complete
fishing minigame has not been played through.
