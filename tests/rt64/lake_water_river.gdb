# Adult File3 river contact smoke. Uses the actual decoded actor mesh and live
# collision water boxes 0/1. Run through capture_lake_water.py with entrance 1.
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb

lake_updates = 0
phase = 'approach'
phase_updates = 0
accepted = {'upper': 0, 'lower': 0}
in_water = {'upper': False, 'lower': False}
exit_codes = []
gdb.events.exited.connect(lambda event: exit_codes.append(getattr(event, 'exit_code', None)))

def val(expr):
    return gdb.parse_and_eval(expr)

def select_layer_point(box_index, layer):
    boxes = val('play->colCtx.colHeader->waterBoxes')
    triangles = val('sWorldFootprint')
    count = int(val('sHighTriangleCount'))
    box = boxes[box_index]
    xmin, zmin = float(box['xMin']), float(box['zMin'])
    xmax = xmin + float(box['xLength'])
    zmax = zmin + float(box['zLength'])
    best = None
    for i in range(count):
        triangle = triangles[i]
        if int(triangle['layer']) != layer:
            continue
        x = sum(float(triangle['points'][p]['x']) for p in range(3)) / 3.0
        z = sum(float(triangle['points'][p]['z']) for p in range(3)) / 3.0
        margin = min(x - xmin, xmax - x, z - zmin, zmax - z)
        if margin > 0 and (best is None or margin > best[0]):
            best = (margin, x, z, float(box['ySurface']))
    if best is None:
        raise RuntimeError('No layer %d triangle centroid inside water box %d' % (layer, box_index))
    return best

def teleport(box_index, layer, name):
    margin, x, z, mean = select_layer_point(box_index, layer)
    p = '((Player*)play->actorCtx.actorLists[2].head)'
    y = mean - 40.0
    for axis, value in (('x', x), ('y', y), ('z', z)):
        gdb.execute('set %s->actor.world.pos.%s = %f' % (p, axis, value))
        gdb.execute('set %s->actor.prevPos.%s = %f' % (p, axis, value))
        gdb.execute('set %s->actor.velocity.%s = 0' % (p, axis))
    gdb.execute('set %s->actor.speedXZ = 0' % p)
    gdb.execute('set %s->currentBoots = 0' % p)
    gdb.execute('set %s->prevBoots = 0' % p)
    gdb.execute('set %s->stateFlags1 &= ~((unsigned)1 << 27)' % p)
    print('RIVER PROBE: %s point box=%d margin=%.1f XZ=(%.1f,%.1f) mean=%.1f' %
          (name, box_index, margin, x, z, mean))

class LakeUpdate(gdb.Breakpoint):
    def __init__(self):
        super().__init__('LakeWater_Update', internal=True)
    def stop(self):
        global lake_updates, phase, phase_updates
        if int(val('play->sceneNum')) != 0x57:
            return False
        lake_updates += 1
        p = val('((Player*)play->actorCtx.actorLists[2].head)')
        if int(p) == 0:
            return False
        if phase == 'approach' and lake_updates >= 80:
            teleport(0, 2, 'upper')
            phase, phase_updates = 'upper', 0
            return False
        phase_updates += 1
        flags = int(p['stateFlags1'])
        if flags & (1 << 27):
            if phase in in_water:
                in_water[phase] = True
        if phase == 'upper' and phase_updates >= 40:
            teleport(1, 1, 'lower')
            phase, phase_updates = 'lower', 0
        elif phase == 'lower' and phase_updates >= 40:
            phase = 'done'
        return False

class AcceptedRiverSample(gdb.Breakpoint):
    def __init__(self):
        super().__init__('LakeWater.c:468', internal=True)
    def stop(self):
        if int(val('play->sceneNum')) == 0x57:
            layer = int(val('targetLayer'))
            if phase == 'upper' and layer == 2:
                accepted['upper'] += 1
            elif phase == 'lower' and layer == 1:
                accepted['lower'] += 1
        return False

LakeUpdate()
AcceptedRiverSample()
end
run
python
print('LAKE RIVER SMOKE: updates=%d accepted upper/lower=%d/%d in-water=%s/%s phase=%s' %
      (lake_updates, accepted['upper'], accepted['lower'], in_water['upper'], in_water['lower'], phase))
if exit_codes != [0] or phase != 'done' or accepted['upper'] == 0 or accepted['lower'] == 0:
    gdb.execute('quit 1')
if not in_water['upper'] and not in_water['lower']:
    print('LAKE RIVER SMOKE: neither relocated player set IN_WATER')
    gdb.execute('quit 1')
end
quit 0
