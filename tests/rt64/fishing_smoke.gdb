# Run with isolated debug-warp settings for Fishing Pond (scene 25, entrance 1),
# SHIP_RT64_TEST_FRAMES=360 and SHIP_RT64_TEST_KEYS=60:Space.
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb
ripple_draws = 0
exit_codes = []
gdb.events.exited.connect(lambda event: exit_codes.append(event.exit_code))
class Ripple(gdb.Breakpoint):
    def stop(self):
        global ripple_draws
        assert int(gdb.parse_and_eval('play->sceneNum')) == 0x49
        ripple_draws += 1
        return False
Ripple('soh/src/overlays/actors/ovl_Fishing/z_fishing.c:1236')
end
run
python
print('FISHING REGRESSION RIPPLE DRAWS:', ripple_draws)
if exit_codes != [0] or ripple_draws == 0:
    gdb.execute('quit 1')
end
quit 0
