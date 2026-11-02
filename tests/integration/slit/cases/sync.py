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

    def test_stop_during_coordinated_motion(self):
        self.rig.start_move('CENTERY', .7)
        self.rig.wait(lambda: self.rig.get('CENTERY', '-Busy') == 1
                      and all(self.rig.get(axis, '-PosAct') > .06
                              for axis in self.rig.profile['physical']),
                      'both blades moving before coordinated STOP', 5)
        before = {axis: self.rig.get(axis, '-PosAct') for axis in self.rig.profile['physical']}
        self.rig.stop_motion('CENTERY')
        def stopped():
            for axis in self.rig.names:
                self.assertEqual(self.rig.get(axis, '-ErrId'), 0, axis + ' fault during coordinated STOP')
            return (self.rig.get('CENTERY', '-Busy') == 0
                    and all(self.rig._drive_stationary(axis) and abs(self.rig.get(axis, '-VelAct')) < .01
                            for axis in self.rig.profile['physical']))
        self.rig.wait(stopped, 'both blades finish coordinated ramp-down without faults', 5)
        positions = {axis: self.rig.get(axis, '-PosAct') for axis in self.rig.profile['physical']}
        for axis, position in positions.items():
            self.assertLess(position, .65, axis + ' must stop before the original target')
            self.assertGreaterEqual(position, before[axis] - .015, axis + ' must not jump backwards')
        self.assertAlmostEqual(positions['TR_LO'], positions['TR_HI'], delta=.03)
        self.rig.assert_stays(stopped, 'coordinated STOP remains stationary and fault-free', seconds=.5)
        self.rig.kinematics()
        self.rig.move('CENTERY', 0)  # A fresh command must recover normal coordinated motion.

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
