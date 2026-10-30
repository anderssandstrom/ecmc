"""EL5042 measurement noise, using the configured .01 mm target tolerance."""
from .base import SlitCase


class NoiseTests(SlitCase):
    def at_target_axis(self):
        if self.loop != 'closed':
            self.skipTest('requires primary EL5042 feedback and position control')
        axis = 'TR_HI'
        self.rig.move(axis, .2)
        self.rig.wait(lambda: self.rig.get(axis, '-AtTarget') == 1,
                      'closed-loop axis initially at target', 3)
        self.healthy(axis)
        return axis

    def healthy(self, axis):
        self.assertEqual(self.rig.get(axis, '-ErrId'), 0, 'noise must not cause an axis fault')
        self.assertEqual(self.rig.get(axis, '-EnaAct'), 1, 'axis must remain enabled')

    def test_small_encoder_noise_preserves_at_target(self):
        axis = self.at_target_axis()
        self.rig.plant.inject(axis, encoder_noise_mm=.001)
        try:
            def stable():
                self.healthy(axis)
                return self.rig.get(axis, '-AtTarget') == 1
            self.rig.assert_stays(stable, 'small encoder noise preserves at-target', seconds=1)
        finally:
            self.rig.plant.inject(axis, encoder_noise_mm=0)

    def test_encoder_noise_loses_at_target_then_recovers(self):
        axis = self.at_target_axis()
        # Above the .01 target band, below the .02 following-error tolerance.
        self.rig.plant.inject(axis, encoder_noise_mm=.014)
        try:
            def lost():
                self.healthy(axis)
                error = abs(self.rig.get(axis, '-PosAct') - self.rig.get(axis, '-PosSet'))
                return error > .011 and self.rig.get(axis, '-AtTarget') == 0
            self.rig.wait(lost, 'encoder noise clears at-target', 3)
            def powered():
                self.healthy(axis)
                return True
            self.rig.assert_stays(powered, 'noise leaves the axis enabled without faults', seconds=1)
        finally:
            self.rig.plant.inject(axis, encoder_noise_mm=0)
        self.rig.wait(lambda: self.rig.get(axis, '-AtTarget') == 1,
                      'at-target recovers after removing encoder noise', 3)
        def recovered():
            self.healthy(axis)
            return self.rig.get(axis, '-AtTarget') == 1
        self.rig.assert_stays(recovered, 'recovered at-target stays stable', seconds=.5)
        self.rig.move(axis, 0)
