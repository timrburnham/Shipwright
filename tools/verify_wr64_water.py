#!/usr/bin/env python3
"""Execute supplied cartridge water instructions; write local reference traces.

This is a focused, big-endian VR4300 interpreter, not a game emulator. Unsupported
instructions fail closed. No ROM instructions or assets are emitted into source.
"""
import argparse
import hashlib
import json
import math
import random
import struct
import subprocess
import tempfile
from pathlib import Path

ROM_SHA1 = '508dfc2d4caa42b6f6de5263d0aed5e44ac7966a'
FIELD = 0x80162420


def signed(x, bits=32):
    x &= (1 << bits) - 1
    return x - (1 << bits) if x & (1 << (bits - 1)) else x


def float_bits(x):
    return struct.unpack('>I', struct.pack('>f', x))[0]


class Cartridge:
    def __init__(self, rom):
        self.mem = bytearray(0x800000)
        self.mem[0x46800:0x45800 + 0xF7440] = rom[0x1000:0xF7440]
        self.r = [0] * 32
        self.f = [0] * 32
        self.hi = self.lo = 0
        self.cc = False
        self.skips = set()
        self.writes = []

    def read(self, address, size, sign=False):
        offset = address & 0x7fffff
        if offset + size > len(self.mem):
            raise ValueError('RAM read crosses boundary')
        return int.from_bytes(self.mem[offset:offset + size], 'big', signed=sign)

    def write(self, address, value, size):
        offset = address & 0x7fffff
        if offset + size > len(self.mem):
            raise ValueError('RAM write crosses boundary')
        self.mem[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'big')
        if FIELD <= address < FIELD + 384 * 128 * 4:
            self.writes.append((address - FIELD, signed(value, size * 8)))

    def fp(self, reg, fmt):
        if fmt == 16:
            return struct.unpack('>f', self.f[reg].to_bytes(4, 'big'))[0]
        if fmt == 17:
            bits = (self.f[reg + 1] << 32) | self.f[reg]
            return struct.unpack('>d', bits.to_bytes(8, 'big'))[0]
        if fmt == 20:
            return signed(self.f[reg])
        raise NotImplementedError(f'FP format {fmt}')

    def put_fp(self, reg, fmt, value):
        if fmt == 16:
            self.f[reg] = float_bits(value)
        elif fmt == 17:
            bits = int.from_bytes(struct.pack('>d', value), 'big')
            self.f[reg], self.f[reg + 1] = bits & 0xffffffff, bits >> 32
        elif fmt == 20:
            self.f[reg] = int(value) & 0xffffffff
        else:
            raise NotImplementedError(f'FP output format {fmt}')

    def instruction(self, pc):
        w = self.read(pc, 4)
        op, rs, rt, rd, sh, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
        imm = signed(w, 16)
        a, b = self.r[rs], self.r[rt]
        target = None
        annul = False
        if op == 0:
            if fn in (0, 2, 3, 4, 6, 7):
                n = sh if fn < 4 else a & 31
                self.r[rd] = b << n if fn in (0, 4) else b >> n if fn in (2, 6) else signed(b) >> n
            elif fn in (8, 9):
                target = a
                if fn == 9:
                    self.r[rd] = pc + 8
            elif fn == 16:
                self.r[rd] = self.hi
            elif fn == 18:
                self.r[rd] = self.lo
            elif fn in (24, 25):
                v = signed(a) * signed(b) if fn == 24 else a * b
                self.lo, self.hi = v & 0xffffffff, (v >> 32) & 0xffffffff
            elif fn in (26, 27):
                x, y = (signed(a), signed(b)) if fn == 26 else (a, b)
                if y == 0:
                    raise ZeroDivisionError(hex(pc))
                q = abs(x) // abs(y) * (-1 if (x < 0) != (y < 0) else 1)
                self.lo, self.hi = q & 0xffffffff, (x - q * y) & 0xffffffff
            elif fn in (32, 33, 34, 35, 36, 37, 38, 39, 42, 43):
                self.r[rd] = {32: lambda: a + b, 33: lambda: a + b,
                              34: lambda: a - b, 35: lambda: a - b,
                              36: lambda: a & b, 37: lambda: a | b,
                              38: lambda: a ^ b, 39: lambda: ~(a | b),
                              42: lambda: int(signed(a) < signed(b)), 43: lambda: int(a < b)}[fn]()
            else:
                raise NotImplementedError(f'SPECIAL {fn:x} at {pc:08x}')
        elif op in (2, 3):
            target = ((pc + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2)
            if op == 3:
                self.r[31] = pc + 8
        elif op in (1, 4, 5, 6, 7, 20, 21, 22, 23):
            if op == 1:
                if rt not in (0, 1, 2, 3, 16, 17, 18, 19):
                    raise NotImplementedError(f'REGIMM {rt:x} at {pc:08x}')
                condition = signed(a) < 0 if rt in (0, 2, 16, 18) else signed(a) >= 0
                likely = rt in (2, 3, 18, 19)
                if rt >= 16:
                    self.r[31] = pc + 8
            else:
                condition = {4: a == b, 5: a != b, 6: signed(a) <= 0, 7: signed(a) > 0}[op & 15]
                likely = op >= 20
            target = pc + 4 + imm * 4 if condition else pc + 8
            annul = likely and not condition
        elif op in (8, 9, 10, 11, 12, 13, 14, 15):
            self.r[rt] = {8: lambda: a + imm, 9: lambda: a + imm,
                          10: lambda: int(signed(a) < imm), 11: lambda: int(a < (imm & 0xffffffff)),
                          12: lambda: a & (w & 65535), 13: lambda: a | (w & 65535),
                          14: lambda: a ^ (w & 65535), 15: lambda: (w & 65535) << 16}[op]()
        elif op in (32, 33, 35, 36, 37, 40, 41, 43, 49, 53, 57, 61):
            address = (a + imm) & 0xffffffff
            if op in (32, 33, 35, 36, 37):
                self.r[rt] = self.read(address, {32: 1, 33: 2, 35: 4, 36: 1, 37: 2}[op], op in (32, 33))
            elif op in (40, 41, 43):
                self.write(address, b, {40: 1, 41: 2, 43: 4}[op])
            elif op in (49, 57):
                if op == 49:
                    self.f[rt] = self.read(address, 4)
                else:
                    self.write(address, self.f[rt], 4)
            elif op == 53:
                self.f[rt + 1], self.f[rt] = self.read(address, 4), self.read(address + 4, 4)
            else:
                self.write(address, self.f[rt + 1], 4)
                self.write(address + 4, self.f[rt], 4)
        elif op == 17:
            if rs == 0:
                self.r[rt] = self.f[rd]
            elif rs == 4:
                self.f[rd] = b
            elif rs == 8:
                condition = self.cc == bool(rt & 1)
                target = pc + 4 + imm * 4 if condition else pc + 8
                annul = bool(rt & 2) and not condition
            elif rs in (16, 17, 20):
                x = self.fp(rd, rs)
                y = self.fp(rt, rs)
                if fn in (0, 1, 2, 3, 4, 5, 6, 7):
                    value = {0: lambda: x + y, 1: lambda: x - y, 2: lambda: x * y,
                             3: lambda: x / y, 4: lambda: math.sqrt(x), 5: lambda: abs(x),
                             6: lambda: x, 7: lambda: -x}[fn]()
                    self.put_fp(sh, rs, value)
                elif fn in (12, 13, 14, 15, 32, 33, 36):
                    fmt = {32: 16, 33: 17}.get(fn, 20)
                    value = {12: round, 13: math.trunc, 14: math.ceil, 15: math.floor, 36: round}.get(fn, lambda v: v)(x)
                    self.put_fp(sh, fmt, value)
                elif fn >= 48:
                    self.cc = ((fn & 2) and x == y) or ((fn & 4) and x < y)
                else:
                    raise NotImplementedError(f'FP function {fn:x} at {pc:08x}')
            else:
                raise NotImplementedError(f'COP1 {rs:x} at {pc:08x}')
        else:
            raise NotImplementedError(f'op {op:x}, word {w:08x} at {pc:08x}')
        self.r = [v & 0xffffffff for v in self.r]
        self.r[0] = 0
        return target, annul

    def call(self, address, *args, stop=None):
        self.r[29], self.r[31] = 0x807ff000, 0xffffffff
        for i, arg in enumerate(args):
            self.r[4 + i] = arg & 0xffffffff
        pc = address
        for count in range(2000000):
            if pc == stop:
                return count
            if pc == 0xffffffff:
                return count
            if pc in self.skips:
                pc = self.r[31]
                continue
            if pc == 0x800C7010:  # libc sqrtf, with a single-precision result
                self.put_fp(0, 16, math.sqrt(self.fp(12, 16)))
                pc = self.r[31]
                continue
            target, annul = self.instruction(pc)
            if target is None:
                pc += 4
            else:
                if not annul:
                    nested, _ = self.instruction(pc + 4)
                    if nested is not None:
                        raise ValueError('branch in delay slot')
                pc = target
        raise RuntimeError(f'instruction limit at {pc:08x}')


def extract_drake_source(rom):
    """Evaluate course 4 / screen profile 8 / difficulty 0's loader block."""
    cpu = Cartridge(rom)
    cpu.call(0x80047c38)
    cpu.r[2] = cpu.read(0x800d68fc + 4*4, 4)
    cpu.r[7] = 0x801bfb68
    cpu.r[10], cpu.r[11], cpu.r[12], cpu.r[13] = 0x801bfa48, 0x80154350, 0x24, 0xffffffff
    cpu.r[9], cpu.r[17], cpu.r[18] = 0, 0x801bfd70, cpu.read(0x800d6924 + 4*4, 4)
    cpu.write(0x801bfb68, 0, 4)
    cpu.write(0x801bfd70, 0, 4)
    cpu.write(0x801db338, 0, 4)
    cpu.call(0x8007239c, stop=0x80072694)
    if cpu.read(0x801bfb68, 4) != 0 or cpu.read(0x801bfd70, 4) != 1:
        raise AssertionError('Unexpected Drake source counts')
    return bytes(cpu.mem[0x1bfb70:0x1bfbb0])


def extract_sunny_sources(rom):
    """Evaluate Sunny Beach's normal course-1 secondary source list.

    D_801CE638's normal profile selects D_800D68AC/D_800D68D4; profile 8
    instead selects D_800D68FC/D_800D6924 and has no Sunny Beach sources.
    The loader writes evaluated records contiguously at D_801BFB70.
    """
    cpu = Cartridge(rom)
    cpu.call(0x80047c38)
    course_id = 1
    cpu.r[2] = cpu.read(0x800d68ac + course_id * 4, 4)
    cpu.r[7] = 0x801bfb68
    cpu.r[10], cpu.r[11], cpu.r[12], cpu.r[13] = 0x801bfa48, 0x80154350, 0x24, 0xffffffff
    cpu.r[9], cpu.r[17] = 0, 0x801bfd70  # difficulty 0, secondary count
    cpu.r[18] = cpu.read(0x800d68d4 + course_id * 4, 4)
    cpu.write(0x801bfb68, 0, 4)  # primary count
    cpu.write(0x801bfd70, 0, 4)  # secondary count
    cpu.write(0x801db338, 0, 4)
    cpu.call(0x8007239c, stop=0x80072694)
    primary_count = cpu.read(0x801bfb68, 4)
    secondary_count = cpu.read(0x801bfd70, 4)
    if primary_count != 0 or secondary_count != 3:
        raise AssertionError(f'Unexpected Sunny Beach source counts: primary={primary_count}, secondary={secondary_count}')
    records = bytes(cpu.mem[0x1bfb70:0x1bfb70 + secondary_count * 64])
    if len(records) != 192:
        raise AssertionError(f'Unexpected Sunny Beach source payload length: {len(records)}')
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--out', type=Path, default=Path('build/cartridge-water/reference.json'))
    parser.add_argument('--port', type=Path, help='Native rt64_wave_field_trace executable to compare')
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError('Expected supplied US revision 1 big-endian ROM')
    cpu = Cartridge(rom)
    cpu.write(0x80192494, 12, 4)
    cpu.write(0x8019249c, 469, 4)
    setup_count = cpu.call(0x8004E614)
    # Camera setup calls mesh generation after preparing its exact offsets.
    # Skip only that leaf while measuring the integrator.
    cpu.skips.add(0x80050204)
    cpu.write(0x801de648, 0, 4)
    camera_count = cpu.call(0x80050F8C, 0, float_bits(0), float_bits(0))
    coords = [[cpu.read(0x80160430 + 2*i, 1, True), cpu.read(0x80160431 + 2*i, 1, True)] for i in range(469)]
    offsets = [cpu.read(0x80160a98 + 8*i, 4) for i in range(469)]
    for addr, shift in ((0x800d4724, 9), (0x800d472c, 5), (0x800d4730, 6)):
        cpu.write(addr, shift, 4)
    fixtures = []
    for u, v, amplitude in ((0, 0, -2), (12, 0, -2), (-12, -12, -7)):
        cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4] = bytes(384*128*4)
        offset = offsets[coords.index([u, v])]
        cpu.write(FIELD + offset, amplitude << 8, 2)
        ticks = []
        for _ in range(3):
            cpu.writes.clear()
            cpu.call(0x8004EAA4)
            cells = [[off, cpu.read(FIELD + off, 2, True), cpu.read(FIELD + off + 2, 2, True)] for off in offsets if cpu.read(FIELD + off, 4)]
            ticks.append({'cells': cells, 'write_count': len(cpu.writes)})
        fixtures.append({'seed': [u, v, amplitude], 'ticks': ticks})
    result = {'rom_sha1': ROM_SHA1, 'setup_instructions': setup_count,
              'camera_instructions': camera_count, 'coordinates': coords, 'offsets': offsets,
              'fixtures': fixtures}
    if args.port:
        rng = random.Random(0x4eaa4)
        comparisons = []
        for center in ((0, 0), (-4096, -3072), (8192, 6144), (-24550, 200), (24550, -200)):
            seeds = [(center[0] + rng.uniform(-700, 700), center[1] + rng.uniform(-600, 600), rng.randint(-100, 5)) for _ in range(24)]
            seeds += [(center[0] + 704, center[1], -2), (center[0] + 2048, center[1], -7)]
            cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4] = bytes(384*128*4)
            cpu.call(0x80050F8C, 0, float_bits(center[0]), float_bits(center[1]))
            for x, z, amplitude in seeds:
                cpu.put_fp(12, 16, x)
                cpu.put_fp(14, 16, z)
                cpu.call(0x8004F3D4, 0, 0, amplitude)
            expected = bytearray()
            for _ in range(12):
                cpu.call(0x8004EAA4)
                expected.extend(cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4])
            with tempfile.TemporaryDirectory(prefix='wr64-parity-') as temp:
                output = Path(temp) / 'field.bin'
                command = [str(args.port.resolve()), str(output), *map(str, center), '12']
                command += [str(value) for seed in seeds for value in seed]
                subprocess.run(command, check=True)
                actual = output.read_bytes()
                native_uv = (Path(str(output) + '.uv')).read_bytes()[-384*128*4:]
            if actual != expected:
                i = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), min(len(actual), len(expected)))
                raise AssertionError(f'Cartridge mismatch at center {center}, tick {i // (384*128*4)}, field byte {i % (384*128*4)}: port={actual[i:i+4].hex()}, ROM={expected[i:i+4].hex()}')
            comparisons.append({'center': center, 'ticks': 12, 'seeds': seeds, 'sha256': hashlib.sha256(expected).hexdigest()})
            # Run the original dynamic mesh through its interior/edge height
            # loops. Stop before the separate six-vertex distant skirt, whose
            # camera-dependent coordinates do not define interior attributes.
            cpu.skips.discard(0x80050204)
            cpu.call(0x80050204, 0, 0x80400000, stop=0x8005091c)
            cpu.skips.add(0x80050204)
            mesh_offsets = [cpu.read(0x80160a98 + 8*i, 4) for i in range(469)]
            verified = 0
            for i, (u, v) in enumerate(coords):
                if max(abs(u), abs(v), abs(u-v)) >= 12:
                    continue
                uv = bytes(cpu.mem[0x400000 + i*16 + 8:0x400000 + i*16 + 12])
                off = mesh_offsets[i]
                if uv != native_uv[off:off+4]:
                    raise AssertionError(f'UV mismatch at center {center}, vertex {(u,v)}: port={native_uv[off:off+4].hex()}, ROM={uv.hex()}')
                verified += 1
            comparisons[-1]['interior_vertices_verified'] = verified
        result['native_comparisons'] = comparisons
        print('Native field matches all bytes of 60 cartridge ticks across five centers')
        cpu.call(0x80047c38)  # Original sine-table Taylor-series initializer
        original_sine = bytes(cpu.mem[0x154350:0x158350])
        # Evaluate the selected course's source-loader block with its actual
        # ROM table pointers. Profile 8 selects D_800D68FC / D_800D6924.
        source_record = extract_drake_source(rom)
        cpu.mem[0x1bfb70:0x1bfbb0] = source_record
        center = (-2000, -2000)
        cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4] = bytes(384*128*4)
        cpu.call(0x80050F8C, 0, float_bits(center[0]), float_bits(center[1]))
        expected = bytearray()
        for _ in range(128):
            cpu.call(0x8004cc7c, 0x801bfb70)
            cpu.call(0x8004eaa4)
            expected.extend(cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4])
        with tempfile.TemporaryDirectory(prefix='wr64-source-') as temp:
            record_path, output = Path(temp) / 'source.bin', Path(temp) / 'field.bin'
            record_path.write_bytes(source_record)
            subprocess.run([str(args.port.resolve()), '--source', str(output), str(record_path), *map(str, center), '128'], check=True)
            actual = output.read_bytes()
            if Path(str(output) + '.sine').read_bytes() != original_sine:
                raise AssertionError('Native sine table differs from original initializer')
        if actual != expected:
            i = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
            raise AssertionError(f'Source mismatch at tick {i // (384*128*4)}, field byte {i % (384*128*4)}')
        result['source_comparison'] = {'course': 4, 'profile': 8, 'difficulty': 0, 'ticks': 128,
            'center': center, 'source_count': 1, 'sha256': hashlib.sha256(expected).hexdigest(),
            'sine_sha256': hashlib.sha256(original_sine).hexdigest()}
        print('Native course source and sine table match 128 cartridge ticks')

        sunny_sources = extract_sunny_sources(rom)
        cpu.mem[0x1bfb70:0x1bfb70 + len(sunny_sources)] = sunny_sources
        cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4] = bytes(384*128*4)
        cpu.call(0x80050F8C, 0, float_bits(center[0]), float_bits(center[1]))
        expected = bytearray()
        source_count = len(sunny_sources) // 64
        for _ in range(128):
            for source_index in range(source_count):
                cpu.call(0x8004cc7c, 0x801bfb70 + source_index * 64)
            cpu.call(0x8004eaa4)
            expected.extend(cpu.mem[FIELD & 0x7fffff:(FIELD & 0x7fffff) + 384*128*4])
        with tempfile.TemporaryDirectory(prefix='wr64-sunny-source-') as temp:
            record_path, output = Path(temp) / 'sources.bin', Path(temp) / 'field.bin'
            record_path.write_bytes(sunny_sources)
            subprocess.run([str(args.port.resolve()), '--source', str(output), str(record_path), *map(str, center), '128'], check=True)
            actual = output.read_bytes()
        if actual != expected:
            i = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), min(len(actual), len(expected)))
            raise AssertionError(f'Sunny Beach source mismatch at tick {i // (384*128*4)}, field byte {i % (384*128*4)}')
        result['sunny_source_comparison'] = {
            'course': 1, 'profile': 1, 'difficulty': 0, 'primary_source_count': 0,
            'secondary_source_count': source_count, 'ticks': 128, 'center': center,
            'record_bytes': len(sunny_sources), 'records_sha256': hashlib.sha256(sunny_sources).hexdigest(),
            'field_sha256': hashlib.sha256(expected).hexdigest(),
        }
        print('Native Sunny Beach course-1 source list matches 128 cartridge ticks')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Executed original water functions; wrote {args.out}')


if __name__ == '__main__':
    main()
