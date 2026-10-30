from .base import SlitCase


class FeedbackTests(SlitCase):
    def test_encoder_readiness_recovery_requires_reset_and_new_command(self):
        if self.loop != 'closed':
            self.skipTest('requires primary EL5042 feedback')
        axis = 'TR_HI'
        self.rig.start_move(axis, .7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, 'motion before readiness fault', 4)
        self.rig.plant.inject(axis, ready=False)
        self.rig.fault_stopped(axis, 0x14417)
        self.rig.plant.inject(axis, ready=True)
        self.rig.wait(lambda: self.rig.encoder_status(axis) & 4, 'restored encoder ready bit published', 3)
        def disabled():
            return (self.rig.get(axis, '-EnaCmd-RB') == 0 and self.rig.get(axis, '-EnaAct') == 0
                    and self.rig.get(axis, '-Busy') == 0 and not self.rig.drive_control(axis) & 1
                    and self.rig.brake(axis) == 0)
        self.rig.assert_stays(lambda: disabled() and self.rig.get(axis, '-ErrId') == 0x14417,
                              'restored readiness must preserve the fault and keep power off')
        self.rig.reset()
        self.rig.assert_stays(lambda: disabled() and self.rig.get(axis, '-ErrId') == 0,
                              'reset must not restart the interrupted move')
        self.rig.move(axis, 0)

    def test_unready_secondary_encoder_does_not_stop_internal_feedback_motion(self):
        if self.loop != 'open':
            self.skipTest('EL5042 is the primary encoder in closed-loop mode')
        axis = 'TR_HI'
        self.rig.plant.inject(axis, ready=False)
        self.rig.wait(lambda: not self.rig.encoder_status(axis) & 4, 'secondary ready bit removed', 3)
        self.rig.move(axis, .2)
        self.rig.move(axis, 0)
        self.assertEqual(self.rig.get(axis, '-ErrId'), 0)
        self.assertEqual(self.rig.encoder_status(axis) & 4, 0)

    def test_encoder_not_ready_at_idle_prevents_enable(self):
        if self.loop != 'closed':
            self.skipTest('requires primary EL5042 feedback')
        axis = 'TR_HI'
        self.rig.put(axis, '-EnaCmd', 0)
        self.rig.wait(lambda: self.rig.get(axis, '-EnaAct') == 0, 'HI disabled', 5)
        self.rig.plant.inject(axis, ready=False)
        self.rig.wait(lambda: self.rig.get(axis, '-ErrId') == 0x14417, 'idle primary encoder not-ready', 3)
        try:
            self.rig.put(axis, '-EnaCmd', 1)
        except RuntimeError:
            pass
        self.rig.fault_stopped(axis, 0x14417)

    def test_encoder_not_ready_during_motion(self):
        if self.loop != 'closed':
            self.skipTest('EL5042 is not the primary encoder in open-loop mode')
        axis = 'TR_HI'
        self.rig.start_move(axis, .7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .06, 'motion before encoder not-ready', 4)
        self.rig.plant.inject(axis, ready=False)
        self.rig.fault_stopped(axis, 0x14417)  # ERROR_ENC_NOT_READY

    def test_frozen_encoder_following_error(self):
        axis = 'TR_HI'
        self.rig.plant.inject(axis, frozen=True)
        self.rig.start_move(axis, .7)
        if self.loop == 'closed':
            self.rig.fault_stopped(axis, 0x14C08)  # ERROR_MON_MAX_POSITION_LAG_EXCEEDED
        else:
            self.rig.wait(lambda: abs(self.rig.get(axis, '-PosAct') - .7) < .015
                          and self.rig.get(axis, '-Busy') == 0, 'open-loop move ignores frozen secondary encoder', 10)
            self.assertEqual(self.rig.get(axis, '-ErrId'), 0)

    def test_unready_axis_does_not_block_other_physical_axis(self):
        if self.loop != 'closed':
            self.skipTest('requires primary EL5042 feedback')
        self.rig.put('TR_HI', '-EnaCmd', 1)
        self.rig.wait(lambda: self.rig.get('TR_HI', '-EnaAct') == 1, 'HI enabled', 5)
        self.rig.plant.inject('TR_HI', ready=False)
        self.rig.fault_stopped('TR_HI', 0x14417)
        self.rig.move('TR_LO', .15)
        self.assertNotEqual(self.rig.get('TR_HI', '-ErrId'), 0)
