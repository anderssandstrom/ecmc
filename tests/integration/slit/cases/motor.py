"""Motor coordinate transforms, SPMG and an unreachable AtTarget condition."""
from .base import SlitCase


class MotorTests(SlitCase):
    axis = 'TR_HI'

    def test_offset_and_direction_coordinate_mapping(self):
        axis = self.axis
        saved = {s: self.rig.get(axis, s) for s in ('.OFF', '.DIR', '.FOFF', '-CfgSyncSftLim', '-CfgDLLM-RB', '-CfgDHLM-RB')}
        def restore():
            errors = self.rig.stop_all()
            for s in ('.DIR', '.OFF', '.FOFF'):
                self.rig.put(axis, s, saved[s])
            self.rig.put(axis, '-CfgSyncSftLim', 1)
            self.rig.put(axis, '-CfgDLLM', saved['-CfgDLLM-RB'])
            self.rig.put(axis, '-CfgDHLM', saved['-CfgDHLM-RB'])
            self.rig.wait(lambda: abs(self.rig.get(axis, '.DLLM') - saved['-CfgDLLM-RB']) < 1e-6
                          and abs(self.rig.get(axis, '.DHLM') - saved['-CfgDHLM-RB']) < 1e-6,
                          'coordinate test restores dial limits', 5)
            self.rig.put(axis, '-CfgSyncSftLim', saved['-CfgSyncSftLim'])
            self.assertFalse(errors, str(errors))
        self.addCleanup(restore)
        self.rig.put(axis, '-CfgSyncSftLim', 0)
        self.rig.put(axis, '-EnaCmd', 1)
        self.rig.wait(lambda: self.rig.get(axis, '-EnaAct') == 1, 'enabled for coordinate moves', 6)
        self.rig.put(axis, '.FOFF', 1)  # Freeze the requested user offset.
        for direction in (0, 1):
            before = self.rig.get(axis, '-PosAct')
            self.rig.put(axis, '.DIR', direction)
            self.rig.put(axis, '.OFF', .3)
            sign = 1 if direction == 0 else -1
            self.rig.wait(lambda: abs(self.rig.get(axis, '.RBV') - (.3 + sign*before)) < .015,
                          'user readback reflects OFF and DIR without motion', 5)
            self.assertAlmostEqual(self.rig.get(axis, '-PosAct'), before, delta=.002)
            for dial in (.2, -.2, 0):
                user = .3 + sign*dial
                complete = self.rig.put(axis, '.VAL', user)
                self.rig.wait(lambda: complete.is_set() and self.rig.get(axis, '.DMOV') == 1
                              and self.rig.get(axis, '-Busy') == 0
                              and abs(self.rig.get(axis, '-PosAct') - dial) < .015,
                              'motor user target reaches independent native dial target', 10)
                self.assertAlmostEqual(self.rig.get(axis, '.RBV'), user, delta=.015)
                self.assertAlmostEqual(self.rig.get(axis, '.DVAL'), dial, delta=.002)
                self.assertEqual(self.rig.get(axis, '.LVIO'), 0)
                self.assertEqual(self.rig.get(axis, '.SEVR'), 0)

    def set_spmg(self, value):
        self.rig.put(self.axis, '.SPMG', value)
        self.rig.wait(lambda: self.rig.get(self.axis, '.SPMG') == value,
                      'motor acknowledges SPMG field write', 3)

    def test_spmg_pause_resume_and_stop_cancel(self):
        axis = self.axis
        saved = self.rig.get(axis, '.SPMG')
        self.addCleanup(self.set_spmg, saved)
        self.set_spmg(3)  # Go
        self.rig.start_move(axis, .7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') > .05, 'moving before Pause', 4)
        self.set_spmg(1)  # Pause
        self.rig.wait_native_stopped(axis)
        stopped = self.rig.get(axis, '-PosAct')
        self.assertLess(stopped, .65)
        self.rig.assert_stays(lambda: self.rig.get(axis, '-Busy') == 0
                              and abs(self.rig.get(axis, '-PosAct') - stopped) < .015,
                              'Pause holds the stopped axis', seconds=.5)
        self.set_spmg(3)
        self.rig.wait(lambda: self.rig.get(axis, '-Busy') == 1, 'Go resumes native motion', 5)
        self.rig.wait(lambda: self.rig.get(axis, '.DMOV') == 1
                      and abs(self.rig.get(axis, '-PosAct') - .7) < .015,
                      'Go resumes paused target', 10)
        self.rig.start_move(axis, -.7)
        self.rig.wait(lambda: self.rig.get(axis, '-PosAct') < .6, 'moving before Stop', 4)
        self.set_spmg(0)
        self.rig.wait_native_stopped(axis)
        stopped = self.rig.get(axis, '-PosAct')
        self.assertGreater(stopped, -.65)
        self.wait_motor_stop_cancelled(stopped)
        self.set_spmg(3)
        self.rig.assert_stays(lambda: self.rig.get(axis, '-Busy') == 0
                              and self.rig.get(axis, '-ErrId') == 0
                              and abs(self.rig.get(axis, '-PosAct') - stopped) < .015,
                              'Go after Stop must not replay the cancelled target')
        self.rig.move(axis, 0)

    def wait_motor_stop_cancelled(self, stopped):
        axis = self.axis
        def cancelled():
            self.rig.check_native_error(axis)
            self.assertEqual(self.rig.get(axis, '-Busy'), 0, 'SPMG Stop must not restart native motion')
            self.assertTrue(self.rig._drive_stationary(axis), 'SPMG Stop must keep drive stationary')
            self.assertLess(abs(self.rig.get(axis, '-PosAct') - stopped), .015,
                            'axis must remain at stopped position during motor cancellation')
            return (self.rig.get(axis, '.SPMG') == 0 and self.rig.get(axis, '.DMOV') == 1
                    and self.rig.get(axis, '.MOVN') == 0 and self.rig.get(axis, '.MIP') == 0
                    and abs(self.rig.get(axis, '.VAL') - self.rig.get(axis, '.RBV')) < .015
                    and abs(self.rig.get(axis, '.DVAL') - self.rig.get(axis, '-PosAct')) < .015)
        # Native ramp-down ends before the slower motor callback synchronizes
        # its cancelled target. Keep Stop selected until that callback completes.
        self.rig.wait(cancelled, 'motor acknowledges SPMG Stop and cancels user/dial targets', 5)

    def zero_target_tolerance(self, trigger):
        axis = self.axis
        saved_transport = self.rig.open_text_commands()
        self.addCleanup(self.rig.close_text_commands, saved_transport)
        number = self.rig.profile['axes'][axis]
        command = self.rig.text_command
        tolerance = command(f'GetAxisMonAtTargetTol({number})', query=True)
        self.addCleanup(command, f'Cfg.SetAxisMonAtTargetTol({number},{tolerance:.12g})')
        command(f'Cfg.SetAxisMonAtTargetTol({number},0)')
        self.assertEqual(command(f'GetAxisMonAtTargetTol({number})', query=True), 0)
        if trigger == 'motor':
            complete = self.rig.start_move(axis, .2)
        else:
            complete = None
            self.rig.stage_native(axis, 3, .2)
            self.rig.execute_native(axis)
        self.rig.wait(lambda: abs(self.rig.get(axis, '-PosAct')-.2) < .015
                      and self.rig._drive_stationary(axis),
                      'trajectory reaches target and stops with zero tolerance', 10)
        def incomplete():
            self.rig.check_native_error(axis)
            return (self.rig.get(axis, '-AtTarget') == 0
                    and self.rig.get(axis, '.DMOV') == 0
                    and (complete is None or not complete.is_set()))
        self.rig.assert_stays(incomplete, 'unfulfilled AtTarget must not complete motor request')
        self.rig.put(axis, '-StpCmd', 1)
        self.rig.wait(lambda: self.rig.get(axis, '.DMOV') == 1
                      and self.rig.get(axis, '.MOVN') == 0
                      and (complete is None or complete.is_set()),
                      'STOP terminates a move even when AtTarget cannot assert', 5)
        command(f'Cfg.SetAxisMonAtTargetTol({number},{tolerance:.12g})')
        self.rig.move(axis, -.15)
        self.rig.move(axis, 0)

    def test_zero_at_target_tolerance_motor(self):
        self.zero_target_tolerance('motor')

    def test_zero_at_target_tolerance_native(self):
        self.zero_target_tolerance('native')
