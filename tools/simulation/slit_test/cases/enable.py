from .base import SlitCase, DRIVE_ENABLE_FAILURE_WAIT


class EnableTests(SlitCase):
    def test_missing_enable_acknowledgement(self):
        axis = 'TR_LO'
        self.assertEqual(self.rig.stop_all(), [], 'disable before missing-enable injection')
        self.rig.wait_drive_disabled(axis)
        self.rig.plant.inject(axis, acknowledge_enable=False)
        try:
            self.rig.put(axis, '-EnaCmd', 1)
        except RuntimeError:
            pass  # A synchronous enable timeout may also surface as a failed put.
        self.rig.fault_stopped(axis, 0x14613, timeout=DRIVE_ENABLE_FAILURE_WAIT)

    def test_lost_enable_during_motion(self):
        axis = 'TR_LO'
        self.rig.start_move(axis, .7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, 'motion before enable loss', 4)
        self.rig.plant.inject(axis, acknowledge_enable=False)
        self.rig.fault_stopped(axis, 0x14317)  # ERROR_AXIS_AMPLIFIER_ENABLED_LOST
        self.assertLess(self.rig.get(axis, '-PosAct'), .65)

    def test_drive_fault(self):
        axis = 'TR_HI'
        self.rig.start_move(axis, .7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, 'motion before drive fault', 4)
        self.rig.plant.inject(axis, drive_fault=True)
        self.rig.wait(lambda: self.rig.drive_status(axis) & 8, 'injected drive fault bit published', 3)
        self.rig.fault_stopped(axis, 0x1460E)  # ERROR_DRV_HW_ALARM_0: linked status bit 3.
