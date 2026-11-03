"""Direct motion without the state machine, and bounded command bursts."""
from .base import SlitCase


class InterfaceTests(SlitCase):
    def healthy(self, axis):
        self.rig.check_native_error(axis)
        self.assertLess(abs(self.rig.get(axis, '-PosAct')), .8,
                        'command burst must stay within fixture travel')

    def test_physical_motion_with_state_machine_disabled(self):
        pv = self.rig.prefix + 'SM00-EnaCmd'
        saved = self.rig.client.get(pv)
        def restore():
            errors = self.rig.stop_all()
            self.rig.client.put(pv, saved)
            self.assertFalse(errors, 'STOP before restoring state machine: ' + str(errors))
        self.addCleanup(restore)
        # First enter physical ownership so disabling must retain internal
        # trajectories rather than leaving slaves in external mode.
        self.rig.move('TR_HI', .1)
        self.rig.client.put(pv, 0)
        self.rig.wait(lambda: self.rig.client.get(pv) == 0
                      and all(self.rig.get(a, '-TrjSrcTyp-RB') == 0
                              for a in self.rig.profile['physical']),
                      'state machine disabled and physical trajectories internal', 5)
        for axis in self.rig.profile['physical']:
            other = 'TR_LO' if axis == 'TR_HI' else 'TR_HI'
            before = self.rig.get(other, '-PosAct')
            self.rig.move(axis, .25)
            self.rig.native_move(axis, -.2)
            self.assertAlmostEqual(self.rig.get(other, '-PosAct'), before, delta=.015,
                                   msg='independent physical motion must not move the other blade')
            self.rig.move(axis, 0)
            self.assertEqual(self.rig.client.get(pv), 0)
            self.assertEqual(self.rig.get(axis, '.DMOV'), 1)
            self.assertEqual(self.rig.get(axis, '.MOVN'), 0)

    def burst(self, interface):
        axis = 'TR_HI'
        self.rig.stage_native(axis, 3, .6, .2)
        self.rig.execute_native(axis)
        self.rig.wait(lambda: self.rig.get(axis, '-Busy') == 1
                      and self.rig.get(axis, '.DMOV') == 0
                      and self.rig.get(axis, '.MOVN') == 1,
                      'motion visible on both interfaces before burst', 5)
        if interface == 'text':
            saved = self.rig.open_text_commands()
            self.addCleanup(self.rig.close_text_commands, saved)
        writes = 0
        for index in range(12):
            target = .35 if index % 2 == 0 else -.35
            for repeat in range(2):
                self.healthy(axis)
                if interface == 'motor' or (interface == 'mixed' and repeat == 0):
                    # Ordinary asynchronous writes avoid serializing put-notify
                    # callbacks behind the first motor request. Repeat identical
                    # targets too; the second write must not wedge processing.
                    self.rig.client.put(self.rig.name(axis, '.VAL'), target,
                                        wait=False, notify=False)
                    writes += 1
                elif interface == 'text':
                    number = self.rig.profile['axes'][axis]
                    # Unlike the auto-reset axis-PV trigger, text bExecute=0
                    # stops the trajectory. Do not cancel/restart during ramp-
                    # down: retarget the running move and repeat execute high.
                    for suffix, value in (('fPosition', target), ('bExecute', 1)):
                        self.rig.text_command(f'Main.M{number}.{suffix}={value}')
                        writes += 1
                else:
                    self.rig.put(axis, '-ExeCmd', 0)
                    self.rig.put(axis, '-TgtPosCmd', target)
                    self.rig.put(axis, '-ExeCmd', 1)
                    writes += 3
        self.rig.put(axis, '-StpCmd', 1)
        self.rig.wait_native_stopped(axis)
        self.rig.wait(lambda: self.rig.get(axis, '.DMOV') == 1
                      and self.rig.get(axis, '.MOVN') == 0
                      and self.rig.get(axis, '.PACT') == 0,
                      'motor completes after command burst and STOP', 5)
        stopped = self.rig.get(axis, '-PosAct')
        def idle():
            self.healthy(axis)
            return (self.rig.get(axis, '-Busy') == 0 and self.rig.get(axis, '.DMOV') == 1
                    and self.rig.get(axis, '.MOVN') == 0
                    and abs(self.rig.get(axis, '-PosAct') - stopped) < .015)
        self.rig.assert_stays(idle, 'queued commands must not restart motion after STOP')
        self.assertEqual(self.rig.get(axis, '.LVIO'), 0)
        self.assertEqual(self.rig.get(axis, '.SEVR'), 0)
        self.rig.move(axis, .15)
        self.rig.native_move(axis, -.15)
        self.rig.move(axis, 0)
        self.rig.trace.append({'action': 'command burst recovered', 'interface': interface,
                               'writes': writes, 'snapshot': self.rig.snapshot()})

    def test_repeated_motor_commands(self):
        self.burst('motor')

    def test_repeated_native_commands(self):
        self.burst('native')

    def test_alternating_motor_and_native_commands(self):
        self.burst('mixed')

    def test_repeated_text_commands(self):
        self.burst('text')
