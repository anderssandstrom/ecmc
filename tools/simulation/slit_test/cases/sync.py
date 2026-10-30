from .base import SlitCase


class SyncTests(SlitCase):
    def test_slave_feedback_fault_stops_master_motion(self):
        if self.loop != 'closed':
            self.skipTest('requires primary EL5042 feedback')
        self.rig.start_move('CENTERY', .7)
        self.rig.wait(lambda: self.rig.get('TR_HI', '-PosAct') > .06, 'coordinated motion before encoder freeze', 5)
        self.rig.plant.inject('TR_HI', frozen=True)
        self.rig.fault_stopped('TR_HI', 0x14C08)
        self.rig.wait(lambda: all(self.rig.get(axis, '-Busy') == 0 for axis in self.rig.names),
                      'slave fault stops coordinated group', 5)

    def test_virtual_center(self):
        self.rig.move('CENTERY', .2)
        for axis in self.rig.profile['physical']:
            self.assertAlmostEqual(self.rig.get(axis, '-PosAct'), .2, delta=.025)
        self.rig.kinematics()

    def test_virtual_gap(self):
        self.rig.move('GAPY', .4)
        self.assertAlmostEqual(self.rig.get('TR_LO', '-PosAct'), -.2, delta=.025)
        self.assertAlmostEqual(self.rig.get('TR_HI', '-PosAct'), .2, delta=.025)
        self.rig.kinematics()

    def test_master_to_slave_to_master_transition(self):
        self.rig.move('CENTERY', .15)
        self.rig.move('TR_LO', -.15)
        self.rig.kinematics()
        gap = self.rig.get('TR_HI', '-PosAct') - self.rig.get('TR_LO', '-PosAct')
        self.rig.move('CENTERY', -.1)
        self.assertAlmostEqual(self.rig.get('TR_HI', '-PosAct') - self.rig.get('TR_LO', '-PosAct'), gap, delta=.03)
        self.rig.kinematics()

    def test_physical_axes_use_external_trajectory_during_master_move(self):
        self.rig.start_move('CENTERY', .6)
        self.rig.wait(lambda: all(self.rig.get(axis, '-TrjSrcTyp-RB') == 1
                                 for axis in self.rig.profile['physical']), 'master selects external slave trajectories', 5)
        self.assertEqual(int(self.rig.client.get(self.rig.prefix + 'SM00-Stat')) & 32768, 0)
        self.rig.wait(lambda: abs(self.rig.get('CENTERY', '-PosAct') - .6) < .02
                      and not self.rig.get('CENTERY', '-Busy'), 'master move completes', 10)
        self.rig.kinematics()
