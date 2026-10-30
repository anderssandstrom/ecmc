#!/usr/bin/env python3
"""Motor put-completion semantics for the live slit harness, without EPICS.

DOC: PyEpics PV.put(wait=False, use_complete=True, callback=...). Motor VAL
put-notify may finish with the motion, so the one-second configuration-write
timeout is not a valid motion-completion timeout. Motion state, final position,
axis errors and motor alarms remain separate acceptance checks.
"""
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
from slit_test.rig import Client, Rig


class Factory:
    def __init__(self):
        self.values = {'TEST:TR_LO.DMOV': 1, 'TEST:TR_LO-EnaAct': 1,
                       'TEST:TR_LO-PosAct': 0, 'TEST:TR_LO-Busy': 0}
        self.callback, self.target = None, None
        self.result, self.limit, self.axis_error = 1, False, 0
        self.calls = []
        self.finish_after, self.finish_actual, self.timers = None, None, []
        self.stop_callback, self.stop_error = None, 0
        self.queued_stops = []
    def __call__(self, name, **kwargs): return PV(name, self)
    def finish(self, actual=None):
        self.values.update({'TEST:TR_LO-PosAct': self.target if actual is None else actual,
                            'TEST:TR_LO-Busy': 0, 'TEST:TR_LO.DMOV': 1})
        self.callback(pvname='TEST:TR_LO.VAL')
        for callback in self.queued_stops:
            if callback: callback(pvname='TEST:TR_LO.STOP')
        self.queued_stops.clear()
    def finish_stop(self):
        self.values.update({'TEST:TR_LO-PosAct': .03, 'TEST:TR_LO-Busy': 0,
                            'TEST:TR_LO.DMOV': 1, 'TEST:TR_LO-VelAct': 0,
                            'TEST:TR_LO-ErrId': self.stop_error})
        if self.callback:
            self.callback(pvname='TEST:TR_LO.VAL')
        if self.stop_callback:
            self.stop_callback(pvname='TEST:TR_LO.STOP')


class PV:
    def __init__(self, name, factory):
        self.pvname, self.factory = name, factory
        self.connected = self.write_access = True
    def wait_for_connection(self, timeout): return True
    def get(self, **kwargs): return self.factory.values.get(self.pvname, 0)
    def put(self, value, **kwargs):
        f = self.factory
        f.calls.append((self.pvname, value, kwargs))
        if self.pvname.endswith('.STOP'):
            if f.result != 1: return f.result
            notify = kwargs.get('use_complete') or kwargs.get('callback') or kwargs['wait']
            if notify and f.values['TEST:TR_LO-Busy']:
                # EPICS dbProcessNotify queues another notify on an active
                # record. Such a STOP cannot interrupt the outstanding move.
                f.queued_stops.append(kwargs.get('callback'))
                return f.result
            f.stop_callback = kwargs.get('callback')
            if f.values['TEST:TR_LO-Busy']:
                timer = threading.Timer(.05, f.finish_stop)
                f.timers.append(timer)
                timer.start()
            elif f.stop_callback:
                f.stop_callback(pvname=self.pvname)
        elif self.pvname.endswith('.VAL'):
            assert kwargs['wait'] is False and kwargs['use_complete'] is True
            if f.result != 1: return f.result
            f.callback, f.target = kwargs['callback'], value
            f.values['TEST:TR_LO-ErrId'] = f.axis_error
            if f.limit:
                f.values['TEST:TR_LO.LVIO'] = 1
                f.callback(pvname=self.pvname)
            elif value == f.values['TEST:TR_LO-PosAct']:
                f.callback(pvname=self.pvname)
            else:
                f.values.update({'TEST:TR_LO-Busy': 1, 'TEST:TR_LO.DMOV': 0})
                if f.finish_after is not None:
                    timer = threading.Timer(f.finish_after, lambda: f.finish(actual=f.finish_actual))
                    f.timers.append(timer)
                    timer.start()
        else:
            assert kwargs['wait'] is True
            if self.pvname.endswith('-EnaCmd') and value == 0:
                assert f.values['TEST:TR_LO-Busy'] == 0, 'disabled before ramp-down completed'
            f.values[self.pvname] = value
        return f.result
    def disconnect(self): self.connected = False


class Plant:
    def check(self): pass


def setup():
    factory = Factory()
    client = Client(factory, timeout=.001)
    return factory, client, Rig({'axes': {'TR_LO': 5}, 'physical': {}}, 'TEST:', client, Plant())


factory, client, rig = setup()
request = rig.start_move('TR_LO', .2)
assert not request.is_set() and factory.values['TEST:TR_LO-Busy'] == 1
factory.finish()
assert request.is_set()
# The next request gets its own completion event; old callbacks cannot satisfy it.
old_callback = factory.callback
next_request = rig.start_move('TR_LO', -.2)
old_callback(pvname='TEST:TR_LO.VAL')
assert request.is_set() and not next_request.is_set()
factory.finish()
assert next_request.is_set()
client.put('TEST:TR_LO.VELO', .4)
assert factory.calls[-1][2]['wait'] is True

factory, client, rig = setup()
factory.finish_after = .05
try:
    rig.move('TR_LO', .2, timeout=1)  # Completion takes longer than the .001 s I/O timeout.
finally:
    for timer in factory.timers: timer.join()
assert rig.trace[-1]['action'] == 'move'

factory, client, rig = setup()
rig.move('TR_LO', 0, timeout=.1)  # No-op setup moves still wait for their own callback.

factory, client, rig = setup()
factory.values['TEST:TR_LO.SEVR'] = 2
try:
    rig.move('TR_LO', 0, timeout=.1)
except AssertionError as error:
    assert 'alarm' in str(error) and '".SEVR": 2.0' in str(error)
else:
    raise AssertionError('correct position must not conceal a motor alarm')

factory, client, rig = setup()
factory.result = -1
try:
    rig.start_move('TR_LO', .2)
except RuntimeError as error:
    assert 'result=-1' in str(error) and 'before cleanup' in str(error)
    assert '".MSTA"' in str(error) and '"-Blocked"' in str(error)
else:
    raise AssertionError('failed submission must not be accepted')

for reason in ('limit', 'axis_error'):
    factory, client, rig = setup()
    if reason == 'limit': factory.limit = True
    else: factory.axis_error = 0x1431c
    try:
        rig.start_move('TR_LO', .2)
    except RuntimeError as error:
        assert ('LVIO=1' if reason == 'limit' else '0x1431c') in str(error)
    else:
        raise AssertionError('rejected/failed motion must not be accepted')

factory, client, rig = setup()
factory.finish_after, factory.finish_actual = .01, .1
try:
    try:
        rig.move('TR_LO', .2, timeout=.08)
    except AssertionError as error:
        assert 'reaches 0.2' in str(error)
    else:
        raise AssertionError('put completion alone must not count as reaching the target')
finally:
    for timer in factory.timers: timer.join()
print('Slit motor requests: asynchronous submission, per-request completion, bounded arrival, rejection and pre-cleanup diagnostics passed.')

factory, client, rig = setup()
move_complete = rig.start_move('TR_LO', .7)
try:
    rig.stop_motion('TR_LO', timeout=1)
finally:
    for timer in factory.timers: timer.join()
assert rig.trace[-1]['action'] == 'motor STOP completed'
assert factory.values['TEST:TR_LO-PosAct'] < .7
assert move_complete.is_set() and not factory.queued_stops
stop_call = next(call for call in factory.calls if call[0].endswith('.STOP'))
assert stop_call[2]['wait'] is False and not stop_call[2].get('callback') and not stop_call[2].get('use_complete')

factory, client, rig = setup()
rig.start_move('TR_LO', .7)
rig.profile['physical']['TR_LO'] = {'drive_slave': 14}
factory.values['TEST:SIM:ec0:s14:velocitySetpoint01'] = 65534  # signed -2: tiny holding correction
try:
    assert rig.stop_all() == []
finally:
    for timer in factory.timers: timer.join()
assert factory.values['TEST:TR_LO-EnaCmd'] == 0

factory, client, rig = setup()
factory.stop_error = 0x14c08
rig.start_move('TR_LO', .7)
try:
    try:
        rig.stop_motion('TR_LO', timeout=1)
    except RuntimeError as error:
        assert '0x14c08' in str(error) and 'before cleanup' in str(error)
    else:
        raise AssertionError('STOP must not conceal an actual following error')
finally:
    for timer in factory.timers: timer.join()
print('STOP: ordinary put interrupts active motor put-notify, ramp-down precedes disable, following-error diagnostics retained.')

# A coordinated fault after acceptance must surface immediately, even when
# motor put completion never arrives. Capture the SM and slave interlocks
# before teardown can clear them; an arrival timeout conceals the real error.
factory, client, rig = setup()
factory.values.update({'TEST:SM00-Stat': 32768, 'TEST:SM00-StateCmd': 0,
                       'TEST:TR_LO-LastIlock': 3, 'TEST:TR_LO-SumIlockFwd': 1})
class FaultAfterAcceptance:
    def __init__(self): self.checks = 0
    def check(self):
        self.checks += 1
        if self.checks == 3:
            factory.values['TEST:TR_LO-ErrId'] = 0x1432E
rig.plant = FaultAfterAcceptance()
try:
    rig.move('TR_LO', .15, timeout=.1)
except AssertionError as error:
    assert '0x1432e' in str(error) and 'before cleanup' in str(error)
    assert '"state_machine"' in str(error) and '"-LastIlock": 3.0' in str(error)
    assert rig.trace[-1]['error'] == 0x1432E
else:
    raise AssertionError('post-acceptance axis fault must fail without waiting for arrival')
assert rig.plant.checks == 3
print('Coordinated faults: exact error and state-machine/interlock snapshot preserved before cleanup, without an arrival timeout.')
