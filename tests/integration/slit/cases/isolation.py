"""Blocked-command and physical-jog isolation from virtual slit axes."""
from .base import SlitCase


class IsolationTests(SlitCase):
    virtual = ('CENTERY', 'GAPY')

    def fault_free(self):
        self.rig.plant.check()
        for axis in self.rig.names:
            error = self.rig.get(axis, '-ErrId')
            # A failed plant injects drive alarms when withdrawing feedback.
            # Preserve that original failure rather than calling it an axis bug.
            self.rig.plant.check()
            self.assertEqual(error, 0,
                             axis + ': isolated/rejected command must not cause a controller fault')
        for axis in self.rig.profile['physical']:
            self.assertLess(abs(self.rig.get(axis, '-PosAct')), .8, 'isolation test travel bound')

    def blocked_burst(self, interface):
        physical, other, target = 'TR_HI', 'TR_LO', .65
        before = self.rig.get(physical, '-PosAct')
        other_position = self.rig.get(other, '-PosAct')
        self.rig.stage_native(physical, 3, target, .05)
        self.rig.execute_native(physical)
        self.rig.wait(lambda: all(self.rig.get(a, '-Blocked') == 1 for a in self.virtual),
                      'physical motion blocks both virtual axes', 5)
        pending = []
        def isolated():
            self.fault_free()
            self.assertEqual(self.rig.get(physical, '-Busy'), 1,
                             'physical move must remain active throughout blocked-command burst')
            self.assertEqual(self.rig.get(physical, '-TrjSrcTyp-RB'), 0)
            self.assertAlmostEqual(self.rig.get(physical, '-TgtPosCmd'), target, delta=.001,
                                   msg='blocked virtual requests must not replace physical target')
            self.assertAlmostEqual(self.rig.get(other, '-PosAct'), other_position, delta=.015)
            for axis in self.virtual:
                self.assertEqual(self.rig.get(axis, '-Blocked'), 1)
                self.assertEqual(self.rig.get(axis, '-Busy'), 0,
                                 axis + ': blocked request must not execute')
            return True
        isolated()
        for index in range(12):
            for axis in self.virtual:
                self.rig.plant.check()
                if interface == 'motor':
                    suffix, value = '.VAL', .2 if index % 2 == 0 else -.2
                else:
                    suffix, value = '-ExeCmd', 1
                # Wait for each submitted record operation later, while the
                # physical axis still owns motion. CA submission alone is not
                # proof that a blocked command was processed/rejected.
                pending.append(self.rig.client.put(self.rig.name(axis, suffix), value, wait=False))
            # Avoid a full synchronous scan before each command: it competes
            # with the external plant for CA processing. Keep every request,
            # checking isolation after each four-command batch instead.
            if len(pending) % 4 == 0: isolated()
        def processed():
            isolated()
            return all(event.is_set() for event in pending)
        self.rig.wait(processed, 'blocked command records finish without executing or faulting', 5)
        def progressed():
            isolated()
            return self.rig.get(physical, '-PosAct') > before + .02
        self.rig.wait(progressed, 'physical move continues despite blocked requests', 5)
        self.rig.put(physical, '-StpCmd', 1)
        self.rig.wait_native_stopped(physical)
        stopped = {a: self.rig.get(a, '-PosAct') for a in self.rig.profile['physical']}
        def no_replay():
            self.fault_free()
            return (all(self.rig.get(a, '-Busy') == 0 for a in self.rig.names)
                    and all(abs(self.rig.get(a, '-PosAct') - p) < .015 for a, p in stopped.items()))
        self.rig.wait(no_replay, 'physical STOP leaves no pending virtual execution', 5)
        self.rig.assert_stays(no_replay, 'blocked requests must not replay after physical STOP')
        self.rig.move(physical, 0)
        self.rig.move('CENTERY', .1)
        self.rig.move('CENTERY', 0)
        self.fault_free()
        self.rig.trace.append({'action': 'blocked command isolation', 'interface': interface,
                               'requests': len(pending), 'snapshot': self.rig.snapshot()})

    def test_blocked_motor_commands_during_physical_move(self):
        self.blocked_burst('motor')

    def test_blocked_native_commands_during_physical_move(self):
        self.blocked_burst('native')

    def jog(self, stop_with_record):
        saved = {axis: {field: self.rig.get(axis, field) for field in ('.JVEL', '.JAR')}
                 for axis in self.rig.profile['physical']}
        def restore():
            for axis, settings in saved.items():
                self.rig.put(axis, '.JOGF', 0)
                self.rig.put(axis, '.JOGR', 0)
                for field, value in settings.items(): self.rig.put(axis, field, value)
        self.addCleanup(restore)
        for axis in self.rig.profile['physical']:
            self.rig.put(axis, '.JVEL', .2)
            self.rig.put(axis, '.JAR', 1)
            other = 'TR_LO' if axis == 'TR_HI' else 'TR_HI'
            for field, sign in (('.JOGF', 1), ('.JOGR', -1)):
                before, other_position = self.rig.get(axis, '-PosAct'), self.rig.get(other, '-PosAct')
                self.rig.put(axis, '-EnaCmd', 1)
                self.rig.wait(lambda: self.rig.get(axis, '-EnaAct') == 1, 'physical axis enabled for jog', 6)
                self.rig.put(axis, field, 1)
                def moving():
                    self.fault_free()
                    self.assertAlmostEqual(self.rig.get(other, '-PosAct'), other_position, delta=.015)
                    return (self.rig.get(axis, '-Busy') == 1 and self.rig.get(axis, '.DMOV') == 0
                            and self.rig.get(axis, '.MOVN') == 1
                            and sign*self.rig.get(axis, '-VelAct') > .02
                            and sign*(self.rig.get(axis, '-PosAct')-before) > .06)
                self.rig.wait(moving, axis + ' jog moves in requested direction without virtual faults', 5)
                self.rig.put(axis, '.STOP' if stop_with_record else field, 1 if stop_with_record else 0)
                def stopped():
                    self.fault_free()
                    self.assertAlmostEqual(self.rig.get(other, '-PosAct'), other_position, delta=.015)
                    return (self.rig.get(axis, '-Busy') == 0 and self.rig._drive_stationary(axis)
                            and self.rig.get(axis, '.DMOV') == 1 and self.rig.get(axis, '.MOVN') == 0
                            and self.rig.get(axis, '.MIP') == 0 and self.rig.get(axis, field) == 0)
                self.rig.wait(stopped, axis + ' jog ramp-down and motor completion without virtual faults', 5)
                position = self.rig.get(axis, '-PosAct')
                def held():
                    self.fault_free()
                    return stopped() and abs(self.rig.get(axis, '-PosAct')-position) < .015
                self.rig.assert_stays(held, 'stopped physical jog must not replay or fault virtual axes', seconds=.5)
                self.rig.kinematics()
                self.rig.move(axis, 0)
        self.rig.move('CENTERY', .1)
        self.rig.move('CENTERY', 0)
        self.fault_free()

    def test_physical_jogs_release_without_virtual_errors(self):
        self.jog(False)

    def test_physical_jogs_stop_without_virtual_errors(self):
        self.jog(True)
