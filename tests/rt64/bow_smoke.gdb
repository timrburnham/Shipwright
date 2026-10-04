# Run with an isolated adult debug save, a bounded RT64 frame limit, and
# repeated Left key presses. See docs/RT64_VALIDATION.md.
set pagination off
set confirm off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb
counts = {'shoot': 0, 'near': 0, 'far': 0}
exit_codes = []
gdb.events.exited.connect(lambda event: exit_codes.append(event.exit_code))
class Equip(gdb.Breakpoint):
    def stop(self):
        gdb.execute('set var gSaveContext.equips.buttonItems[1] = 3')
        gdb.execute('set var gSaveContext.equips.cButtonSlots[0] = 3')
        gdb.execute('set var gSaveContext.inventory.ammo[3] = 30')
        self.enabled = False
        return False
class Shot(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('this->actor.parent')) == 0:
            counts['shoot'] += 1
        return False
class Draw(gdb.Breakpoint):
    def stop(self):
        z = float(gdb.parse_and_eval('thisx->projectedPos.z'))
        cutoff = int(gdb.parse_and_eval('gGameInfo->data[95 + 5 * 96]'))
        counts['near' if z < cutoff else 'far'] += 1
        return False
Equip('Play_Update')
Shot('EnArrow_Shoot')
Draw('EnArrow_Draw')
end
run
python
print('BOW REGRESSION COUNTS:', counts)
if exit_codes != [0] or counts['shoot'] < 2 or counts['near'] == 0 or counts['far'] == 0:
    gdb.execute('quit 1')
end
quit 0
