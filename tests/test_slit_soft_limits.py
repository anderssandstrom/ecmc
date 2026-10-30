#!/usr/bin/env python3
"""Live-harness soft-limit verdicts without an IOC.

DOC: ecmcAxisSequencer::checkSoftLimits sets direction-specific warning IDs;
ecmcMonitor::checkLimitsInterlock sets predictive target interlocks. This can
stop at the starting position without a fatal error or reaching the boundary.
An unrelated error, stale/no diagnostic or overrun must not count as success.
"""
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
from slit_test.cases.limits import LimitTests, controller_soft_limit_stopped

sample = {'position': 0, 'setpoint': 0, 'warning': 0x114D00, 'error': 0,
          'interlock': 1, 'busy': 0, 'stationary': True}
assert controller_soft_limit_stopped(sample, True, -1, 1)
assert controller_soft_limit_stopped({**sample, 'interlock': 0}, True, -1, 1)
assert controller_soft_limit_stopped({**sample, 'warning': 0}, True, -1, 1)
assert controller_soft_limit_stopped({**sample, 'warning': 0x114D01}, False, -1, 1)
# Early predictive stops and boundary stops both require a specific response.
assert controller_soft_limit_stopped({**sample, 'position': .999, 'setpoint': 1}, True, -1, 1)
assert not controller_soft_limit_stopped({**sample, 'warning': 0, 'interlock': 0}, True, -1, 1)
assert not controller_soft_limit_stopped({**sample, 'busy': 1}, True, -1, 1)
assert not controller_soft_limit_stopped({**sample, 'stationary': False}, True, -1, 1)

for change in ({'warning': 0x114D01}, {'error': 0x14C08}, {'error': 0x14417},
               {'position': 1.1}, {'setpoint': -1.1}):
    try:
        controller_soft_limit_stopped({**sample, **change}, True, -1, 1)
    except AssertionError:
        pass
    else:
        raise AssertionError('must reject wrong-direction/unrelated fault or overrun: ' + repr(change))

print('Slit soft limits: predictive warning/interlock stops pass; no-op, moving, wrong-direction, unrelated fault and overrun do not.')


class MotorRig:
    """Rejected targets roll back VAL; same-target writes retain prior LVIO."""
    def __init__(self):
        self.position = self.target = 0
        self.low, self.high, self.lvio = -1, 1, 1
        self.moves, self.rejections, self.writes = [], [], []
    def get(self, axis, suffix):
        return {'.LLM': self.low, '.HLM': self.high, '.LVIO': self.lvio,
                '-PosAct': self.position}[suffix]
    def move(self, axis, target):
        if target == self.target and self.lvio:
            raise RuntimeError('same-target no-op retains stale LVIO')
        assert self.low <= target <= self.high
        self.position = self.target = target
        self.lvio = 0
        self.moves.append(target)
    def put(self, axis, suffix, value):
        self.writes.append((suffix, value))
        if suffix == '.LLM': self.low = value
        elif suffix == '.HLM': self.high = value
        elif suffix == '.VAL':
            assert not self.low <= value <= self.high
            assert self.lvio == 0  # Each rejection must establish fresh LVIO.
            self.lvio = 1
            self.rejections.append((value, self.position))
            complete = threading.Event()
            complete.set()
            return complete
        else:
            raise AssertionError('test must not manually clear LVIO')
    def wait(self, predicate, description, timeout):
        assert predicate(), description


rig = MotorRig()
case = LimitTests('test_motor_soft_limits')
case.rig = rig
case.test_motor_soft_limits()
assert rig.moves == [-.1, .1]
assert rig.rejections == [(.6, -.1), (-.6, .1)]
assert (rig.low, rig.high) == (-1, 1)
assert all(suffix != '.LVIO' for suffix, _ in rig.writes)
print('Motor soft-limit sequence: distinct valid moves clear stale LVIO, both invalid targets reject without motion, original bounds restored.')


class ControllerRig:
    """Retain a rejected target across disable/reset, as in the live failure."""
    def __init__(self):
        self.position = self.target = 0
        self.enabled, self.execute = True, False
        self.warning = self.interlock = 0
        self.trace, self.requests, self.recoveries = [], [], 0
    def get(self, axis, suffix):
        return {'.LLM': -1, '.HLM': 1, '.LVIO': 0, '-EnaAct': self.enabled,
                '-PosAct': self.position, '-PosSet': self.position, '-Busy': 0,
                '-ErrId': 0, '-WrnId': self.warning, '-TgtPosCmd': self.target,
                '-SftLimBwdEna-RB': 1, '-SftLimFwdEna-RB': 1,
                '-SftLimBwdIlock': self.interlock == -1,
                '-SftLimFwdIlock': self.interlock == 1}[suffix]
    def put(self, axis, suffix, value):
        if suffix == '-TgtPosCmd': self.target = value
        elif suffix == '-ExeCmd':
            self.execute = bool(value)
            if value:
                assert self.enabled
                self.requests.append(self.target)
                if -1 <= self.target <= 1:
                    self.position = self.target
                    self.warning = self.interlock = 0
                else:
                    self.interlock = 1 if self.target > 1 else -1
                    self.warning = 0x114D00 if self.target > 1 else 0x114D01
        elif suffix == '-EnaCmd' and not self.enabled:
            # Recovery must not carry the old rejected target/execute request
            # into a write of another bit in the shared control word.
            assert -1 <= self.target <= 1, 'stale rejected target during enable'
            assert not self.execute, 'pending execute during enable'
            self.enabled = True
            self.recoveries += 1
    def wait(self, predicate, description, timeout):
        assert predicate(), description
    def _drive_stationary(self, axis): return True
    def snapshot(self): return {'target': self.target, 'enabled': self.enabled}
    def stop_all(self):
        self.enabled = False
        return []
    def reset(self):
        self.warning = self.interlock = 0
        # Model a pending execute/readback retained with the rejected target.
        self.execute = True


rig = ControllerRig()
case = LimitTests('test_controller_soft_limits_without_motor_rejection')
case.rig = rig
case.test_controller_soft_limits_without_motor_rejection()
assert rig.recoveries == 2
assert [target for target in rig.requests if abs(target) > 1] == [1.2, -1.2]
assert rig.requests[-1] == 0 and rig.position == 0
print('Controller soft-limit recovery: replace rejected target and clear execute before re-enabling; both directions tested.')
