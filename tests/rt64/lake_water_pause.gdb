# Temporary File3, entrance 4; SDL keys:60:Space,120:Space,180:Space,500:Space,650:Space
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb
steps = 0
paused_updates = 0
pause_steps = None
resume_steps = None
exits = []
gdb.events.exited.connect(lambda e: exits.append(getattr(e, 'exit_code', None)))
class Step(gdb.Breakpoint):
    def stop(self):
        global steps
        steps += 1
        return False
class Update(gdb.Breakpoint):
    def stop(self):
        global paused_updates, pause_steps, resume_steps
        if int(gdb.parse_and_eval('play->sceneNum')) != 0x57:
            return False
        paused = int(gdb.parse_and_eval('play->pauseCtx.state')) != 0
        if paused:
            paused_updates += 1
            if pause_steps is None:
                pause_steps = steps
            assert steps == pause_steps, 'Water advanced during pause'
        elif pause_steps is not None:
            resume_steps = steps
        return False
Step('LakeWaterSimulation_Step', internal=True)
Update('Play_Update', internal=True)
end
run
python
print('LAKE PAUSE: steps=%d paused_updates=%d pause_steps=%s resume_steps=%s' %
      (steps, paused_updates, pause_steps, resume_steps))
if exits != [0] or paused_updates < 5 or resume_steps is None or resume_steps <= pause_steps:
    gdb.execute('quit 1')
end
quit 0
