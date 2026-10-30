#!/usr/bin/env python3
"""Exercise live boundary case verdicts with controlled plant/CA responses.

Negative controls must reject travel into a limit, an automatically replayed
move after bounce, failed coordinated STOP, absent wrap and position jumps.
These probes validate the harness; production encoder arithmetic is separately
stepped by test_monitor.py. No IOC or real controller is simulated here.
"""
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
from slit_test.cases.limits import LimitTests
from slit_test.cases.motion import MotionTests
from slit_test.cases.sync import SyncTests


class Rig:
    def __init__(self, defect=None):
        self.names = ('TR_LO', 'TR_HI', 'CENTERY', 'GAPY')
        self.profile = {'physical': {'TR_LO': {}, 'TR_HI': {}}}
        self.plant = self
        self.trace = []
        self.defect = defect
        self.positions = {axis: 0 for axis in self.names}
        self.targets, self.events, self.limits = {}, {}, {}
        self.interrupted = {}
        self.escapes = 0
    def snapshot(self): return dict(self.positions)
    def wait(self, predicate, description, timeout=15):
        for _ in range(100):
            self.step()
            if predicate(): return
        raise AssertionError(description)
    def assert_stays(self, predicate, description, seconds=1):
        for _ in range(30):
            self.step()
            assert predicate(), description
    def step(self):
        for axis, target in list(self.targets.items()):
            current = self.positions[axis]
            delta = max(-.02, min(.02, target-current))
            self.positions[axis] += delta
            if self.defect == 'jump': self.positions[axis] = 5
            if abs(target-self.positions[axis]) < 1e-8:
                self.positions[axis] = target
                del self.targets[axis]
                self.events[axis].set()
        self.positions['CENTERY'] = (self.positions['TR_LO'] + self.positions['TR_HI']) / 2
    def get(self, axis, suffix):
        if suffix == '-PosAct': return self.positions[axis]
        if suffix in ('-ErrId', '.SEVR', '.LVIO'): return int(self.defect == 'fault')
        if suffix == '-Busy': return int(bool(self.targets) if axis == 'CENTERY' else axis in self.targets)
        if suffix == '.DMOV': return int(axis not in self.targets)
        if suffix == '-VelAct': return .4 if axis in self.targets else 0
        if suffix in ('-LimFwd', '-LimBwd'):
            return int(self.limits.get((axis, suffix == '-LimFwd'), True))
        if suffix in ('-SumIlockFwd', '-SumIlockBwd'):
            return int(not self.limits.get((axis, suffix == '-SumIlockFwd'), True))
        raise AssertionError(suffix)
    def _drive_stationary(self, axis): return axis not in self.targets
    def move(self, axis, target):
        for forward in (False, True):
            if not self.limits.get((axis, forward), True):
                assert (target < self.positions[axis]) == forward, 'move into held limit'
                assert self.defect != 'no_escape', 'healthy escape rejected'
                self.escapes += 1
        if axis == 'CENTERY':
            for blade in self.profile['physical']: self.positions[blade] = target
        self.positions[axis] = target
    def start_move(self, axis, target):
        event = threading.Event()
        for blade in (self.profile['physical'] if axis == 'CENTERY' else (axis,)):
            self.targets[blade], self.events[blade] = target, event
        return event
    def put(self, axis, suffix, target):
        assert suffix == '.VAL'
        event = threading.Event()
        if self.defect == 'ignore_limit': self.positions[axis] = target
        event.set()
        return event
    def inject(self, axis, **values):
        for field, ok in values.items():
            forward = field == 'forward_ok'
            self.limits[axis, forward] = ok
            if not ok and axis in self.targets:
                self.interrupted[axis] = self.targets.pop(axis)
                self.events[axis].set()
            if ok and self.defect == 'replay' and axis in self.interrupted:
                self.start_move(axis, self.interrupted[axis])
    def stop_motion(self, axis):
        assert axis == 'CENTERY'
        for blade in self.profile['physical']:
            self.positions[blade] += .04
            if self.defect == 'no_stop': self.positions[blade] = .7
            self.targets.pop(blade, None)
        self.positions[axis] = (self.positions['TR_LO'] + self.positions['TR_HI']) / 2
    def kinematics(self): pass
    def internal_counter(self, axis):
        if self.defect == 'no_wrap': return 1000
        return round(self.positions[axis]*12800) & 0xffff


def run(cls, method, defects):
    for defect in (None, *defects):
        case = cls(method)
        case.rig, case.loop = Rig(defect), 'open'
        try:
            getattr(case, method)()
        except AssertionError:
            assert defect is not None, method
        else:
            assert defect is None, method + ' missed ' + str(defect)
            if 'escape' in method: assert case.rig.escapes == 2
        if cls is LimitTests:
            assert all(case.rig.limits.values()), 'limit input left injected after failure'


for method in ('test_forward_limit_allows_backward_escape', 'test_backward_limit_allows_forward_escape'):
    run(LimitTests, method, ('ignore_limit', 'no_escape'))
for method in ('test_forward_limit_bounce_does_not_restart_motion', 'test_backward_limit_bounce_does_not_restart_motion'):
    run(LimitTests, method, ('replay', 'fault'))
run(SyncTests, 'test_stop_during_coordinated_motion', ('no_stop', 'fault'))
run(MotionTests, 'test_internal_encoder_wraparound_both_directions', ('no_wrap', 'jump', 'fault'))
print('Slit boundaries: actual live case methods reject ignored limits, bounce replay, failed STOP, missing wraps and jumps.')
