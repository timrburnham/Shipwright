# Temporary File3, --save-states, input:60:Space,120:Space,180:Space,500:F5,560:F7,650:F7
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb
saves = 0
loads = 0
rebuilds = 0
steps = 0
exits = []
gdb.events.exited.connect(lambda e: exits.append(getattr(e, 'exit_code', None)))
class Save(gdb.Breakpoint):
    def stop(self):
        global saves
        saves += 1
        return False
class Load(gdb.Breakpoint):
    def stop(self):
        global loads
        loads += 1
        return False
class Initialize(gdb.Breakpoint):
    def stop(self):
        global rebuilds
        if loads > rebuilds and int(gdb.parse_and_eval('play->sceneNum')) == 0x57:
            rebuilds += 1
        return False
class Step(gdb.Breakpoint):
    def stop(self):
        global steps
        if loads:
            steps += 1
        return False
Save('SaveState::Save', internal=True)
Load('SaveState::Load', internal=True)
Initialize('LakeWater_Initialize', internal=True)
Step('LakeWaterSimulation_Step', internal=True)
end
run
python
print('LAKE SAVE STATE: saves=%d loads=%d rebuilds=%d steps_after_load=%d' % (saves,loads,rebuilds,steps))
if exits != [0] or saves != 1 or loads != 2 or rebuilds != 2 or steps < 10:
    gdb.execute('quit 1')
end
quit 0
