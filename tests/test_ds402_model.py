#!/usr/bin/env python3
"""DS402 command transitions and native-field CA adapter; no IOC/network.

Basis: ECMC ecmcDriveDS402.cpp and Beckhoff DS402 control/status word mapping.
Scaling expectations use explicit counts/rev and velocity units/rev/s, never
the implementation to construct the expected trajectory.
"""
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
import ds402
import ioc


def rejects(function):
    try:
        function()
    except (ValueError, RuntimeError):
        return
    raise AssertionError('expected rejection')


def enable(model):
    assert model.step(0, 0, 0).drive_status == 0x40
    assert model.step(6, 0, 0).drive_status == 0x21
    assert model.step(7, 0, 0).drive_status == 0x23
    assert model.step(15, 0, 0).drive_status == 0x27


model = ds402.Model(ds402.Settings(encoder_counts_per_rev=1000,
                                   velocity_units_per_rev_per_second=100))
assert model.step(15, 100, 1).drive_status == 0x40  # no skipped startup sequence
enable(model)
assert model.step(15, 100, .25).position == 250
assert model.step(15, -100, .5).position == (1 << 32) - 250
assert model.step(7, 100, 1).drive_status == 0x23  # disable operation
assert model.position_counts == -250
assert model.step(15, 0, 0).drive_status == 0x27
assert model.step(11, 100, 1).drive_status == 0x07  # quick stop, no motion
assert model.position_counts == -250
assert model.step(15, 0, 0).drive_status == 0x27
model.trip()
assert model.step(15, 100, 1).drive_status == 8
assert model.step(128, 0, 0).drive_status == 0x40
model.trip()
assert model.step(128, 0, 0).drive_status == 8  # reset requires a rising edge
model.step(0, 0, 0)
assert model.step(128, 0, 0).drive_status == 0x40
enable(model)
assert model.step(0, 100, 1).drive_status == 0x40

# Repository CSV example: 2^31 raw = 8000 rev/s, 2^20 counts/rev.
model = ds402.Model()
enable(model)
assert model.step(15, 268435456, .125).position == 131072000
model = ds402.Model(ds402.Settings(mode='csp'), (1 << 32) - 5)
enable(model)
assert model.step(15, 10, .01).position == 10
assert model.position_counts == (1 << 32) + 10  # nearest wrapped target
assert model.step(15, -10, .01).position == (1 << 32) - 10
assert model.step(7, 500, 1).position == (1 << 32) - 10
for bad in (None, float('nan'), 1.5, 1 << 32, -(1 << 31) - 1):
    rejects(lambda: model.step(15, bad, .01))
rejects(lambda: ds402.Settings(mode='cst'))
rejects(lambda: ds402.Settings(velocity_units_per_rev_per_second=0))
rejects(lambda: model.step(15, 0, -1))


class PV:
    def __init__(self, name, factory):
        self.pvname, self.factory = name, factory
        self.connected = name in factory.values
        self.read_access = self.write_access = True
    def wait_for_connection(self, timeout):
        return self.connected
    def get(self, **kwargs):
        assert not kwargs['use_monitor']
        if self.pvname.endswith('driveControl01'):
            # Simulate ECMC observing feedback before issuing the next command.
            status = self.factory.values[self.pvname.replace('driveControl', 'driveStatus')]
            return {0x40: 6, 0x21: 7, 0x23: 15, 0x27: 15}.get(status, 0)
        return self.factory.values.get(self.pvname)
    def put(self, value, **kwargs):
        assert kwargs['wait']
        self.factory.puts.append((self.pvname, value))
        self.factory.values[self.pvname] = value
        return 1
    def disconnect(self):
        self.connected = False


class Factory:
    def __init__(self, slave=3, mode='csv'):
        self.values, self.puts = {}, []
        self.base = f'TEST:SIM:ec0:s{slave}:'
        fields = [('driveControl01', 'mbbiDirect', 0),
                  ('velocitySetpoint01' if mode == 'csv' else 'positionSetpoint01', 'longin', 268435456 if mode == 'csv' else 50),
                  ('driveStatus01', 'mbboDirect', 0), ('positionActual01', 'mbboDirect', 0),
                  ('positionActual01-RB', 'mbbiDirect', 0), ('velocityActual01', 'longout', 0)]
        for name, record, value in fields:
            self.values[self.base + name] = value
            self.values[self.base + name + '.RTYP'] = record
    def __call__(self, name, auto_monitor):
        assert not auto_monitor
        return PV(name, self)


for mode in ('csv', 'csp'):
    factory = Factory(mode=mode)
    io = ds402.EpicsIO('TEST:SIM:', 0, 3, '01', .01, factory)
    io.connect()
    assert io.mode == mode and io.initial_position() == 0
    model = ds402.Model(ds402.Settings(mode=io.mode))
    io.publish(model.feedback())
    for _ in range(5):
        io.publish(model.step(*io.read(), .01))
    assert factory.values[factory.base + 'positionActual01'] > 0
    assert factory.values[factory.base + 'driveStatus01'] == 0x27
    assert all(name.endswith(('positionActual01', 'driveStatus01', 'velocityActual01')) for name, _ in factory.puts)
    model.trip()
    io.withdraw(model.feedback())
    assert factory.values[factory.base + 'driveStatus01'] == 8
    io.close()

factory = Factory()
factory.values[factory.base + 'positionSetpoint01'] = 0
factory.values[factory.base + 'positionSetpoint01.RTYP'] = 'longin'
io = ds402.EpicsIO('TEST:SIM:', 0, 3, '01', .01, factory)
rejects(io.connect)
assert not factory.puts
io.mode = 'csp'
io.connect()
io.close()
# EL7211-0010's legacy EcAddEntryComplete(...,32,velocityActual01)
# produces a U32 mbboDirect record. Signed native S32 produces longout.
for record in ('longout', 'mbboDirect'):
    factory = Factory()
    factory.values[factory.base + 'velocityActual01.RTYP'] = record
    io = ds402.EpicsIO('TEST:SIM:', 0, 3, '01', .01, factory)
    io.connect()
    for velocity in (100, 0, -100, -(1 << 31), (1 << 31) - 1):
        io.publish(ds402.Feedback(0x27, 0, velocity))
        expected = velocity if record == 'longout' else velocity % (1 << 32)
        assert factory.values[factory.base + 'velocityActual01'] == expected
    io.close()
for invalid in ('longin', 'mbbiDirect', 'ao'):
    factory = Factory()
    factory.values[factory.base + 'velocityActual01.RTYP'] = invalid
    io = ds402.EpicsIO('TEST:SIM:', 0, 3, '01', .01, factory)
    rejects(io.connect)
    assert not factory.puts
    io.close()
factory = Factory()
factory.values[factory.base + 'driveStatus01.RTYP'] = 'mbbiDirect'
io = ds402.EpicsIO('TEST:SIM:', 0, 3, '01', .01, factory)
rejects(io.connect)
assert not factory.puts
io.close()
for hardware in ('EL7211', 'EL7211-9014_CSP_STD', 'EP7211-0034', 'ELM7222-0010_OPT'):
    assert ioc.model_module(hardware) is ds402
for hardware in ('EL72110', 'EL7211_CST_OPT', 'EL7211_CSTCA', 'EL7047_CSP'):
    assert ioc.model_module(hardware) is None

# Exercise scanner dispatch, inferred CSP mode, optional field absence, and
# the shared loop's best-effort fault feedback on shutdown.
factory = Factory(mode='csp')
factory.values.pop(factory.base + 'velocityActual01')
actual_run = ds402.run
def deterministic_run(io, model, period, duration=None, sleep=None, stop=None):
    now = [0.0]
    def advance(seconds): now[0] += seconds
    return actual_run(io, model, period, duration, clock=lambda: now[0], sleep=advance, stop=stop)
ds402.run = deterministic_run
try:
    result = ioc.run_models([ioc.Slave(0, 3, 'EL7211_CSP_STD')], 'TEST:SIM:', factory,
                            threading.Thread, rate=100, duration=.1)
    assert result == {'ec0.s3': 0}
    assert factory.values[factory.base + 'positionActual01'] == 50
    assert factory.values[factory.base + 'driveStatus01'] == 8
    # Both command fields: worker must apply the explicit mode before connecting.
    factory = Factory()
    factory.values[factory.base + 'positionSetpoint01'] = 75
    factory.values[factory.base + 'positionSetpoint01.RTYP'] = 'longin'
    result = ioc.run_models([ioc.Slave(0, 3, 'EL7211_ALL')], 'TEST:SIM:', factory,
                            threading.Thread, rate=100, duration=.1,
                            settings={'ec0.s3': {'mode': 'csp'}})
    assert result == {'ec0.s3': 0}
    assert factory.values[factory.base + 'positionActual01'] == 75
    # A second malformed DS402 slave fails without stopping the working one.
    bad = Factory(slave=4)
    bad.values.pop(bad.base + 'driveControl01')
    factory = Factory(mode='csp')
    factory.values.update(bad.values)
    result = ioc.run_models([ioc.Slave(0, 3, 'EL7211_CSP'), ioc.Slave(0, 4, 'EP7211')],
                            'TEST:SIM:', factory, threading.Thread, rate=100, duration=.1)
    assert result == {'ec0.s3': 0, 'ec0.s4': 1}
    assert factory.values[factory.base + 'positionActual01'] == 50
    assert not any('s4:' in name for name, _ in factory.puts)
finally:
    ds402.run = actual_run
print('DS402: state transitions, scaling, CSV/CSP, rollover, native PV validation and scanner dispatch passed.')
