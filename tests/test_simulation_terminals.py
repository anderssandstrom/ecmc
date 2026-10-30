#!/usr/bin/env python3
"""Scalar encoder layouts and dual-channel 7062 DS402 dispatch, using fake CA.

DOC: ecmccfg EL5042, EL5101 variants, EL5102 and EX7062 CSV/CSP mappings.
Manual writes preserve raw widths and do not invent motor/encoder coupling.
"""
from pathlib import Path
import queue
import re
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
import encoder
import gui
import ioc
import manual

PREFIX = 'TEST:SIM:'
TYPES = {'mbboDirect': 'mbbiDirect', 'longout': 'longin', 'int64out': 'int64in', 'ao': 'ai'}


class PV:
    def __init__(self, name, factory):
        self.pvname, self.factory = name, factory
        self.connected = name in factory.values
        self.read_access = self.write_access = True
    def wait_for_connection(self, timeout): return self.connected
    def get(self, **kwargs):
        assert not kwargs['use_monitor']
        if not self.connected: return None
        if re.search(r':driveControl\d\d$', self.pvname):
            status = self.factory.values[self.pvname.replace('driveControl', 'driveStatus')]
            return {0x40: 6, 0x21: 7, 0x23: 15, 0x27: 15}.get(status, 0)
        return self.factory.values[self.pvname]
    def put(self, value, **kwargs):
        assert kwargs['wait']
        self.factory.puts.append((self.pvname, value))
        self.factory.values[self.pvname] = value
        target = self.pvname[:-4] if self.pvname.endswith('-Set') else self.pvname + '-RB'
        self.factory.values[target] = value
        return 1
    def disconnect(self): self.connected = False


class Factory:
    def __init__(self): self.values, self.puts = {}, []
    def __call__(self, name, auto_monitor):
        assert not auto_monitor
        return PV(name, self)
    def field(self, name, record='mbboDirect', bits=32, value=0):
        self.values[name] = value
        self.values[name + '.RTYP'] = record
        if record in TYPES:
            self.values[name + '-RB'] = value
            self.values[name + '-RB.RTYP'] = TYPES[record]
            writer = name
        else:
            writer = name + '-Set'
            self.values[writer] = value
            self.values[writer + '.RTYP'] = next(key for key, val in TYPES.items() if val == record)
        self.values[writer + '.NOBT'] = bits


def rejects(function):
    try: function()
    except ValueError: return
    raise AssertionError('expected invalid value rejection')


for hardware in ('EL5042', 'EL5042_DC', 'EL5102', 'EL5101', 'EL5101_32bit',
                 'EL5101-0010', 'EL5101-0011'):
    assert ioc.model_module(hardware) is encoder
for hardware in ('EL50420', 'EL51010', 'EL5103'):
    assert ioc.model_module(hardware) is None

for hardware, bits in (('EL5042', 64), ('EL5102', 32), ('EL5101', 16),
                       ('EL5101_32bit', 32), ('EL5101-0010', 32)):
    slave, factory = ioc.Slave(0, 5, hardware), Factory()
    base = PREFIX + 'ec0:s5:'
    for channel in range(1, encoder.channel_count(hardware) + 1):
        suffix = f'{channel:02}'
        factory.field(base + 'encoderStatus' + suffix, bits=16)
        factory.field(base + 'positionActual' + suffix, 'int64out' if bits == 64 else 'mbboDirect', bits, 123)
        if hardware != 'EL5042':
            factory.field(base + 'encoderControl' + suffix, 'mbbiDirect', 8 if bits == 16 else 16)
            factory.field(base + 'encoderValue' + suffix, 'mbbiDirect', bits)
            factory.field(base + 'encoderLatchPostion' + suffix, bits=bits)
    io = encoder.Terminal(PREFIX, slave, .001, factory)
    io.connect()
    assert not factory.puts and not io.notice
    for channel in range(1, encoder.channel_count(hardware) + 1):
        position = base + f'positionActual{channel:02}'
        raw = (1 << bits) - 1
        io.write(position, raw)
        assert io.read()[position] == (-1 if bits == 64 else raw)
        rejects(lambda: io.write(position, 1 << bits))
        rejects(lambda: io.write(position, 1.5))
        if bits != 64:
            rejects(lambda: io.write(position, -1))
        if hardware != 'EL5042':
            control = base + f'encoderControl{channel:02}'
            io.write(control, 1)
            assert factory.puts[-1] == (control + '-Set', 1)
    io.close()

# Oversampling -0011 has no generated realtime scalar position; expose its
# scalar control/status/time fields and explicitly report the missing array model.
factory = Factory()
factory.field(PREFIX + 'ec0:s5:encoderStatus01', bits=16)
factory.field(PREFIX + 'ec0:s5:nextSyncTime', 'int64out', 64)
io = encoder.Terminal(PREFIX, ioc.Slave(0, 5, 'EL5101-0011'), .001, factory)
io.connect()
assert 'oversampling arrays are not modeled' in io.notice
io.close()

# Floating-point/manual fields use finite numeric values. Large integers must
# remain exact; U64 bit-pattern conversion must not pass through float.
factory = Factory()
factory.field('TEST:SIM:wide', 'int64out', 64)
factory.field('TEST:SIM:float', 'ao')
fields = manual.Fields(['TEST:SIM:wide', 'TEST:SIM:float'], .001, factory)
fields.connect()
fields.write('TEST:SIM:wide', (1 << 63) + 123)
assert factory.puts[-1][1] == -(1 << 63) + 123
fields.write('TEST:SIM:float', 1.25)
assert factory.puts[-1][1] == 1.25
rejects(lambda: fields.write('TEST:SIM:float', float('nan')))
fields.close()

# Configured 7062 channels get independent workers/rows and infer CSV/CSP from
# their native command fields. The terminal is a DS402 stepper, not EL7041 Q15.
factory = Factory()
base = PREFIX + 'ec0:s7:'
for channel, mode in (('01', 'csv'), ('02', 'csp')):
    factory.field(base + 'driveControl' + channel, 'mbbiDirect', 16)
    factory.field(base + ('velocitySetpoint' if mode == 'csv' else 'positionSetpoint') + channel,
                  'longin', value=268435456 if mode == 'csv' else 75)
    factory.field(base + 'driveStatus' + channel, bits=16)
    factory.field(base + 'positionActual' + channel)
factory.field(base + 'binaryInputs01', bits=16, value=3)
factory.field(base + 'positionActual01_2', value=456)
factory.field(base + 'touchProbeControl02', 'mbbiDirect', bits=16)
for hardware in ('EL7062', 'ED7062_CSP', 'EL7062_OPT', 'MO7062-0100_OPT'):
    assert ioc.model_module(hardware) is ioc.ds402
assert ioc.model_module('EL7062_CST_OPT') is None
assert ioc.model_module('EL7062_DMC') is None
slaves = ioc.expand_channels([ioc.Slave(0, 7, 'EL7062_OPT')], PREFIX, factory, .001)
assert [slave.path for slave in slaves] == ['ec0.s7.ch01', 'ec0.s7.ch02']
assert not factory.puts
selected = ioc.expand_channels([ioc.Slave(0, 7, 'EL7062_OPT')], PREFIX, factory, .001,
                               {'ec0.s7': {'channel': '02'}})
assert [slave.channel for slave in selected] == ['02']
mailbox = gui.Mailbox()
results = ioc.run_models(slaves, PREFIX, factory, threading.Thread, timeout=.001,
                         duration=.15, report=mailbox.report)
assert results == {'ec0.s7.ch01': 0, 'ec0.s7.ch02': 0}
assert factory.values[base + 'positionActual01'] > 0
assert factory.values[base + 'positionActual02'] == 75
assert factory.values[base + 'driveStatus01'] == factory.values[base + 'driveStatus02'] == 8
updates = mailbox.take()
assert updates['ec0.s7.ch02'][1][base + 'positionSetpoint02'][1] == base + 'positionSetpoint02'
assert updates['ec0.s7.ch01'][1][base + 'binaryInputs01'] == (3, base + 'binaryInputs01')
assert updates['ec0.s7.ch01'][1][base + 'positionActual01_2'] == (456, base + 'positionActual01_2')
assert updates['ec0.s7.ch02'][1][base + 'touchProbeControl02'][1] == base + 'touchProbeControl02'

# Manual drive feedback writes go through the same generated field record;
# every displayed native field is editable when the IOC grants write access.
commands = {slave.path: queue.Queue() for slave in slaves}
commands['ec0.s7.ch01'].put((base + 'positionActual01', 123456))
results = ioc.run_models(slaves, PREFIX, factory, threading.Thread, timeout=.001,
                         duration=.05, report=mailbox.report, commands=commands)
assert all(value == 0 for value in results.values())
assert (base + 'positionActual01', 123456) in factory.puts
print('Encoder scalar layouts/widths, exact U64 writes, oversampling notice, dual-channel 7062 CSV/CSP and manual drive writes passed.')
