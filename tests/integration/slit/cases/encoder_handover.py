"""Two-encoder power handover: live counter control and validated absolute return."""
from .encoder_power import EncoderPowerCase


class EncoderHandoverTests(EncoderPowerCase):
    def setUp(self):
        if self.loop != 'closed':
            self.skipTest('handover requires primary absolute encoder plus internal counter')
        super().setUp()
        self.assertEqual(self.query('EncPwrBackup'), 0, 'fixture starts with handover disabled')
        self.previous_diff_tol = self.query('EncPwrDiffTol')
        self.addCleanup(self.remove_handover)
        self.configure_backup(1, .02)
        self.config('EncPwrOffDelay', .8)

    def configure_backup(self, index, tolerance):
        self.rig.text_command(f'Cfg.SetAxisEncPwrBackup({self.number},{index},{tolerance:.9g})')

    def remove_handover(self):
        errors = self.rig.stop_all()
        self.rig.wait(lambda: self.rig.get(self.axis, '-EnaAct') == 0
                      and self.rig.get(self.axis, '-Busy') == 0,
                      'disabled idle axis before removing handover mode', 5)
        self.config("EncPwrCtrl", 0)
        self.config("EncMode", 0)
        self.configure_backup(0, self.previous_diff_tol)
        self.assertFalse(errors, 'stop before removing handover: ' + str(errors))

    def primary(self):
        return self.rig.get(self.axis, '-SetEncIdCmd-RB')

    def sleep_on_counter(self):
        self.start(.15)
        self.arrive(.15)
        self.dwell_then_off(.8)
        self.rig.wait(lambda: self.primary() == 1, 'counter selected before encoder sleeps', 3)
        self.rig.wait(lambda: self.rig.get(self.axis, '-Enc01-Absolute') == 0
                      and self.rig.get(self.axis, '-Enc02-Absolute') == 1
                      and self.rig.get(self.axis, '-Enc01-Homed') == self.rig.get(self.axis, '-Enc02-Homed')
                      and self.rig.get(self.axis, '-Enc01-Ready') == 1
                      and self.rig.get(self.axis, '-Enc02-PwrOn') == 0
                      and ((int(self.rig.get(self.axis, '-Enc02-Stat')) >> 8) & 7) == 1,
                      'encoder status exposes referenced counter and sleeping absolute feedback', 3)
        self.assertAlmostEqual(self.rig.get(self.axis, '-Enc01-PosAct'),
                               self.rig.get(self.axis, '-Enc02-PosAct'), delta=.002,
                               msg='counter reference agrees with absolute encoder before power-off')

    def test_counter_handover_and_repeated_wake(self):
        self.sleep_on_counter()
        self.config('EnableAutoEnable', 0)
        for expected in (.20, .25, .30):
            self.rig.assert_stays(lambda: self.healthy() and self.primary() == 1
                                  and self.raw() == 0 and self.rig.get(self.axis, '-EnaAct') == 1,
                                  'holding torque uses counter while absolute supply is off', .2)
            self.start(.05, command=2)
            self.rig.wait(lambda: self.healthy() and self.primary() == 2 and self.state() == 3,
                          'fresh absolute feedback selected on successful wake', 3)
            self.arrive(expected)
            self.dwell_then_off(.8)
            self.assertEqual(self.primary(), 1)

    def mismatch(self, sign):
        self.sleep_on_counter()
        before = self.rig.plant.physical_position(self.axis)
        tolerance = self.query('EncPwrDiffTol')
        self.rig.plant.inject(self.axis, encoder_bias_mm=sign * 2 * tolerance)
        self.start(.05, command=2)
        self.rig.fault_stopped(self.axis, 0x1441f, timeout=5)
        self.assertEqual(self.primary(), 1, 'mismatch retains counter feedback')
        self.assertAlmostEqual(self.rig.plant.physical_position(self.axis), before, delta=.002,
                               msg='mismatch must not start the queued move or correct toward old position')
        self.rig.plant.inject(self.axis, encoder_bias_mm=0)
        self.rig.reset()
        self.rig.assert_stays(lambda: self.healthy() and self.rig.get(self.axis, '-Busy') == 0
                              and abs(self.rig.plant.physical_position(self.axis)-before) < .002,
                              'reset does not replay rejected motion', .5)
        self.start(.05, command=2)
        self.arrive(.20)
        self.dwell_then_off(.8)

    def test_positive_wake_mismatch_blocks_motion(self): self.mismatch(1)
    def test_negative_wake_mismatch_blocks_motion(self): self.mismatch(-1)

    def test_ready_timeout_keeps_counter_selected(self):
        self.sleep_on_counter()
        before = self.rig.plant.physical_position(self.axis)
        self.config('EncPwrTimeout', .8)
        self.rig.plant.inject(self.axis, ready=False)
        self.start(.05, command=2)
        self.rig.fault_stopped(self.axis, 0x1441d, timeout=5)
        self.assertEqual(self.primary(), 1)
        self.assertAlmostEqual(self.rig.plant.physical_position(self.axis), before, delta=.002)

    def fixed_modes(self, interface):
        self.sleep_on_counter()
        self.assertFalse(self.rig.stop_all())
        self.rig.wait_drive_disabled(self.axis)

        def select(mode):
            if interface == 'pv':
                self.rig.put(self.axis, '-EncMode', mode)
            else:
                self.config('EncMode', mode)
            self.rig.wait(lambda: self.query('EncMode') == mode
                          and self.rig.get(self.axis, '-EncMode-RB') == mode,
                          'accepted generic encoder mode', 3)

        # Fixture configuration assigns counter=1 and absolute feedback=2.
        # Neither the mode labels nor mode command encode those hardware roles.
        select(1)
        self.start(.05, command=2)
        self.arrive(.20)
        self.rig.assert_stays(lambda: self.healthy() and self.primary() == 1
                              and self.state() == 1, 'fixed encoder 1 motion leaves supply off', .3)
        self.assertFalse(self.rig.stop_all())
        self.rig.wait_drive_disabled(self.axis)
        select(2)
        self.rig.wait(lambda: self.healthy() and self.state() == 3 and self.primary() == 2,
                      'fixed encoder 2 waits for valid powered feedback', 3)
        self.start(.05, command=2)
        self.arrive(.25)
        self.rig.assert_stays(lambda: self.healthy() and self.state() == 3
                              and self.primary() == 2, 'fixed encoder 2 remains powered', 1.1)
        self.assertFalse(self.rig.stop_all())
        self.rig.wait_drive_disabled(self.axis)
        select(0)
        self.wait_off()
        self.assertEqual(self.primary(), 1)

    def test_fixed_encoders_via_pv(self): self.fixed_modes('pv')
    def test_fixed_encoders_via_text(self): self.fixed_modes('text')

    def test_force_on_does_not_change_fixed_encoder(self):
        self.sleep_on_counter()
        self.assertFalse(self.rig.stop_all())
        self.rig.wait_drive_disabled(self.axis)
        self.rig.put(self.axis, '-EncMode', 1)
        self.rig.put(self.axis, '-EncForceOn', 1)
        self.rig.wait(lambda: self.healthy() and self.state() == 3
                      and self.rig.get(self.axis, '-EncForceOn-RB') == 1,
                      'inspection hold powers encoder without motion', 3)
        self.assertEqual(self.primary(), 1)
        self.assertEqual(self.rig.get(self.axis, '-EnaAct'), 0)
        self.start(.05, command=2)
        self.arrive(.20)
        self.rig.assert_stays(lambda: self.healthy() and self.state() == 3
                              and self.primary() == 1, 'inspection hold survives completed motion', 1.1)
        self.rig.put(self.axis, '-EncForceOn', 0)
        self.rig.assert_stays(lambda: self.healthy() and self.state() == 3,
                              'release starts a fresh encoder dwell', .3)
        self.wait_off()
        self.assertEqual(self.primary(), 1)
