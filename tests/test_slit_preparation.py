#!/usr/bin/env python3
"""Preparation after fault recovery: delayed plant/reset/motor acknowledgements.

Controlled CA values model a motor target still at zero while actual position
is 0.067 mm. A same-target write can be a no-op. Real Rig.prepare/reset/SYNC
logic must establish fresh readback and synchronize before requesting zero.
This verifies setup sequencing, not the live cause of a missing motor command.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
from slit_test.rig import Rig


class Plant:
    def __init__(self, rig): self.rig = rig
    def clear_faults(self): self.rig.healthy_at = self.rig.clock + .03


class Client:
    def __init__(self, rig): self.rig = rig
    def put(self, name, value):
        r = self.rig
        if name == 'TEST:SM00-StateCmd':
            assert value == 3
            r.reset_at = r.clock + (.04 if not r.block_reset else 10)
            r.readback_at = r.reset_at + .03
            r.problem_at = r.reset_at + (10 if r.block_problem else .3)
            r.resets += 1
    def get(self, name):
        if name == 'TEST:SM00-StateCmd': return 3 if self.rig.clock < self.rig.reset_at else 0
        raise AssertionError(name)


class PreparationRig(Rig):
    def __init__(self, block_reset=False, block_sync=False, block_problem=False):
        super().__init__({'axes': {'TR_HI': 6}, 'physical': {'TR_HI': {}}}, 'TEST:', None, None)
        self.client, self.plant = Client(self), Plant(self)
        self.clock, self.healthy_at, self.reset_at, self.readback_at = 0, 100, 0, 0
        self.position, self.motor_target = .067, 0
        self.sync_at, self.sync_value = None, None
        self.resets, self.syncs, self.moves = 0, 0, []
        self.block_reset, self.block_sync = block_reset, block_sync
        self.enabled = False
        self.problem_at = 0
        self.block_problem = block_problem
    def wait(self, predicate, description, timeout=15):
        deadline = self.clock + timeout
        while self.clock <= deadline:
            if self.sync_at is not None and self.clock >= self.sync_at:
                self.motor_target, self.sync_at = self.sync_value, None
            if predicate(): return
            self.clock += .01
        raise AssertionError('timeout: ' + description)
    def encoder_status(self, axis): return 4 if self.clock >= self.healthy_at else 0
    def drive_status(self, axis): return 0 if self.clock >= self.healthy_at else 8
    def get(self, axis, suffix):
        return {'-ErrId': 0, '-Busy': 0, '.DMOV': 1, '-PosAct': self.position,
                '.MSTA': 512 if self.clock < self.problem_at else 0,
                '.RBV': self.position if self.clock >= self.readback_at else 0,
                '.VAL': self.motor_target, '.SYNC': int(self.sync_at is not None)}[suffix]
    def put(self, axis, suffix, value):
        assert self.clock >= self.healthy_at, 'reset before restored plant input was published'
        if suffix == '.SYNC':
            assert value == 1 and not self.enabled
            assert self.clock >= self.reset_at and self.clock >= self.readback_at
            assert self.clock >= self.problem_at, 'stale motor problem bit at synchronization'
            self.syncs += 1
            self.sync_at = self.clock + (10 if self.block_sync else .02)
            self.sync_value = self.get(axis, '.RBV')
    def stop_all(self):
        self.enabled = False
        return []
    def wait_drive_disabled(self, axis, timeout=5):
        assert not self.enabled
    def move(self, axis, target):
        assert self.clock >= self.reset_at, 'state-machine reset can still cancel the move'
        assert self.sync_at is None, 'motor synchronization still pending'
        if target == self.motor_target and abs(self.position - target) > .015:
            raise AssertionError('same-target motor no-op with displaced physical position')
        self.moves.append(target)
        self.motor_target = self.position = target
        self.enabled = True
    def kinematics(self): assert abs(self.position) < .015


rig = PreparationRig()
try:
    rig.move('TR_HI', 0)
except AssertionError as error:
    assert 'same-target motor no-op' in str(error)
else:
    raise AssertionError('fixture must reproduce stale-target setup failure')
rig.prepare('closed')
assert rig.resets == 2 and rig.syncs == 1 and rig.moves == [0]
assert rig.position == 0 and rig.clock >= rig.readback_at

for rig, expected in ((PreparationRig(block_reset=True), 'RESET completion'),
                      (PreparationRig(block_sync=True), 'target synchronized'),
                      (PreparationRig(block_problem=True), 'cleared problem bit')):
    try:
        rig.prepare('closed')
    except AssertionError as error:
        assert expected in str(error)
        assert rig.moves == []
    else:
        raise AssertionError('unacknowledged setup operation must prevent motion')
print('Slit preparation: restored inputs, RESET and fresh motor SYNC precede zero move; missing acknowledgements fail setup.')
