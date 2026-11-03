"""Opt-in native target-write triggering through PV and text interfaces."""
from .base import SlitCase


class TargetWriteTests(SlitCase):
    axis = 'TR_HI'

    def setUp(self):
        super().setUp()
        self.number = self.rig.profile['axes'][self.axis]
        transport = self.rig.open_text_commands()
        self.addCleanup(self.rig.close_text_commands, transport)
        self.saved = {name: self.query(name) for name in (
            'AutoExecuteOnTargetPos', 'EnableAutoEnable', 'AutoEnableTimeout')}
        self.addCleanup(self.restore)
        self.config('AutoExecuteOnTargetPos', 0)
        # Stage parameters with legacy semantics before selecting the mode.
        self.rig.stage_native(self.axis, 3, 0, .2)

    def query(self, name):
        if name == 'AutoExecuteOnTargetPos': name = 'AutoExec'
        return self.rig.text_command(f'GetAxis{name}({self.number})', query=True)

    def config(self, name, value):
        if name == 'AutoExecuteOnTargetPos': name = 'AutoExec'
        self.rig.text_command(f'Cfg.SetAxis{name}({self.number},{value:.12g})')

    def restore(self):
        errors = self.rig.stop_all()
        self.config('AutoExecuteOnTargetPos', 0)
        for name in ('AutoEnableTimeout', 'EnableAutoEnable', 'AutoExecuteOnTargetPos'):
            self.config(name, self.saved[name])
        self.assertFalse(errors, 'stop before restoring target-write mode: ' + str(errors))

    def select_mode(self, enable):
        self.config('AutoExecuteOnTargetPos', enable)
        self.assertEqual(self.query('AutoExecuteOnTargetPos'), enable)
        self.rig.wait(lambda: self.rig.get(self.axis, '-AutoExecuteOnTargetPos-RB') == enable,
                      'axis panel readback matches target-write mode', 3)

    def write(self, interface, value):
        if interface == 'pv':
            self.rig.put(self.axis, '-TgtPosCmd', value)
        else:
            self.rig.text_command(f'Main.M{self.number}.fPosition={value:.12g}')

    def completed(self, target):
        def moving():
            self.rig.check_native_error(self.axis)
            return (self.rig.get(self.axis, '-Busy') == 1
                    and self.rig.get(self.axis, '.DMOV') == 0
                    and self.rig.get(self.axis, '.MOVN') == 1)
        self.rig.wait(moving, 'target write starts native motion and motor flags follow', 5)
        self.rig.wait_native_stopped(self.axis, target)
        self.rig.wait(lambda: self.rig.get(self.axis, '.DMOV') == 1
                      and self.rig.get(self.axis, '.MOVN') == 0,
                      'motor flags settle after target-write move', 5)

    def absolute_relative(self, interface):
        before = self.rig.get(self.axis, '-PosAct')
        self.write(interface, .2)
        self.rig.assert_stays(lambda: self.rig.get(self.axis, '-Busy') == 0
                              and abs(self.rig.get(self.axis, '-PosAct') - before) < .015,
                              'default mode stages target without motion', seconds=.3)
        self.select_mode(1)
        self.rig.assert_stays(lambda: self.rig.get(self.axis, '-Busy') == 0,
                              'selecting the mode must not execute the staged target', seconds=.3)
        self.write(interface, .2)
        self.completed(.2)
        self.write(interface, -.2)
        self.completed(-.2)
        self.rig.put(self.axis, '-MtnCmd', 2)
        # Identical REL writes must each trigger a fresh move, not just a
        # value-change callback. Expected positions are independent of readback.
        for expected in (0, .2):
            self.write(interface, .2)
            self.completed(expected)
        self.select_mode(0)
        self.rig.native_move(self.axis, 0)

    def auto_enable(self, interface):
        self.config('AutoEnableTimeout', 5)
        self.config('EnableAutoEnable', 1)
        self.rig.put(self.axis, '-EnaCmd', 0)
        self.rig.wait(lambda: self.rig.get(self.axis, '-EnaAct') == 0
                      and self.rig.drive_control(self.axis) & 1 == 0,
                      'drive disabled before automatic target-write enable', 5)
        self.select_mode(1)
        self.assertEqual(self.rig.get(self.axis, '-EnaAct'), 0)
        self.write(interface, .2)
        self.completed(.2)
        self.assertEqual(self.rig.get(self.axis, '-EnaAct'), 1)

    def retarget(self, interface):
        self.select_mode(1)
        self.write(interface, .6)
        self.rig.wait(lambda: self.rig.get(self.axis, '-Busy') == 1
                      and self.rig.get(self.axis, '-PosAct') > .05,
                      'motion underway before target-write retarget', 5)
        self.write(interface, .25)
        self.rig.wait_native_stopped(self.axis, .25)
        self.rig.assert_stays(lambda: self.rig.get(self.axis, '-Busy') == 0
                              and self.rig.get(self.axis, '-ErrId') == 0
                              and abs(self.rig.get(self.axis, '-PosAct')-.25) < .015,
                              'retarget completes without replay', seconds=.3)

    def test_pv_absolute_and_repeated_relative(self): self.absolute_relative('pv')
    def test_text_absolute_and_repeated_relative(self): self.absolute_relative('text')
    def test_pv_auto_enable(self): self.auto_enable('pv')
    def test_text_auto_enable(self): self.auto_enable('text')
    def test_pv_retarget(self): self.retarget('pv')
    def test_text_retarget(self): self.retarget('text')
