#!/usr/bin/env python3
"""Capture Lake Hylia with isolated settings and a temporary or copied save.

The existing debug warp screen supplies a temporary debug save. This tool
selects Lake Hylia, sets Link's age, and uses the bounded RT64 input/capture
fixture. It does not install assets or prove swimming correctness.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def prepare(build, output, age, enabled, entrance, save=None):
    app = build / 'soh'
    executable = app / 'soh.elf'
    if not executable.is_file():
        raise ValueError(f'Build the Linux RT64 application first: {executable}')
    for name in ('oot.o2r', 'soh.o2r'):
        if not (app / name).is_file():
            raise ValueError(f'Missing game archive: {app / name}')
    output.mkdir(parents=True, exist_ok=True)
    marker = output / '.lake-water-capture'
    if any(output.iterdir()) and not marker.exists():
        raise ValueError(f'Refusing to overwrite an unrelated directory: {output}')
    marker.write_text('Isolated Lake Hylia capture settings; safe to regenerate.\n')
    source_config = app / 'shipofharkinian.json'
    config = json.loads(source_config.read_text()) if source_config.exists() else {}
    cvars = config.setdefault('CVars', {})
    cvars.setdefault('gSettings', {})['BootSequence'] = 2 if save else 3
    cvars.setdefault('gDeveloperTools', {})['BetterDebugWarpScreen'] = 1
    cvars['gDeveloperTools']['DebugEnabled'] = int(save is not None)
    cvars.setdefault('gGeneral', {}).update({
        'BetterDebugWarpScreenCurrentScene': 24,
        'BetterDebugWarpScreenTopDisplayedScene': 20,
        'BetterDebugWarpScreenPageDownIndex': entrance,
        'BetterDebugWarpScreenLinkAge': 1 if age == 'child' else 0,
        'BetterDebugWarpScreenNightFlag': 0,
    })
    cvars.setdefault('gEnhancements', {})['WaveRaceLakeWater'] = int(enabled)
    cvars.setdefault('gOpenWindows', {}).update({'ModalWindow': 0, 'Notifications': 0})
    # Use fixed native layout for reproducible reference images.
    cvars['gSettings']['AdvancedResolution'] = {'Enabled': 0}
    cvars['gSettings']['MSAAValue'] = 1
    config.pop('WarpPoints', None)
    (output / 'shipofharkinian.json').write_text(json.dumps(config, indent=4) + '\n')
    if save:
        save_data = json.loads(save.read_text())
        if 'base' not in save_data.get('sections', {}):
            raise ValueError(f'Not a Shipwright save file: {save}')
        save_dir = output / 'Save'
        save_dir.mkdir(exist_ok=True)
        # Use slot 1 to enter the existing debug warp screen after loading.
        # This is a copy, so the normal game's save is never modified.
        shutil.copyfile(save, save_dir / 'file1.sav')
    for name in ('oot.o2r', 'soh.o2r', 'gamecontrollerdb.txt', 'assets', 'cartridge-water'):
        source = app / name
        target = output / name
        if source.exists() and not target.exists():
            target.symlink_to(source.resolve(), target_is_directory=source.is_dir())
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/linux-clang-rt64')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--age', choices=('child', 'adult'), default='child')
    parser.add_argument('--water', choices=('zelda', 'wave-race'), default='wave-race')
    parser.add_argument('--save', type=Path, help='Load a copy of an existing Shipwright save before warping')
    parser.add_argument('--entrance', type=int, choices=range(7), default=0,
                        help='Lake entrance: 0=field, 1=river, 2=temple, 3=fishing, '
                             '4=laboratory shoreline, 5=underwater shortcut, 6=warp pad')
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--capture-start', type=int, default=0)
    parser.add_argument('--timeout', type=int, default=90)
    parser.add_argument('--keys', help='Override bounded SDL frame:Scancode key schedule')
    parser.add_argument('--gdb-script', type=Path, help='Run a local gameplay regression under GDB')
    parser.add_argument('--msaa', type=int, choices=(1, 2, 4, 8), default=1)
    parser.add_argument('--profile', action='store_true', help='Report RT64 CPU submission times')
    parser.add_argument('--save-states', action='store_true', help='Enable save states only in isolated settings')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if args.frames < 90 or not 0 <= args.capture_start <= args.frames:
        parser.error('Use at least 90 frames and a capture-start within the run.')
    output = args.output.resolve()
    try:
        executable = prepare(args.build.resolve(), output, args.age, args.water == 'wave-race', args.entrance,
                             args.save.resolve() if args.save else None)
        if args.msaa != 1 or args.save_states:
            config_path = output / 'shipofharkinian.json'
            config = json.loads(config_path.read_text())
            config['CVars']['gSettings']['MSAAValue'] = args.msaa
            if args.save_states:
                config['CVars'].setdefault('gCheats', {})['SaveStatesEnabled'] = 1
            config_path.write_text(json.dumps(config, indent=4) + '\n')
        if args.prepare_only:
            print(output)
            return 0
        env = os.environ.copy()
        env.update({
            'SHIP_HOME': str(output),
            'SHIP_RT64_TEST_FRAMES': str(args.frames),
            'SHIP_RT64_TEST_KEYS': args.keys if args.keys is not None else
                                  ('60:Space,120:Space,180:Space' if args.save else '60:Space'),
            'SHIP_RT64_CAPTURE': str(output / ('frame' if args.capture_start else 'frame.ppm')),
            'SHIP_WR64_WATER_TRACE': '1',
        })
        if args.profile:
            env['SHIP_RT64_PROFILE'] = '1'
        env.pop('SHIP_RT64_TEST_KEY', None)
        env.pop('SHIP_RT64_CAPTURE_START', None)
        if args.capture_start:
            env['SHIP_RT64_CAPTURE_START'] = str(args.capture_start)
        with (output / 'launch.log').open('w') as log:
            command = [str(executable)]
            if args.gdb_script:
                if not args.gdb_script.is_file():
                    raise ValueError(f'Missing GDB script: {args.gdb_script}')
                command = ['gdb', '-batch', '-x', str(args.gdb_script.resolve()), '--args', str(executable)]
            result = subprocess.run(command, cwd=output, env=env,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
        if result.returncode:
            raise ValueError(f'Application exited with {result.returncode}; see {output / "launch.log"}')
        images = sorted(output.glob('*.ppm'))
        if not images:
            raise ValueError('Application produced no capture; inspect launch.log.')
        try:
            from PIL import Image
            for path in images:
                Image.open(path).save(path.with_suffix('.png'))
        except ImportError:
            pass
        print(f'Capture: {output} ({len(images)} images). Confirm the scene visually and inspect water diagnostics.')
        return 0
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
