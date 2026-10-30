#!/usr/bin/env python3
"""Execute live recovery/cancellation cases against controlled CA/plant boundaries.

A virtual clock verifies sustained observations without sleeping. Negative
controls deliberately restart after readiness/reset or release a cancelled
brake; these must fail. No IOC, hardware motion or production policy changes.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
import slit_test.rig as rig_module
from slit_test.rig import Rig
from slit_test.cases.feedback import FeedbackTests
from slit_test.cases.brake import BrakeTests


class Client:
    timeout = 1
    write_access = True
    pvname = 'TEST:TR_HI-EnaCmd'
    def __init__(self, rig): self.rig = rig
    def pv(self, name): return self
    def put(self, value, **kwargs):
        r = self.rig
        r.requested = bool(value)
        r.enabled = r.brake_value = False
        if value:
            r.amp = True
            r.open_at = r.clock + .25
        else:
            r.amp = False
            if not r.late_open: r.open_at = None
        return 1
    def get(self, name):
        r = self.rig
        if 'encoderStatus' in name:
            assert name == 'TEST:SIM:ec0:s9:encoderStatus01-RB'
            return 4 if r.ready else 0
        if 'driveControl' in name: return int(r.amp)
        if 'driveStatus' in name: return int(r.amp) << 1
        if 'velocitySetpoint' in name: return 0
        raise AssertionError(name)


class Plant:
    def __init__(self, rig): self.rig = rig
    def inject(self, axis, **values):
        r = self.rig
        assert set(values) == {'ready'}
        r.ready = values['ready']
        if not r.ready and r.closed and r.enabled:
            r.error = 0x14417
            r.enabled = r.requested = r.amp = r.brake_value = r.busy = False
        if r.ready and r.restart_ready:
            r.enabled = r.requested = r.amp = r.brake_value = r.busy = True
            r.error = 0


class RecoveryRig(Rig):
    def __init__(self, closed=True, restart_ready=False, restart_reset=False, late_open=False):
        profile = {'axes': {'TR_HI': 6}, 'physical': {'TR_HI': {
            'drive_slave': 12, 'encoder_slave': 9, 'encoder_channel': '01',
            'brake': {'slave': 5, 'channel': '02'}}}}
        super().__init__(profile, 'TEST:', None, None)
        self.client, self.plant = Client(self), Plant(self)
        self.clock, self.error, self.position = 0, 0, 0
        self.ready = self.enabled = self.requested = self.amp = self.brake_value = True
        self.busy, self.open_at = False, None
        self.closed, self.restart_ready, self.restart_reset = closed, restart_ready, restart_reset
        self.late_open, self.moves, self.resets = late_open, [], 0
    def get(self, axis, suffix):
        return {'-EnaCmd-RB': int(self.requested), '-EnaAct': int(self.enabled),
                '-Busy': int(self.busy), '-ErrId': self.error, '-PosAct': self.position}[suffix]
    def brake(self, axis): return int(self.brake_value)
    def snapshot(self):
        return {'error': self.error, 'enable': self.enabled, 'brake': self.brake_value, 'seconds': self.clock}
    def wait(self, predicate, description, timeout=15):
        deadline = self.clock + timeout
        while self.clock <= deadline:
            if self.open_at is not None and self.clock >= self.open_at:
                if self.requested or self.late_open:
                    self.brake_value = self.enabled = self.amp = True
                self.open_at = None
            if predicate(): return
            self.clock += .02
        raise AssertionError('timeout: ' + description)
    def start_move(self, axis, target):
        assert self.ready or not self.closed
        self.position = .07
        self.enabled = self.requested = self.brake_value = self.amp = self.busy = True
    def move(self, axis, target):
        assert self.ready or not self.closed
        assert self.error == 0
        self.moves.append(target)
        self.position = target
        self.enabled = self.requested = self.brake_value = self.amp = True
        self.busy = False
    def reset(self):
        self.resets += 1
        assert self.ready
        self.error = 0
        if self.restart_reset:
            self.enabled = self.requested = self.amp = self.brake_value = self.busy = True
    def stop_all(self):
        self.enabled = self.requested = self.amp = self.brake_value = self.busy = False
        self.open_at = None
        return []


def run(case_type, method, rig):
    case = case_type(method)
    case.rig, case.loop = rig, 'closed' if rig.closed else 'open'
    original = rig_module.time.monotonic
    try:
        rig_module.time.monotonic = lambda: rig.clock
        getattr(case, method)()
    finally:
        rig_module.time.monotonic = original


recovery = 'test_encoder_readiness_recovery_requires_reset_and_new_command'
rig = RecoveryRig()
run(FeedbackTests, recovery, rig)
assert rig.resets == 1 and rig.moves == [0] and rig.clock >= 2
for rig in (RecoveryRig(restart_ready=True), RecoveryRig(restart_reset=True)):
    try:
        run(FeedbackTests, recovery, rig)
    except AssertionError:
        assert rig.moves == []  # Must fail before a new explicit move.
    else:
        raise AssertionError('automatic restart must fail the recovery test')

rig = RecoveryRig(closed=False)
run(FeedbackTests, 'test_unready_secondary_encoder_does_not_stop_internal_feedback_motion', rig)
assert rig.moves == [.2, 0] and not rig.ready and rig.error == 0

cancel = 'test_cancel_enable_while_brake_opening'
rig = RecoveryRig()
run(BrakeTests, cancel, rig)
assert not rig.brake_value and not rig.amp and rig.clock >= 1
try:
    run(BrakeTests, cancel, RecoveryRig(late_open=True))
except AssertionError as error:
    assert 'cancelled opening' in str(error)
else:
    raise AssertionError('a delayed brake release after cancellation must fail')
print('Slit recovery: explicit restart, secondary isolation and cancellation pass; automatic restart and late brake release fail.')
