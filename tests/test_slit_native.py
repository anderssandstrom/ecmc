#!/usr/bin/env python3
"""Exercise actual native case/helper code against controlled motion responses.

Negative controls reject motion before execute, ignored execute, wrong relative
mode, failure to stop, and controller faults. This checks the test harness, not
ECMC trajectory physics; production trajectory/monitor suites cover that layer.
"""
from pathlib import Path
import math
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
sys.path.insert(0, str(Path(__file__).resolve().parent / 'integration'))
from slit.rig import Rig
from slit.cases.native import NativeTests


class NativeRig(Rig):
    def __init__(self, defect=None):
        super().__init__({'axes': {'TR_LO': 5, 'TR_HI': 6, 'CENTERY': 12, 'GAPY': 13},
                          'physical': {'TR_LO': {}, 'TR_HI': {}}}, 'TEST:', None, None)
        self.defect = defect
        self.values = {a: {'-MtnCmd': 3, '-MtnCmdData': 0, '-TgtPosCmd': 0,
                           '-TgtVelCmd': .4, '-ExeCmd-RB': 1, '-EnaAct': 1} for a in self.names}
        self.positions = dict.fromkeys(self.names, 0.)
        self.active, self.writes = {}, []
    def get(self, axis, suffix):
        if suffix == '-ErrId': return 0x14c08 if self.defect == 'fault' else 0
        if suffix == '-Busy': return int(axis in self.active)
        if suffix == '-PosAct': return self.positions[axis]
        if suffix == '-VelAct': return self.active.get(axis, (0, 0))[1]
        return self.values[axis][suffix]
    def snapshot(self): return {'positions': dict(self.positions), 'active': dict(self.active)}
    def put(self, axis, suffix, value):
        assert suffix != '.VAL', 'native tests must not issue motor-record moves'
        self.writes.append((axis, suffix, value))
        values = self.values[axis]
        if suffix == '-EnaCmd':
            values['-EnaAct'] = value
        elif suffix == '-StpCmd':
            if self.defect != 'no_stop': self.active.pop(axis, None)
        elif suffix == '-ExeCmd':
            if not value and self.defect == 'zero_stops': self.active.pop(axis, None)
            # axisAsynWriteCmd auto-resets the command bit. The sequencer
            # execute status can remain high at idle and zero is not STOP.
            if value and axis not in self.active and self.defect != 'ignore_execute':
                values['-ExeCmd-RB'] = 1
                command, position = values['-MtnCmd'], values['-TgtPosCmd']
                if command == 1:
                    self.active[axis] = (None, values['-TgtVelCmd'])
                else:
                    target = position + (self.positions[axis] if command == 2 and self.defect != 'wrong_relative' else 0)
                    speed = math.copysign(values['-TgtVelCmd'], target-self.positions[axis])
                    self.active[axis] = (target, speed)
        else:
            values[suffix] = value
            if suffix == '-TgtPosCmd' and self.defect == 'early_motion': self.positions[axis] = value
    def step(self):
        for axis, (target, velocity) in list(self.active.items()):
            position = self.positions[axis] + velocity*.02
            if target is not None and (target-position)*velocity <= 0:
                position = target
                del self.active[axis]
            if axis == 'CENTERY':
                delta = position-self.positions[axis]
                for blade in self.profile['physical']: self.positions[blade] += delta
            elif axis == 'GAPY':
                delta = (position-self.positions[axis])/2
                self.positions['TR_LO'] -= delta
                self.positions['TR_HI'] += delta
            self.positions[axis] = position
        self.positions['CENTERY'] = (self.positions['TR_LO']+self.positions['TR_HI'])/2
        self.positions['GAPY'] = self.positions['TR_HI']-self.positions['TR_LO']
    def wait(self, predicate, description, timeout=15):
        for _ in range(math.ceil(timeout/.02)):
            self.step()
            if predicate(): return
        raise AssertionError(description)
    def assert_stays(self, predicate, description, seconds=1):
        for _ in range(math.ceil(seconds/.02)):
            self.step()
            assert predicate(), description
    def kinematics(self):
        assert abs(self.positions['CENTERY']-(self.positions['TR_LO']+self.positions['TR_HI'])/2) < .02


methods = ('test_staged_absolute_command_waits_for_execute',
           'test_relative_commands_repeat_triggers_in_both_directions',
           'test_velocity_commands_use_native_stop',
           'test_native_stop_interrupts_position_motion', 'test_native_center_and_gap_commands')
for name in methods:
    case = NativeTests(name)
    case.rig = NativeRig()
    getattr(case, name)()
    # Every command trigger is staged independently; execute status remains
    # high at idle throughout this fixture, reproducing the live timeout.
    assert all(v['-ExeCmd-RB'] == 1 for v in case.rig.values.values())
    # Clear the command record before staging without waiting for status zero.
    staged = False
    for axis, suffix, value in case.rig.writes:
        if suffix == '-ExeCmd' and value == 0: staged = True
        if suffix == '-ExeCmd' and value == 1:
            assert staged
            staged = False

for name, defect in ((methods[0], 'early_motion'), (methods[0], 'ignore_execute'),
                     (methods[1], 'wrong_relative'), (methods[2], 'no_stop'), (methods[2], 'zero_stops'),
                     (methods[3], 'no_stop'), (methods[4], 'fault')):
    case = NativeTests(name)
    case.rig = NativeRig(defect)
    try:
        getattr(case, name)()
    except AssertionError:
        pass
    else:
        raise AssertionError(name + ' missed ' + defect)

# Bad input is rejected before issuing any native write.
for command, velocity in ((0, .4), (3, -.4), (1, 0), (3, float('nan'))):
    rig = NativeRig()
    try: rig.stage_native('TR_LO', command, velocity=velocity)
    except ValueError: pass
    else: raise AssertionError('invalid native request accepted')
    assert not rig.writes
print('Native ECMC harness: ABS/REL/VEL, momentary execute requests, STOP and coordinated moves; negative controls passed.')
