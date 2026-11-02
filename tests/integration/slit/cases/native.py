"""Issue commands through ECMC command/target/execute PVs, without motor VAL."""
from .base import SlitCase


class NativeTests(SlitCase):
    def test_staged_absolute_command_waits_for_execute(self):
        for axis in self.rig.profile['physical']:
            before = self.rig.get(axis, '-PosAct')
            self.rig.stage_native(axis, 3, .2, .2)
            def idle():
                self.rig.check_native_error(axis)
                return (self.rig.get(axis, '-Busy') == 0
                        and abs(self.rig.get(axis, '-PosAct') - before) < .015)
            self.rig.assert_stays(idle, axis + ' staged targets must not start motion', seconds=.3)
            self.rig.execute_native(axis)
            self.rig.wait_native_stopped(axis, .2)
            self.rig.native_move(axis, -.2, velocity=.3)
            self.rig.native_move(axis, 0)

    def test_relative_commands_repeat_triggers_in_both_directions(self):
        for axis in self.rig.profile['physical']:
            self.rig.native_move(axis, .1)
            for distance, expected in ((.15, .25), (-.15, .1), (-.2, -.1)):
                self.rig.native_move(axis, distance, command=2, expected=expected)
            self.rig.native_move(axis, 0)

    def test_velocity_commands_use_native_stop(self):
        axis = 'TR_HI'
        for velocity in (.2, -.2):
            before = self.rig.get(axis, '-PosAct')
            self.rig.stage_native(axis, 1, velocity=velocity)
            self.rig.execute_native(axis)
            def moving():
                self.rig.check_native_error(axis)
                position = self.rig.get(axis, '-PosAct')
                self.assertLess(abs(position), .6, 'velocity test must remain inside the fixture limits')
                return (position - before) * (1 if velocity > 0 else -1) > .06
            self.rig.wait(moving, axis + ' native velocity command moves in requested direction', 4)
            self.rig.put(axis, '-ExeCmd', 0)
            def still_moving():
                self.rig.check_native_error(axis)
                self.assertLess(abs(self.rig.get(axis, '-PosAct')), .6)
                return self.rig.get(axis, '-Busy') == 1
            self.rig.assert_stays(still_moving, axis + ' clearing trigger must not act as STOP', seconds=.15)
            self.rig.put(axis, '-StpCmd', 1)
            self.rig.wait_native_stopped(axis)
            stopped = self.rig.get(axis, '-PosAct')
            self.rig.assert_stays(lambda: self.rig.get(axis, '-Busy') == 0
                                  and self.rig.get(axis, '-ErrId') == 0
                                  and abs(self.rig.get(axis, '-PosAct') - stopped) < .015,
                                  axis + ' native STOP keeps velocity motion stopped', seconds=.3)
            self.rig.native_move(axis, 0)

    def test_native_stop_interrupts_position_motion(self):
        for axis in self.rig.profile['physical']:
            self.rig.stage_native(axis, 3, .7)
            self.rig.execute_native(axis)
            self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, axis + ' motion before native STOP', 4)
            self.rig.put(axis, '-StpCmd', 1)
            self.rig.wait_native_stopped(axis)
            self.assertLess(self.rig.get(axis, '-PosAct'), .65)
            self.rig.native_move(axis, 0)

    def test_native_center_and_gap_commands(self):
        self.rig.native_move('CENTERY', .2)
        for axis in self.rig.profile['physical']:
            self.assertAlmostEqual(self.rig.get(axis, '-PosAct'), .2, delta=.025)
        self.rig.native_move('GAPY', .4)
        self.assertAlmostEqual(self.rig.get('TR_LO', '-PosAct'), 0, delta=.025)
        self.assertAlmostEqual(self.rig.get('TR_HI', '-PosAct'), .4, delta=.025)
        self.rig.kinematics()
        self.rig.native_move('CENTERY', 0)
