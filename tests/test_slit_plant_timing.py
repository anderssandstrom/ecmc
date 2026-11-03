#!/usr/bin/env python3
"""Production plant loop with controlled CA latency; retain watchdog and cause."""
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
sys.path.insert(0, str(Path(__file__).resolve().parent / 'integration'))
import slit.plant as module
from slit.cases.isolation import IsolationTests


class Clock:
    def __init__(self): self.now = 1.
    def monotonic(self): return self.now
    def advance(self, duration): self.now += duration


class Stop:
    def __init__(self, clock): self.clock, self.waits, self.stopped = clock, 0, False
    def is_set(self): return self.stopped
    def set(self): self.stopped = True
    def wait(self, duration):
        self.clock.advance(duration)
        self.waits += 1
        if self.waits >= 3: self.stopped = True
        return self.stopped


def run(stalled):
    clock, ios = Clock(), []
    class IO:
        def __init__(self, *args): self.published, self.withdrawn = [], []; ios.append(self)
        def connect(self): pass
        def initial_position(self): return 0
        def read(self): clock.advance(.005); return 1, 0
        def publish(self, feedback):
            self.published.append(feedback)
            clock.advance(.4 if stalled and len(self.published) == 1 else .001)
        def withdraw(self, feedback):
            # When the watchdog failed, retain its cause before alarm injection.
            if stalled: assert plant.error is not None and plant.failure_details is not None
            self.withdrawn.append(feedback)
        def close(self): pass
    class PV:
        connected = read_access = write_access = True
        def __init__(self, name, **kwargs): self.pvname = name
        def wait_for_connection(self, timeout): return True
        def get(self, **kwargs):
            if self.pvname.endswith('.RTYP'):
                return ('int64in' if '-RB.' in self.pvname else
                        'mbboDirect' if 'encoderStatus' in self.pvname else 'int64out')
            return 5652480
        def put(self, value, **kwargs): clock.advance(.001); return 1
        def disconnect(self): pass
    profile = {'physical': {'TR_HI': {'drive_slave': 12, 'encoder_slave': 9, 'encoder_channel': '01',
                    'internal_counts_per_mm': 12800, 'absolute_counts_per_mm': 4096,
                    'absolute_offset_mm': -1380, 'absolute_bits': 26}}}
    plant = module.Plant(profile, 'TEST:SIM:', PV, None)
    plant.stop = Stop(clock)
    old_io, old_clock = module.el7041.EpicsIO, module.time.monotonic
    try:
        module.el7041.EpicsIO, module.time.monotonic = IO, clock.monotonic
        plant.run()
        snapshot = plant.snapshot()
    finally:
        module.el7041.EpicsIO, module.time.monotonic = old_io, old_clock
    assert ios[0].withdrawn[-1].drive_status & 8
    if stalled:
        assert 'plant loop stalled' in str(plant.error)
        assert 'TR_HI drive feedback write (0.400s)' in str(plant.error)
        assert snapshot['failure']['step_seconds'] > .4
        assert snapshot['failure']['io_seconds']['TR_HI drive feedback write'] > .399
        try: plant.check()
        except RuntimeError as error: assert 'stalled' in str(error)
        else: raise AssertionError('watchdog must still fail; never raise its threshold to hide overload')
    else:
        assert plant.error is None and snapshot['failure'] is None
        assert len(ios[0].published) == 3
        assert snapshot['io_max_seconds']['TR_HI command read'] < .006
    return plant

healthy, failed = run(False), run(True)
for plant in (failed, healthy):
    case = IsolationTests('test_blocked_motor_commands_during_physical_move')
    case.rig = SimpleNamespace(plant=plant, names=('TR_HI',), profile={'physical': {}},
                               get=lambda axis, field: 0x1460e)
    try: case.fault_free()
    except RuntimeError as error:
        assert plant is failed and 'plant loop stalled' in str(error)
    except AssertionError:
        assert plant is healthy  # A real axis fault with a healthy model remains a failure.
    else: raise AssertionError('no failure cause was reported')

def check_context_isolation():
    # Model context ownership rather than timing: the command thread must keep
    # its context, and every plant PV must close before destroying the new one.
    local = module.threading.local()
    initial = object()
    local.context = initial
    events = []
    cache = {initial: {'command': SimpleNamespace(chid=1)}}
    handles = {1: initial}
    def create_context():
        assert not hasattr(local, 'context')
        local.context = object()
        cache[local.context] = {'plant': SimpleNamespace(chid=2)}
        handles[2] = local.context
        events.append(('create', local.context))
    def destroy_context():
        assert not cache[local.context], 'channels must be cleared before destroying context'
        assert local.context not in handles.values(), 'atexit must not see stale channel handles'
        cache.pop(local.context)
        events.append(('destroy', local.context))
        del local.context
    def clear_channel(chid):
        assert handles[chid] is local.context
        handles.pop(chid)
        cache[local.context].pop('plant')
        events.append(('clear channel', local.context))
    def target():
        assert local.context is not initial
        events.append(('disconnect PVs', local.context))
    fake_ca = SimpleNamespace(create_context=create_context, destroy_context=destroy_context,
                              current_context=lambda: local.context, _cache=cache,
                              clear_channel=clear_channel,
                              flush_io=lambda: events.append(('flush', local.context)))
    with patch.dict(sys.modules, {'epics': SimpleNamespace(ca=fake_ca)}):
        thread = module.PlantThread(target=target)
        thread.start()
        thread.join(timeout=2)
        assert not thread.is_alive()
        assert local.context is initial
        assert [name for name, _ in events] == ['create', 'disconnect PVs', 'clear channel', 'flush', 'destroy']
        assert all(context is events[0][1] for _, context in events)
        assert handles == {1: initial} and initial in cache
        # A target exception must also destroy its context, without destroying
        # the caller's context. Invoke run directly in an unattached fake thread.
        del local.context
        def fail(): raise RuntimeError('plant target failed')
        try:
            module.PlantThread(target=fail).run()
        except RuntimeError as error: assert str(error) == 'plant target failed'
        else: raise AssertionError('target exception was swallowed')
        assert not hasattr(local, 'context')
        assert [name for name, _ in events[-4:]] == ['create', 'clear channel', 'flush', 'destroy']
        assert handles == {1: initial}


check_context_isolation()
print('Plant timing: separate CA context lifecycle, unchanged stall watchdog, first-failure timing, and controller fault classification pass.')
