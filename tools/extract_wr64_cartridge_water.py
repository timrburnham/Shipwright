#!/usr/bin/env python3
"""Extract the one-player cartridge water display list from a Rev A ROM.

The ROM image is private input. Outputs are local generated assets under
Shipwright/build/cartridge-water by default and should not be committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from verify_wr64_water import extract_drake_source
import struct
import sys

ROM_SHA1 = "508dfc2d4caa42b6f6de5263d0aed5e44ac7966a"
SEGMENT_ROM_BASE = 0xF6090  # codeseg_ROM_END in sys_main.c; first asset range
SEGMENT_SPLIT_VA = 0x01008290
SEGMENT_SPLIT_ROM = 0xFE320  # second DMA source begins at D_1008290
DL_ADDRESSES = {"water_0100B590": 0x0100B590, "setup_01008290": 0x01008290}
SEGMENT_END = 0x0106FB70
MAIN_VRAM_ROM_DELTA = 0x80045800
COURSE_TEXTURE_TABLE_VA = 0x800DC0E4
COURSE_WATER_TABLE_VA = 0x800DB9E4
COURSE_ID_DRAKE_LAKE = 4
COURSE_ID_SUNNY_BEACH = 1
WATER_TEXTURE_OFFSET = 0x400
COURSE_TEXTURES = (("drake_lake", "Drake Lake", COURSE_ID_DRAKE_LAKE),
                   ("sunny_beach", "Sunny Beach", COURSE_ID_SUNNY_BEACH))
SUNNY_WAVE_SOURCE_BYTES = 192
SUNNY_WAVE_SOURCE_SHA256 = "4983ad6e964add16199e4cfb632f5bc8155f5f5eff5ae2415bb97969d33171c5"
SUNNY_BACKGROUND_DL = 0x0106F8E8
COURSE_BACKGROUND_VERTEX_TABLE = 0x800DAA90
SUNNY_BODY_COLOR = [0, 242, 228, 255]
SUNNY_BODY_LOCAL_Y = -100


def segment_va_to_rom(va: int) -> int:
    """Map the split segment-1 image using the two DMA copies in sys_main.c."""
    if not 0x01000000 <= va <= SEGMENT_END:
        raise ValueError(f"address {va:#x} is outside the verified segment-1 image")
    if va < SEGMENT_SPLIT_VA:
        return SEGMENT_ROM_BASE + va - 0x01000000
    return SEGMENT_SPLIT_ROM + va - SEGMENT_SPLIT_VA


def extract_segment_display_list(rom: bytes, address: int):
    rom_offset = segment_va_to_rom(address)
    rom_limit = segment_va_to_rom(SEGMENT_END)
    commands = bytearray()
    for index in range((rom_limit - rom_offset) // 8):
        offset = rom_offset + index * 8
        if offset + 8 > len(rom) or offset + 8 > rom_limit:
            raise ValueError(f"display list at {address:#x} ran past its verified segment range")
        word0, word1 = struct.unpack_from(">II", rom, offset)
        commands.extend(struct.pack(">II", word0, word1))
        if word0 >> 24 == 0xB8:  # F3D/F3DWAVE G_ENDDL
            return rom_offset, bytes(commands)
    raise ValueError(f"no G_ENDDL found in display list {address:#x} before segment end")


def extract_course_texture(rom: bytes, main_va_to_rom, course_id: int, course_name: str):
    """Resolve one-player D_800DC0E4[course] and decode segment-8 +0x400."""
    table_rom = main_va_to_rom(COURSE_TEXTURE_TABLE_VA) + course_id * 4
    course_table_va = struct.unpack_from(">I", rom, table_rom)[0]
    course_table_rom = main_va_to_rom(course_table_va)
    water_entry = None
    for i in range(32):
        entry_rom = course_table_rom + i * 16
        rom_start, rom_end, flag, dst_offset = struct.unpack_from(">IIII", rom, entry_rom)
        if (rom_start, rom_end, flag, dst_offset) == (0, 0, 0, 0):
            break
        if dst_offset == WATER_TEXTURE_OFFSET:
            if water_entry is not None:
                raise ValueError(f"{course_name} DMA table has duplicate segment-8 +0x400 entries")
            water_entry = {"entry_rom": entry_rom, "rom_start": rom_start,
                           "rom_end": rom_end, "flag": flag, "dst_offset": dst_offset}
    if water_entry is None:
        raise ValueError(f"{course_name} one-player DMA table has no segment-8 +0x400 entry")
    if water_entry["flag"] != 1:
        raise ValueError(f"{course_name} water texture DMA flag is {water_entry['flag']}, expected MIO0 flag 1")
    if water_entry["rom_end"] > len(rom) or water_entry["rom_start"] >= water_entry["rom_end"]:
        raise ValueError(f"{course_name} water texture DMA source lies outside the ROM")
    compressed = rom[water_entry["rom_start"]:water_entry["rom_end"]]
    if compressed[:4] != b"MIO0":
        raise ValueError(f"{course_name} segment-8 +0x400 source is not an MIO0 block")
    expected_size, comp_pos, raw_pos = struct.unpack_from(">III", compressed, 4)
    if expected_size != 32 * 32 * 4:
        raise ValueError(f"{course_name}: expected 32x32 RGBA32 (4096 bytes), got {expected_size}")
    decoded = bytearray()
    bit_pos, comp_cursor, raw_cursor = 16, comp_pos, raw_pos
    while len(decoded) < expected_size:
        if bit_pos >= comp_pos:
            raise ValueError(f"{course_name} MIO0 layout stream ended early")
        bits = compressed[bit_pos]
        bit_pos += 1
        for shift in range(7, -1, -1):
            if len(decoded) >= expected_size:
                break
            if bits & (1 << shift):
                if raw_cursor >= len(compressed):
                    raise ValueError(f"{course_name} MIO0 literal stream ended early")
                decoded.append(compressed[raw_cursor])
                raw_cursor += 1
            else:
                if comp_cursor + 2 > len(compressed):
                    raise ValueError(f"{course_name} MIO0 back-reference stream ended early")
                token = struct.unpack_from(">H", compressed, comp_cursor)[0]
                comp_cursor += 2
                count, distance = (token >> 12) + 3, (token & 0x0FFF) + 1
                if distance > len(decoded):
                    raise ValueError(f"{course_name} MIO0 back-reference precedes output start")
                for _ in range(count):
                    decoded.append(decoded[-distance])
                    if len(decoded) == expected_size:
                        break
    return table_rom, course_table_va, water_entry, bytes(decoded)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "build/cartridge-water")
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    digest = hashlib.sha1(rom).hexdigest()
    if digest != ROM_SHA1:
        parser.error(f"ROM SHA-1 mismatch: expected {ROM_SHA1}, got {digest}")
    if rom[:4] != bytes.fromhex("80371240"):
        parser.error("expected a big-endian .z64 image")

    def main_va_to_rom(va: int) -> int:
        if va < MAIN_VRAM_ROM_DELTA:
            raise ValueError(f"address {va:#x} is outside the main segment")
        return va - MAIN_VRAM_ROM_DELTA

    args.out.mkdir(parents=True, exist_ok=True)
    texture_records = {}
    for texture_key, course_name, course_id in COURSE_TEXTURES:
        try:
            selector_rom, course_table_va, entry, pixels = extract_course_texture(
                rom, main_va_to_rom, course_id, course_name)
        except (ValueError, struct.error) as error:
            parser.error(str(error))
        filename = f"{texture_key}_water_32x32.rgba"
        (args.out / filename).write_bytes(pixels)
        texture_records[texture_key] = {
            "course": f"{course_name} (course ID {course_id})", "course_id": course_id,
            "segment_address": "0x08000400", "selector_table_rom": f"0x{selector_rom:08X}",
            "course_table_address": f"0x{course_table_va:08X}",
            "entry_rom": f"0x{entry['entry_rom']:08X}",
            "entry": {"rom_start": f"0x{entry['rom_start']:08X}", "rom_end": f"0x{entry['rom_end']:08X}",
                      "flag": entry["flag"], "segment8_offset": f"0x{entry['dst_offset']:X}"},
            "compression": "MIO0", "dimensions": [32, 32], "format": "RGBA32",
            "decoded_bytes": len(pixels), "sha256": hashlib.sha256(pixels).hexdigest(), "file": filename,
            "provenance": "D_8000400 is segment 8 +0x400; func_80095174 selects D_800DC0E4[courseID] for one-player and func_80095A28 loads the flag-1 MIO0 entry."
        }
    decoded = (args.out / "drake_lake_water_32x32.rgba").read_bytes()
    table_rom = int(texture_records["drake_lake"]["selector_table_rom"], 16)
    course_table_va = int(texture_records["drake_lake"]["course_table_address"], 16)
    water_entry = {"entry_rom": int(texture_records["drake_lake"]["entry_rom"], 16),
                   "rom_start": int(texture_records["drake_lake"]["entry"]["rom_start"], 16),
                   "rom_end": int(texture_records["drake_lake"]["entry"]["rom_end"], 16),
                   "flag": texture_records["drake_lake"]["entry"]["flag"], "dst_offset": WATER_TEXTURE_OFFSET}
    (args.out / "drake_lake_water_32x32.rgba").write_bytes(decoded)
    source_record = extract_drake_source(rom)
    (args.out / "drake_lake_wave_source.bin").write_bytes(source_record)
    try:
        from verify_wr64_water import extract_sunny_sources
        sunny_source_records = extract_sunny_sources(rom)
    except ImportError:
        sunny_source_records = None
    if sunny_source_records is not None:
        if len(sunny_source_records) != SUNNY_WAVE_SOURCE_BYTES:
            parser.error(f"Sunny Beach wave source must be {SUNNY_WAVE_SOURCE_BYTES} bytes, got {len(sunny_source_records)}")
        sunny_hash = hashlib.sha256(sunny_source_records).hexdigest()
        if sunny_hash != SUNNY_WAVE_SOURCE_SHA256:
            parser.error(f"Sunny Beach wave source SHA-256 mismatch: expected {SUNNY_WAVE_SOURCE_SHA256}, got {sunny_hash}")
        (args.out / "sunny_beach_wave_sources.bin").write_bytes(sunny_source_records)
    extracted = {}
    for name, address in DL_ADDRESSES.items():
        try:
            rom_offset, commands = extract_segment_display_list(rom, address)
        except ValueError as error:
            parser.error(f"{name}: {error}")
        filename = f"{name}.gfx.bin"
        (args.out / filename).write_bytes(commands)
        extracted[name] = {"address": f"0x{address:08X}", "rom_offset": f"0x{rom_offset:08X}",
                           "commands": len(commands) // 8, "file": filename}

    try:
        background_rom, background_dl = extract_segment_display_list(rom, SUNNY_BACKGROUND_DL)
        background_table_rom = main_va_to_rom(COURSE_BACKGROUND_VERTEX_TABLE) + COURSE_ID_SUNNY_BEACH * 4
        background_vertex_va = struct.unpack_from(">I", rom, background_table_rom)[0]
        background_vertex_rom = segment_va_to_rom(background_vertex_va)
        background_vertices = rom[background_vertex_rom:background_vertex_rom + 4 * 16]
        if len(background_vertices) != 4 * 16:
            raise ValueError("Sunny Beach background vertex array was truncated at segment end")
        if (background_vertex_va, background_rom, len(background_dl)) != (0x0106F930, 0x00165978, 0x48):
            raise ValueError("Sunny Beach background source does not match the verified four-vertex/9-command asset")
        (args.out / "sunny_beach_body.gfx.bin").write_bytes(background_dl)
        (args.out / "sunny_beach_body.vtx.bin").write_bytes(background_vertices)
        actual = [list(background_vertices[index * 16 + 12:index * 16 + 16]) for index in range(4)]
        positions = [list(struct.unpack_from(">hhh", background_vertices, index * 16)) for index in range(4)]
        if any(vertex[3] != SUNNY_BODY_COLOR[3] or vertex[:3] != SUNNY_BODY_COLOR[:3] for vertex in actual):
            raise ValueError(f"Sunny Beach base-plane RGBA mismatch: {actual}")
        if any(vertex[1] != SUNNY_BODY_LOCAL_Y for vertex in positions):
            raise ValueError(f"Sunny Beach base-plane local Y mismatch: {positions}")
    except (ValueError, struct.error) as error:
        parser.error(f"Sunny Beach body asset: {error}")

    manifest = {
        "rom_sha1": digest,
        "donor_revision": "wave-race-64-recomp 4926225b89a92ea4a2ee1cdf396cab03df9c7329",
        "reference_revision": "LLONSIT/Wave-Race-64 a51b38a2aaef68da10ea1e47247e70be3b1d4c70",
        "reference_config": "reference/wr64-decomp/waverace64.us.rev1.yaml",
        "rom_segment_mapping": {
            "vram_base": "0x01000000",
            "ranges": [
                {"vram": ["0x01000000", "0x01008290"], "rom_base": "0x000F6090", "source": "codeseg_ROM_END through the first sys_main.c DMA"},
                {"vram": ["0x01008290", "0x0106FB70"], "rom_base": "0x000FE320", "source": "second sys_main.c DMA; ends at ROM 0x165C00"}
            ]
        },
        "water_texture": {
            "course": "Drake Lake (course ID 4)",
            "segment_address": "0x08000400",
            "dma_selector": "func_80095174: one-player selects D_800DC0E4[courseID]; multiplayer selects D_800DC108[courseID]",
            "selector_table_rom": f"0x{table_rom:08X}",
            "course_table_address": f"0x{course_table_va:08X}",
            "entry_rom": f"0x{water_entry['entry_rom']:08X}",
            "entry": {"rom_start": f"0x{water_entry['rom_start']:08X}", "rom_end": f"0x{water_entry['rom_end']:08X}", "flag": water_entry["flag"], "segment8_offset": f"0x{water_entry['dst_offset']:X}"},
            "compression": "MIO0",
            "dimensions": [32, 32], "format": "RGBA32", "decoded_bytes": len(decoded),
            "sha256": hashlib.sha256(decoded).hexdigest(),
            "file": "drake_lake_water_32x32.rgba",
            "provenance": "D_8000400 is segment 8 +0x400; sys_main.c binds segment 8 to D_800D45F0 (base value 0x80316800). code_4F850.c:func_80095174 selects D_800DC0E4[courseID] when D_801CE608.player == 1; code_4F850.c:func_80095A28 loads this flag-1 entry through that one-player course table into segment 8 +0x400."
        },
        "course_textures": texture_records,
        "wave_source": {
            "file": "drake_lake_wave_source.bin", "bytes": 64,
            "sha256": hashlib.sha256(source_record).hexdigest(),
            "course": 4, "screen_profile": 8, "difficulty": 0,
            "selector": "D_800D68FC[4] (empty primary), D_800D6924[4] (one 26-byte secondary source)",
            "evaluation": "Original SysUtils_TaylorSeries and func_80071E70 loader block 0x8007239C..0x80072694",
            "update": "func_8004CC7C; big-endian evaluated 0x40-byte record",
        },
        "sunny_beach_wave_source": {
            "file": "sunny_beach_wave_sources.bin" if sunny_source_records is not None else None,
            "bytes": len(sunny_source_records) if sunny_source_records is not None else None,
            "sha256": hashlib.sha256(sunny_source_records).hexdigest() if sunny_source_records is not None else None,
            "course": 1, "profile": "normal/default", "difficulty": 0,
            "primary_count": 0, "secondary_count": 3,
            "selector": "D_800D68AC[1] + D_800D68D4[1]",
            "evaluation": "Original loader func_8007239C..func_80072694; Sunny default profile"
        },
        "display_lists": extracted,
        "sunny_beach_body": {
            "course_id": COURSE_ID_SUNNY_BEACH,
            "display_list_address": f"0x{SUNNY_BACKGROUND_DL:08X}",
            "display_list_rom": f"0x{background_rom:08X}",
            "display_list_file": "sunny_beach_body.gfx.bin",
            "display_list_commands": len(background_dl) // 8,
            "vertex_table_address": f"0x{COURSE_BACKGROUND_VERTEX_TABLE:08X}",
            "vertex_address": f"0x{background_vertex_va:08X}",
            "vertex_rom": f"0x{background_vertex_rom:08X}",
            "vertex_file": "sunny_beach_body.vtx.bin",
            "vertex_count": 4,
            "local_y": SUNNY_BODY_LOCAL_Y,
            "rgba": SUNNY_BODY_COLOR,
            "provenance": "func_80094A44 draws D_800DAA90[courseID] as a 4-vertex quad after D_106F8E8. Course 1 points to D_106F930. D_106F8E8 is the Original shade-combined, 1-cycle, Z-buffered translucent body material; vertices are the exact cyan base plane at y=-100. Segment-1 split mapping follows sys_main.c DMA from codeseg_ROM_END to FE320, then FE320 to 165C00."
        },
        "inherited_state": {
            "one_player_texture": "D_8000400 = segment 8 + 0x400, 32x32 RGBA32; Draw_WaterEffects loads it before the water list",
            "texture_scale": ["0x8000", "0x8000"],
            "environment_color": "(D_801CB160, D_801CB160, D_801CB160, D_800D9890)",
            "drake_lake_one_player_race": {
                "condition": "gCourseID == DRAKE_LAKE, gPlayers == ONE_PLAYER, D_801CE64C != 1",
                "cycle_type": "G_CYC_2CYCLE",
                "render_mode": ["G_RM_FOG_SHADE_A", "G_RM_AA_ZB_XLU_SURF2"],
                "combine_mode": ["G_CC_DECALRGBA", "G_CC_PASS2"],
                "geometry_mode": ["G_ZBUFFER", "G_SHADE", "G_FOG", "G_SHADING_SMOOTH"],
                "fog_color": "D_801CB058[D_800DAB2C].unk88..unk94",
                "fog_factor": "((500*256)/(far-near), ((500-near)*256)/(far-near)) where near/far=unk98/unk9C"
            },
            "sunny_beach_one_player_race": {
                "condition": "gCourseID == SUNNY_BEACH (1), gPlayers == ONE_PLAYER, D_801CE64C != 1",
                "cycle_type": "G_CYC_1CYCLE",
                "render_mode": ["G_RM_AA_ZB_XLU_SURF", "G_RM_AA_ZB_XLU_SURF2"],
                "combine_mode": ["G_CC_DECALRGBA", "G_CC_DECALRGBA"],
                "geometry_mode": ["G_ZBUFFER", "G_SHADE"],
                "fog": "disabled; branch does not install fog geometry, fog color, or fog factor",
                "texture": "course_textures.sunny_beach (D_800DC0E4[1], segment 8 +0x400)",
                "water_source": "Sunny Beach course 1 / normal profile source records are emitted in sunny_beach_wave_sources.bin; Drake Lake source records remain available separately for its verified profile-8 fixture"
            },
            "other_one_player_preview_path": {
                "condition": "D_801CE64C == 1",
                "cycle_type": "G_CYC_1CYCLE",
                "render_mode": ["G_RM_AA_ZB_XLU_SURF", "G_RM_AA_ZB_XLU_SURF2"],
                "combine_mode": "G_CC_DECALRGBA",
                "geometry_mode": ["G_ZBUFFER", "G_SHADE"]
            },
            "source": "reference/wr64-decomp/src/game/code_43DA0.c:312-401; course-specific one-player geometry/render state verified in Draw_WaterEffects at 0x80090F58"
        },
        "runtime_dependencies": [
            "G_VTX commands in the list read per-frame water vertices from segment 3; they are not ROM assets.",
            "Dynamic env color and alpha are inherited inputs and must be supplied from the matching course/frame state.",
        ],
        "mesh_attributes": {
            "source": "reference/wr64-decomp/asm-all/us/rev1/game/water_69D0.s:func_80050204",
            "height_conversion": "signed fixed-point height uses arithmetic shift right 8, then adds live gWaterLevel",
            "interior_uv": "for A/B/C stencil heights: a=ASR(A,7), b=ASR(B,7), c=ASR(C,7); dx=(a-b)*55; dz=(((a+b)>>1)-c)*64; k=992.0/sqrt(dx*dx+dz*dz+0xC00000); S=1024+trunc(dz*k); T=1024+trunc(dx*k).",
            "stencil_order": "ASM reads A from D_80160A98 row-stencil slot s6[0], B from s4[0], C from current slot s5[8]. The row setup derives s6/s4 from the output row index; the original flat triangular topology must be preserved to map these samples.",
            "edge_uv": "The same function has separate edge loops; this script extracts their code source but does not generate a replacement mesh or claim byte-identical edge attributes."
        },
        "scope": "cartridge Original; no enhanced shader/profile data"
    }
    (args.out / "water_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {len(extracted)} display lists and manifest to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
