#!/usr/bin/env python3
"""Run interface-burst verdicts against controlled boundaries; no live writes."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
sys.path.insert(0, str(Path(__file__).resolve().parent / 'integration'))
from slit.rig import Rig
from slit.cases.interfaces import InterfaceTests


class Client:
    timeout = 1
    def __init__(self, rig): self.rig, self.writes = rig, []
    def get(self, name): return self.rig.enabled
    def put(self, name, value, **kwargs):
        self.writes.append((name, value, kwargs))
        if name.endswith('SM00-EnaCmd'): self.rig.enabled = value
        elif name.endswith('.VAL'):
            assert kwargs == dict(wait=False, notify=False)
            if self.rig.bad == 'write': raise RuntimeError('write rejected')
            self.rig.positions['TR_HI'] = value


class Probe:
    prefix = 'TEST:'
    profile = {'axes': {'TR_LO': 5, 'TR_HI': 6}, 'physical': {'TR_LO': {}, 'TR_HI': {}}}
    def __init__(self, bad=None):
        self.bad, self.enabled, self.busy = bad, 1, 0
        self.positions, self.moves, self.trace = {'TR_LO': 0, 'TR_HI': 0}, [], []
        self.client = Client(self)
        self.commands, self.closed = [], False
    def name(self, axis, suffix): return self.prefix + axis + suffix
    def get(self, axis, suffix):
        if suffix == '-PosAct': return .9 if self.bad == 'travel' else self.positions[axis]
        return {'-Busy': self.busy, '.DMOV': 1-self.busy, '.MOVN': self.busy,
                '-TrjSrcTyp-RB': 0, '.PACT': 0, '.LVIO': 0, '.SEVR': 0}[suffix]
    def check_native_error(self, axis):
        if self.bad == 'error': raise AssertionError('native error')
    def stage_native(self, axis, command, position, velocity):
        assert command == 3 and velocity == .2
    def execute_native(self, axis): self.busy = 1
    def put(self, axis, suffix, value):
        if suffix == '-StpCmd':
            if self.bad != 'stop': self.busy = 0
        elif suffix == '-TgtPosCmd': self.positions[axis] = value
    def wait(self, predicate, description, timeout=15):
        assert predicate(), description
    def wait_native_stopped(self, axis): assert self.busy == 0
    def assert_stays(self, predicate, description, seconds=1):
        if self.bad == 'restart': self.busy = 1
        assert predicate(), description
    def move(self, axis, target):
        self.positions[axis] = target
        if self.bad == 'coupled': self.positions['TR_LO' if axis == 'TR_HI' else 'TR_HI'] += .1
        self.moves.append(('motor', axis, target))
    def native_move(self, axis, target):
        self.positions[axis] = target
        self.moves.append(('native', axis, target))
    def stop_all(self): self.busy = 0; return []
    def snapshot(self): return {}
    def open_text_commands(self): return {'.TMOD': 0}
    def close_text_commands(self, saved): self.closed = True
    def text_command(self, command):
        assert not command.endswith('.bExecute=0'), 'text burst must not request STOP between targets'
        self.commands.append(command)


for interface in ('motor', 'native', 'mixed', 'text'):
    for bad in (None, 'stop', 'restart', 'error', 'travel'):
        c = InterfaceTests('test_repeated_motor_commands'); c.rig = Probe(bad)
        try: c.burst(interface)
        except (AssertionError, RuntimeError): assert bad is not None
        else:
            assert bad is None
            assert c.rig.moves == [('motor', 'TR_HI', .15), ('native', 'TR_HI', -.15), ('motor', 'TR_HI', 0)]
            expected = {'motor': 24, 'native': 72, 'mixed': 48, 'text': 48}[interface]
            assert c.rig.trace[-1]['writes'] == expected
            if interface == 'text':
                assert sum(cmd.endswith('.bExecute=1') for cmd in c.rig.commands) == 24
                assert sum('.fPosition=' in cmd for cmd in c.rig.commands) == 24
        finally: c.doCleanups()
        if interface == 'text' and bad in (None, 'stop', 'restart'): assert c.rig.closed

for bad in (None, 'coupled'):
    c = InterfaceTests('test_physical_motion_with_state_machine_disabled'); c.rig = Probe(bad)
    try: c.test_physical_motion_with_state_machine_disabled()
    except AssertionError: assert bad == 'coupled'
    else: assert bad is None
    finally: c.doCleanups()
    assert c.rig.enabled == 1

# Actual command transport: exact acknowledgement/query checks and restoration.
class TextClient:
    timeout = 1
    def __init__(self):
        self.fields = {'.TMOD': 2, '.IFMT': 1, '.OFMT': 1, '.SEVR': 0}
        self.reply = 'OK;\n'
    def get(self, name): return self.fields[name.removeprefix('TEST:MCU-Cmd')]
    def put(self, name, value): self.fields[name.removeprefix('TEST:MCU-Cmd')] = value
    def pv(self, name): return self
    def get_reply(self, **kwargs): return self.reply

client = TextClient()
rig = Rig({'axes': {}}, 'TEST:', client, None)
saved = rig.open_text_commands()
assert client.fields['.TMOD'] == 'Write/Read'
# asynRecord ASCII input is AINP (AIN is not a valid field).
class ReplyPV:
    def get(self, **kwargs): return client.reply
def reply_pv(name):
    assert name == 'TEST:MCU-Cmd.AINP', 'unknown asynRecord input field: ' + name
    return ReplyPV()
client.pv = reply_pv
rig.text_command('Cfg.SetAxisHomePostMoveEnable(6,1)')
client.reply = '0.2;\n'
assert rig.text_command('GetAxisHomePostMoveTargetPosition(6)', query=True) == .2
for reply, severity in (('Error: 123', 0), ('', 0), ('OK', 2), (None, 0)):
    client.reply, client.fields['.SEVR'] = reply, severity
    try: rig.text_command('Cfg.SetAxisHomePostMoveEnable(6,1)')
    except AssertionError: pass
    else: raise AssertionError('accepted failed command reply')
rig.close_text_commands(saved)
assert all(client.fields[k] == v for k,v in saved.items())
client.reply, client.fields['.SEVR'] = 'OK', 0
rig.text_command('X' * 39)
before = dict(client.fields)
try: rig.text_command('X' * 40)
except ValueError as error: assert '39 bytes' in str(error)
else: raise AssertionError('oversized ASCII command must fail before writing a truncated request')
assert client.fields == before
print('Interface cases: motor/native/mixed/text bursts, STOP/no replay, fresh motion, disabled-SM isolation and command replies passed.')

from slit.cases.motor import MotorTests
import threading

class ToleranceProbe:
    profile = {'axes': {'TR_HI': 6}}
    def __init__(self, bad=None):
        self.bad, self.tolerance, self.stopped = bad, .01, False
        self.event, self.moves = threading.Event(), []
    def open_text_commands(self): return {}
    def close_text_commands(self, saved): pass
    def text_command(self, command, query=False):
        if query: return self.tolerance
        self.tolerance = float(command.split(',')[1].rstrip(')'))
    def start_move(self, axis, target): return self.event
    def stage_native(self, axis, command, target): assert command == 3 and target == .2
    def execute_native(self, axis): pass
    def get(self, axis, suffix):
        done = self.stopped or self.bad == 'early_done'
        return {'-PosAct': .2, '-AtTarget': 0, '.DMOV': int(done), '.MOVN': int(not done)}[suffix]
    def _drive_stationary(self, axis): return True
    def check_native_error(self, axis): pass
    def wait(self, predicate, description, timeout=15): assert predicate(), description
    def assert_stays(self, predicate, description, seconds=1): assert predicate(), description
    def put(self, axis, suffix, value):
        assert suffix == '-StpCmd' and value == 1
        if self.bad != 'ignored_stop': self.stopped = True; self.event.set()
    def move(self, axis, target):
        assert self.tolerance == .01
        self.moves.append(target)

for trigger in ('motor', 'native'):
    for bad in (None, 'early_done', 'ignored_stop'):
        c = MotorTests('test_zero_at_target_tolerance_motor'); c.rig = ToleranceProbe(bad)
        try: c.zero_target_tolerance(trigger)
        except AssertionError: assert bad is not None
        else: assert bad is None and c.rig.moves == [-.15, 0]
        finally: c.doCleanups()
        assert c.rig.tolerance == .01
print('Zero AtTarget tolerance: incomplete requests and STOP recovery pass; early DMOV and ignored STOP fail; tolerance restored.')


# Real Stop acknowledgement predicate: native stopped precedes motor callbacks.
class CancelProbe:
    def __init__(self, bad=None): self.tick, self.bad = 0, bad
    def check_native_error(self, axis):
        if self.bad == 'error' and self.tick == 1: raise AssertionError('native fault')
    def _drive_stationary(self, axis): return not (self.bad == 'velocity' and self.tick == 1)
    def get(self, axis, field):
        ready = self.tick >= 2
        if field == '-Busy': return int(self.bad == 'restart' and self.tick == 1)
        if field == '-PosAct': return .55 if self.bad == 'drift' and self.tick == 1 else .5
        if field in ('.VAL', '.DVAL'):
            return .5 if ready and self.bad != 'target' else -.7
        return {'.SPMG': 0, '.DMOV': int(ready), '.MOVN': int(not ready),
                '.MIP': 0 if ready else 512, '.RBV': .5}[field]
    def wait(self, predicate, description, timeout=15):
        for self.tick in range(4):
            if predicate(): return
        raise AssertionError(description)

for bad in (None, 'target', 'restart', 'drift', 'velocity', 'error'):
    c = MotorTests('test_spmg_pause_resume_and_stop_cancel'); c.rig = CancelProbe(bad)
    try: c.wait_motor_stop_cancelled(.5)
    except AssertionError: assert bad is not None
    else: assert bad is None and c.rig.tick == 2
print('SPMG cancellation: wait for motor target/flags after native standstill; stale target, replay, drift and faults fail.')
