# Gameplay smoke for the adult File3 Lake Hylia shoreline capture.
# Example:
# python3 tools/capture_lake_water.py --output build/linux-clang-rt64/lake-water-file3-shore-wave \
#   --water wave-race --age adult --entrance 5 --frames 1200 \
#   --save build/linux-clang-rt64/soh/Save/file3.sav --gdb-script tests/rt64/lake_water_smoke.gdb
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb

lake_updates = 0
sample_calls = 0
in_water_seen = False
underwater_seen = False
phase = 'approach'
phase_updates = 0
iron_start_y = None
iron_min_y = None
normal_start_y = None
normal_max_y = None
exit_codes = []
gdb.events.exited.connect(lambda event: exit_codes.append(getattr(event, 'exit_code', None)))

def val(expr):
    return gdb.parse_and_eval(expr)

def player_ptr():
    return val('((Player*)play->actorCtx.actorLists[2].head)')

def fval(expr):
    return float(val(expr))

def choose_deep_wet_point():
    # Use the vanilla underwater shortcut entrance. A mesh triangle may span
    # an island, so geometric centroids alone cannot establish deep water.
    p = player_ptr()
    x, z = float(p['actor']['world']['pos']['x']), float(p['actor']['world']['pos']['z'])
    boxes = val('play->colCtx.colHeader->waterBoxes')
    for index in (2, 3):
        box = boxes[index]
        xmin, zmin = float(box['xMin']), float(box['zMin'])
        xmax, zmax = xmin + float(box['xLength']), zmin + float(box['zLength'])
        margin = min(x-xmin, xmax-x, z-zmin, zmax-z)
        if margin > 0:
            mean = float(box['ySurface'])
            if float(p['actor']['floorHeight']) >= mean - 100:
                raise RuntimeError('Shortcut probe is not above a deep underwater floor')
            return margin, x, z, index, mean
    raise RuntimeError('Shortcut probe is outside main lake water boxes')

class LakeUpdate(gdb.Breakpoint):
    def __init__(self):
        super().__init__('LakeWater_Update', internal=True)
    def stop(self):
        global lake_updates, phase, phase_updates, in_water_seen, underwater_seen, sample_calls
        global iron_start_y, iron_min_y, normal_start_y, normal_max_y
        if int(val('play->sceneNum')) != 0x57:
            return False
        lake_updates += 1
        sample_calls = max(sample_calls, int(val("sSamplesUsed")))
        p = player_ptr()
        if p == 0:
            return False
        if phase == 'approach' and lake_updates >= 80:
            margin, x, z, box_index, mean = choose_deep_wet_point()
            y = mean - 40.0
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.world.pos.x = %f' % x)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.world.pos.y = %f' % y)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.world.pos.z = %f' % z)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.prevPos.x = %f' % x)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.prevPos.y = %f' % y)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.prevPos.z = %f' % z)
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.velocity.x = 0')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.velocity.y = 0')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.velocity.z = 0')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->actor.speedXZ = 0')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->stateFlags1 &= ~((unsigned)1 << 27)')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->stateFlags2 &= ~(((unsigned)1 << 10) | ((unsigned)1 << 11))')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->currentBoots = 0')
            gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->prevBoots = 0')
            phase = 'swim'
            phase_updates = 0
            print('LAKE WATER PROBE: moved to high-water triangle, box=%d margin=%.1f XZ=(%.1f,%.1f) mean=%.1f' %
                  (box_index, margin, x, z, mean))
            return False

        flags1 = int(p['stateFlags1'])
        flags2 = int(p['stateFlags2'])
        y = float(p['actor']['world']['pos']['y'])
        if flags1 & (1 << 27):
            in_water_seen = True
        if flags2 & (1 << 10):
            underwater_seen = True

        if phase == 'swim':
            phase_updates += 1
            if phase_updates >= 30:
                gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->prevBoots = 0')
                gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->currentBoots = 1')
                iron_start_y = y
                iron_min_y = y
                phase = 'iron'
                phase_updates = 0
                print('LAKE WATER PROBE: iron boots on at Y=%.1f' % y)
        elif phase == 'iron':
            phase_updates += 1
            iron_min_y = min(iron_min_y, y)
            if phase_updates >= 120:
                gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->prevBoots = 1')
                gdb.execute('set ((Player*)play->actorCtx.actorLists[2].head)->currentBoots = 0')
                normal_start_y = y
                normal_max_y = y
                phase = 'resurface'
                phase_updates = 0
                print('LAKE WATER PROBE: normal boots restored at Y=%.1f; iron minimum Y=%.1f' %
                      (y, iron_min_y))
        elif phase == 'resurface':
            phase_updates += 1
            normal_max_y = max(normal_max_y, y)
            if phase_updates >= 120:
                phase = 'done'
                print('LAKE WATER PROBE: resurface window ended at Y=%.1f' % y)
        return False

LakeUpdate()
end
run
python
print('LAKE WATER SMOKE: updates=%d samples=%d in_water=%s underwater=%s phase=%s' %
      (lake_updates, sample_calls, in_water_seen, underwater_seen, phase))
if phase == 'done' and iron_start_y is not None:
    print('LAKE WATER SMOKE: iron sink=%.1f normal rise=%.1f' %
          (iron_start_y - iron_min_y, normal_max_y - iron_min_y))
if exit_codes != [0] or sample_calls == 0 or not in_water_seen or phase != 'done':
    gdb.execute('quit 1')
if iron_start_y - iron_min_y < 8.0 or normal_max_y - iron_min_y < 8.0:
    print('LAKE WATER SMOKE FAILED: expected iron-boot sink and normal-boot resurfacing')
    gdb.execute('quit 1')
end
quit 0
