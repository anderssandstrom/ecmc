from .base import SlitCase


class MotionTests(SlitCase):
    def test_physical_moves_both_directions(self):
        for axis in self.rig.profile['physical']:
            for target in (.2, -.2, 0):
                self.rig.move(axis, target)
                self.rig.kinematics()

    def test_stop_during_motion(self):
        self.rig.start_move('TR_LO', .7)
        self.rig.wait(lambda: self.rig.get('TR_LO', '-Busy') == 1, 'physical move starts', 3)
        self.rig.stop_motion('TR_LO')
        self.assertLess(self.rig.get('TR_LO', '-PosAct'), .65)

    def test_internal_encoder_wraparound_both_directions(self):
        # Zero crosses the unsigned 16-bit boundary without expanding soft limits.
        # In closed loop EL5042 is primary; the internal encoder is secondary.
        for axis in self.rig.profile['physical']:
            self.rig.move(axis, .1)
            for target, expected_wrap in ((-.1, 1), (.1, -1)):
                previous = self.rig.internal_counter(axis)
                wraps = []
                complete = self.rig.start_move(axis, target)
                def arrived():
                    nonlocal previous
                    self.assertEqual(self.rig.get(axis, '-ErrId'), 0)
                    position = self.rig.get(axis, '-PosAct')
                    self.assertGreaterEqual(position, -.13, 'encoder wrap must not cause a position jump')
                    self.assertLessEqual(position, .13, 'encoder wrap must not cause a position jump')
                    raw = self.rig.internal_counter(axis)
                    if raw - previous > 32768: wraps.append(1)
                    if raw - previous < -32768: wraps.append(-1)
                    previous = raw
                    return (complete.is_set() and self.rig.get(axis, '.DMOV') == 1
                            and self.rig.get(axis, '-Busy') == 0 and abs(position - target) < .015)
                self.rig.wait(arrived, axis + ' crosses internal encoder wrap and reaches target', 6)
                self.assertIn(expected_wrap, wraps, 'must observe the native counter wrapping in the commanded direction')
                self.assertEqual(self.rig.get(axis, '.LVIO'), 0)
                self.assertEqual(self.rig.get(axis, '.SEVR'), 0)
                self.rig.trace.append({'action': 'internal encoder wrap', 'axis': axis,
                                       'target': target, 'wraps': wraps, 'snapshot': self.rig.snapshot()})
            self.rig.move(axis, 0)
