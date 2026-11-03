#!/usr/bin/env python3
"""Execute isolation cases against controlled motion/CA boundaries; no IOC."""
from pathlib import Path
import sys
import threading
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
sys.path.insert(0, str(Path(__file__).resolve().parent / 'integration'))
from slit.cases.isolation import IsolationTests
from slit.rig import Client, Rig


class Requests:
    def __init__(self, rig): self.rig, self.count = rig, 0
    def put(self, name, value, **options):
        assert options == {'wait': False}
        axis = name[len('TEST:'):].split('.')[0].split('-')[0]
        assert axis in ('CENTERY', 'GAPY') and self.rig.mode == 'move'
        assert name.endswith(('.VAL', '-ExeCmd'))
        self.count += 1
        event = threading.Event()
        if self.rig.bad != 'pending': event.set()
        if self.rig.bad == 'accepted': self.rig.virtual_busy = 1
        return event


class Probe:
    prefix = 'TEST:'
    profile = {'axes': {'TR_LO': 5, 'TR_HI': 6, 'CENTERY': 12, 'GAPY': 13},
               'physical': {'TR_LO': {}, 'TR_HI': {}}}
    names = tuple(profile['axes'])
    def __init__(self, bad=None):
        self.bad, self.mode, self.active, self.direction = bad, 'idle', 'TR_HI', 1
        self.positions, self.fields = {a: 0 for a in self.profile['physical']}, {}
        self.moves, self.trace, self.virtual_busy = [], [], 0
        self.client = Requests(self)
        self.plant = self
    def check(self): pass
    def name(self, axis, field): return self.prefix + axis + field
    def get(self, axis, field):
        busy = int(self.mode in ('move', 'jog') and axis == self.active)
        if axis in ('CENTERY', 'GAPY'): busy = self.virtual_busy
        if field == '-ErrId':
            return int(axis == 'CENTERY' and (self.bad == 'virtual_fault' and self.mode in ('move','jog')
                                             or self.bad == 'stop_fault' and self.mode == 'stopped'))
        if field == '-PosAct':
            if axis == self.active and self.bad == 'travel': return .9
            if (axis != self.active and axis in self.positions and self.bad == 'other'
                    and self.mode in ('move','jog','stopped')): return .1
            return self.positions.get(axis, 0)
        if field == '-Blocked': return int(self.mode == 'move' and self.bad != 'unblocked')
        if field == '-TgtPosCmd': return .4 if self.bad == 'target' else .65
        if field in ('.JVEL', '.JAR'): return self.fields.get((axis,field), .4)
        if field in ('.JOGF', '.JOGR'): return self.fields.get((axis,field), 0)
        if field == '-VelAct': return busy*self.direction*.2
        return {'-Busy': busy, '.DMOV': 1-busy, '.MOVN': busy, '.MIP': 32 if busy else 0,
                '-TrjSrcTyp-RB': 0, '-EnaAct': 1}[field]
    def stage_native(self, axis, command, target, velocity):
        assert command == 3 and target == .65 and velocity == .05
        self.active = axis
    def execute_native(self, axis): self.mode = 'move'
    def put(self, axis, field, value):
        self.fields[(axis,field)] = value
        if field in ('.JOGF', '.JOGR') and value:
            self.active, self.mode = axis, 'jog'
            self.direction = 1 if field == '.JOGF' else -1
        elif field in ('-StpCmd', '.STOP') or (field in ('.JOGF', '.JOGR') and axis == self.active):
            if self.bad == 'ignored_stop': return
            self.mode = 'stopped'
            self.fields[(axis,'.JOGF')] = self.fields[(axis,'.JOGR')] = 0
            if self.bad == 'queued': self.virtual_busy = 1
    def tick(self):
        if self.mode in ('move', 'jog'):
            self.positions[self.active] += .01 * (self.direction if self.mode == 'jog' else 1)
    def wait(self, predicate, description, timeout=15):
        for _ in range(40):
            self.tick()
            if predicate(): return
        raise AssertionError(description)
    def wait_native_stopped(self, axis): assert self.mode == 'stopped'
    def assert_stays(self, predicate, description, seconds=1):
        for index in range(3):
            if index == 1 and self.bad == 'replay': self.mode = 'move'
            self.tick()
            assert predicate(), description
    def _drive_stationary(self, axis): return self.get(axis,'-Busy') == 0
    def move(self, axis, position):
        self.moves.append((axis,position))
        self.mode = 'idle'
        if axis in self.positions: self.positions[axis] = position
        elif axis == 'CENTERY':
            shift = position-sum(self.positions.values())/2
            self.positions = {a: p+shift for a,p in self.positions.items()}
    def kinematics(self): pass
    def snapshot(self): return {}


for interface in ('motor', 'native'):
    for bad in (None, 'pending', 'accepted', 'virtual_fault', 'stop_fault', 'other',
                'unblocked', 'target', 'travel', 'queued', 'replay'):
        c = IsolationTests('test_blocked_motor_commands_during_physical_move'); c.rig = Probe(bad)
        try: c.blocked_burst(interface)
        except AssertionError: assert bad is not None
        else:
            assert bad is None and c.rig.client.count == 24, 'unexpected pass: ' + str(bad)
            assert c.rig.moves[-2:] == [('CENTERY', .1), ('CENTERY', 0)]

for via_stop in (False, True):
    for bad in (None, 'virtual_fault', 'stop_fault', 'other', 'travel', 'ignored_stop', 'replay'):
        c = IsolationTests('test_physical_jogs_release_without_virtual_errors'); c.rig = Probe(bad)
        try: c.jog(via_stop)
        except AssertionError: assert bad is not None
        else:
            assert bad is None
            assert c.rig.moves == [('TR_LO',0),('TR_LO',0),('TR_HI',0),('TR_HI',0),
                                   ('CENTERY',.1),('CENTERY',0)]
        finally: assert c.doCleanups()

# Real Rig/Client sends both press and release without put-notify; jog release
# must not be queued behind a motion completion on that motor record.
class PV:
    connected = write_access = True
    def wait_for_connection(self, timeout): return True
    def put(self, value, **options):
        assert options['wait'] is False
        assert not options.get('callback') and not options.get('use_complete')
        return 1
rig = Rig({'axes': {}}, 'TEST:', Client(lambda *a, **k: PV()), None)
for field in ('.JOGF', '.JOGR'):
    for value in (1,0): assert rig.put('TR_HI',field,value) is None
print('Isolation cases: blocked motor/native bursts, both-direction physical jog release/STOP, virtual fault/replay rejection, fresh coordinated recovery and ordinary jog puts passed.')
