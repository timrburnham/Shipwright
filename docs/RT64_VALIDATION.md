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
