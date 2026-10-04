# Wave Race cartridge water assets

The local extractor reads the supplied Wave Race 64 USA Rev A ROM (SHA-1
`508dfc2d4caa42b6f6de5263d0aed5e44ac7966a`) and writes generated files under
`Shipwright/build/cartridge-water/`:

```sh
python Shipwright/tools/extract_wr64_cartridge_water.py \
  "Wave Race 64 (U) (V1.1) [!].z64"
```

The output includes the one-player setup list `0x01008290`, water mesh list
`0x0100B590`, a JSON provenance/state manifest, and both courses' actual
32×32 RGBA32 shading maps. Enabled appearance uses
`sunny_beach_water_32x32.rgba`, selected by `D_800DC0E4[1] -> D_800DB864`
and decoded from ROM MIO0 `0x3620B0..0x362DE0`, into segment 8 offset `0x400`.
Its SHA-256 is `1eeaaee952802068577aee6bd653e4b9a6d1a15c2f8446a56dd3784ddd5ca02a`.
Segment 1 is mapped using the two original DMA copies: ROM `0xF6090` starts
segment `0x01000000`; ROM `0xFE320` starts segment `0x01008290`.
The per-frame water vertices come from segment 3, so a static list alone is
not a complete drawable mesh.

`sunny_beach_wave_sources.bin` holds three evaluated 64-byte secondary records
from course 1, normal profile, difficulty 0; there are no primary records.
SHA-256: `4983ad6e964add16199e4cfb632f5bc8155f5f5eff5ae2415bb97969d33171c5`.
The extractor executes the original sine initializer and source-loader block;
all three source updates match the ROM executor over 128 ticks. The adapter
places 27 records in nine repeated course layouts across Lake Hylia's fixed
domain. The three original patches are disjoint; repeating them as one layout
preserves their relative positions and phases. Independently tiling each
record everywhere was removed because it introduced hard-set overlap. Drake Lake's profile-8
source and texture remain available separately for regression provenance.

`sunny_beach_body.gfx.bin` and `sunny_beach_body.vtx.bin` preserve the original
course-background draw: `D_106F8E8`, four vertices at Y `-100`, RGBA
`(0,242,228,255)`, SHADE combiner and render-mode word `0x00552230`.
These provenance files are not needed by the runtime. The enabled adapter
uses the separate Original body and translucent highlight materials. The
course body is fitted to Zelda's horizontal wet footprints and live tier
levels with the original -100 offset. Its render word writes depth, while
highlights compare depth without writing it. The former opaque composite
remains only as a renderer test fixture; it obscured submerged actors.
The initial claim that a separate body necessarily leaves Zelda's lake bed
visible came from captures with an incorrectly packed identity matrix and
was not a valid comparison.

The water builder `func_80050204` generates interior UVs from three adjacent
wave samples. For `a=A>>7`, `b=B>>7`, `c=C>>7`, it computes
`dx=(a-b)*55`, `dz=(((a+b)>>1)-c)*64`, `k=992/sqrt(dx²+dz²+0xC00000)`, then
`S=1024+trunc(dz*k)` and `T=1024+trunc(dx*k)`. Vertex height uses the signed
fixed-point field's arithmetic `>>8` plus the current mean. The verified stencil
at `(u,v)` is `A=(u-1,v-1)`, `B=(u,v+1)`, `C=(u+1,v)`. The ROM-backed parity
test compares all 397 interior vertices at five camera positions. Edge UV rules remain
part of the donor mesh function and need to be carried over with its original
row topology.

This is a cartridge Original asset path. It does not include enhanced water
profiles, foam, refraction, or shader lighting. Generated ROM-derived files
remain local and are not source assets.
