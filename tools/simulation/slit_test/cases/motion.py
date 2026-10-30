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
