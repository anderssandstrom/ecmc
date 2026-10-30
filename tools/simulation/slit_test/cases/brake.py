"""Brake output behavior via native EL2008 readbacks; no manual output injection."""
from .base import SlitCase, DRIVE_ENABLE_FAILURE_WAIT


class BrakeTests(SlitCase):
    def test_cancel_enable_while_brake_opening(self):
        self.closed()
        for axis in self.rig.profile['physical']:
            self.rig.wait_drive_disabled(axis)
            self.request(axis, True)
            self.rig.wait(lambda: self.rig.drive_control(axis) & 1 and self.rig.brake(axis) == 0,
                          axis + ' opening phase before enable cancellation', 3)
            self.request(axis, False)
            self.rig.assert_stays(lambda: self.rig.brake(axis) == 0 and self.rig.get(axis, '-ErrId') == 0,
                                  axis + ' cancelled opening must keep the brake applied')
            self.rig.wait_drive_disabled(axis)
            self.enabled(axis)  # A fresh enable must still perform a normal opening.
            self.request(axis, False)
            self.rig.wait_drive_disabled(axis)

    def request(self, axis, enabled):
        # Observe the transition while the request is pending. A completed,
        # synchronous put could otherwise hide the entire opening/closing phase.
        pv = self.rig.client.pv(self.rig.name(axis, '-EnaCmd'))
        if not pv.write_access or pv.put(int(enabled), wait=False, timeout=self.rig.client.timeout) != 1:
            raise RuntimeError('enable request failed: ' + pv.pvname)

    def closed(self):
        errors = self.rig.stop_all()
        if errors:
            raise RuntimeError('brake-test stop failed: ' + '; '.join(errors))
        self.rig.wait(lambda: all(self.rig.brake(axis) == 0 and not self.rig.drive_control(axis) & 1
                                 for axis in self.rig.profile['physical']), 'both brakes applied and amplifiers off', 5)

    def enabled(self, axis):
        self.request(axis, True)
        self.rig.wait(lambda: self.rig.brake(axis) == 1 and self.rig.get(axis, '-EnaAct') == 1,
                      axis + ' brake released and enabled', 5)

    def test_disabled_axes_apply_brakes(self):
        self.closed()
        for axis in self.rig.profile['physical']:
            self.assertEqual(self.rig.brake(axis), 0)

    def test_brake_release_follows_amplifier_enable(self):
        self.closed()
        for axis in self.rig.profile['physical']:
            self.request(axis, True)
            self.rig.wait(lambda: self.rig.drive_control(axis) & 1 and self.rig.brake(axis) == 0,
                          axis + ' amplifier commanded on while brake remains applied', 3)
            self.rig.trace.append({'action': 'brake opening phase', 'axis': axis, 'snapshot': self.rig.snapshot()})
            self.rig.wait(lambda: self.rig.brake(axis) == 1 and self.rig.get(axis, '-EnaAct') == 1,
                          axis + ' brake releases after opening delay', 5)

    def test_brake_application_precedes_amplifier_disable(self):
        self.closed()
        for axis in self.rig.profile['physical']:
            self.enabled(axis)
            self.request(axis, False)
            self.rig.wait(lambda: self.rig.brake(axis) == 0 and self.rig.drive_control(axis) & 1,
                          axis + ' brake applied while amplifier remains on', 3)
            self.rig.trace.append({'action': 'brake closing phase', 'axis': axis, 'snapshot': self.rig.snapshot()})
            self.rig.wait(lambda: self.rig.brake(axis) == 0 and not self.rig.drive_control(axis) & 1,
                          axis + ' amplifier disables after brake application', 5)

    def test_brakes_remain_released_during_physical_motion(self):
        for axis in self.rig.profile['physical']:
            self.rig.start_move(axis, .3)
            self.rig.wait(lambda: self.rig.get(axis, '-Busy') == 1, axis + ' moving', 3)
            def complete():
                moving = self.rig.get(axis, '-Busy')
                if moving:
                    self.assertEqual(self.rig.brake(axis), 1)
                return moving == 0
            self.rig.wait(complete, axis + ' motion completes with brake released', 5)
        self.closed()

    def test_failed_enable_never_releases_brake(self):
        self.closed()
        for axis in self.rig.profile['physical']:
            self.rig.wait_drive_disabled(axis)
            self.rig.plant.inject(axis, acknowledge_enable=False)
            self.request(axis, True)
            def failed():
                self.assertEqual(self.rig.brake(axis), 0)
                return self.rig.get(axis, '-ErrId') != 0
            self.rig.wait(failed, axis + ' enable fails without brake release', DRIVE_ENABLE_FAILURE_WAIT)
            self.rig.fault_stopped(axis, 0x14613)  # ERROR_DRV_STATE_MACHINE_TIME_OUT
            self.assertEqual(self.rig.brake(axis), 0)
            self.rig.plant.clear_faults()
            self.rig.reset()

    def test_lost_enable_applies_brake(self):
        axis = 'TR_HI'
        self.rig.start_move(axis, .6)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, 'motion before lost enable', 4)
        self.assertEqual(self.rig.brake(axis), 1)
        self.rig.plant.inject(axis, acknowledge_enable=False)
        self.rig.fault_stopped(axis)
        self.assertEqual(self.rig.brake(axis), 0)

    def test_both_brakes_release_for_coordinated_motion(self):
        self.closed()
        self.rig.start_move('CENTERY', .6)
        self.rig.wait(lambda: all(self.rig.brake(axis) == 1 for axis in self.rig.profile['physical'])
                      and self.rig.get('CENTERY', '-Busy') == 1, 'both slave brakes released during master motion', 5)
        self.rig.wait(lambda: not self.rig.get('CENTERY', '-Busy')
                      and abs(self.rig.get('CENTERY', '-PosAct') - .6) < .02, 'coordinated motion completes', 10)
        self.closed()
