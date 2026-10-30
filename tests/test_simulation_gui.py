#!/usr/bin/env python3
"""Manual digital simulation and optional Tk smoke tests with fake CA endpoints.

DOC: tools/simulation/README.md. Channel layout follows ecmccfg EX1004/1008/
1016 and EX2004/2008 declarations; bit inputs retain their values until an
explicit request. No physical IOC/network is used.
"""
from pathlib import Path
import os
import queue
import select
import shutil
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
import digital
import gui
import ioc


PREFIX = 'TEST:SIM:'


class PV:
    def __init__(self, name, factory):
        self.pvname, self.factory = name, factory
        self.connected = name in factory.values
        self.read_access = self.write_access = True
    def wait_for_connection(self, timeout):
        return self.connected
    def get(self, **kwargs):
        assert not kwargs['use_monitor']
        self.factory.threads.add(threading.get_ident())
        return self.factory.values.get(self.pvname) if self.connected else None
    def put(self, value, **kwargs):
        assert kwargs['wait']
        self.factory.threads.add(threading.get_ident())
        self.factory.puts.append((self.pvname, value))
        if self.factory.fail_put:
            return -1
        self.factory.values[self.pvname] = value
        if self.pvname.endswith('-Set'):
            self.factory.values[self.pvname[:-4]] = value
        # Model the IOC accepting the injection into its memory buffer.
        self.factory.values[self.pvname + '-RB'] = value
        return 1
    def disconnect(self):
        self.connected = False


class Factory:
    def __init__(self):
        self.values, self.puts, self.threads = {}, [], set()
        self.fail_put = False
    def add(self, slave):
        direction, count = digital.LAYOUTS[slave.hardware]
        for channel in range(1, count + 1):
            pv = f'{PREFIX}ec{slave.master}:s{slave.position}:binary{direction.title()}{channel:02}'
            self.values[pv] = channel % 2
            self.values[pv + '.RTYP'] = 'mbboDirect' if direction == 'input' else 'mbbiDirect'
            if direction == 'input':
                self.values[pv + '-RB'] = channel % 2
            else:
                self.values[pv + '-Set'] = channel % 2
                self.values[pv + '-Set.RTYP'] = 'mbboDirect'
    def __call__(self, name, auto_monitor):
        assert not auto_monitor
        return PV(name, self)


def rejects(function):
    try:
        function()
    except (RuntimeError, ValueError):
        return
    raise AssertionError('expected rejection')


for hardware, (direction, count) in digital.LAYOUTS.items():
    factory = Factory()
    slave = ioc.Slave(0, 2, hardware)
    factory.add(slave)
    assert ioc.model_module(hardware) is digital
    io = digital.Terminal(PREFIX, slave, .01, factory)
    io.connect()
    assert len(io.read()) == count
    assert io.read()['01'] == 1
    assert not factory.puts  # Connecting must preserve existing simulated inputs.
    io.write('01', 0)
    assert io.read()['01'] == 0
    assert factory.puts[-1][0].endswith('01' if direction == 'input' else '01-Set')
    rejects(lambda: io.write('01', 2))
    rejects(lambda: io.write('99', 1))
    factory.fail_put = True
    rejects(lambda: io.write('01', 1))
    io.close()

factory = Factory()
input_slave, output_slave = ioc.Slave(0, 1, 'EL1008'), ioc.Slave(0, 2, 'EL2008')
factory.add(input_slave)
factory.add(output_slave)
io = digital.Terminal(PREFIX, input_slave, .01, factory)
factory.values[io.types['01'].pvname] = 'mbbiDirect'
rejects(io.connect)
assert not factory.puts
io.close()
# Older IOCs without output -Set companions still provide readback, without
# writing the input record's VAL field or pretending a write reached the buffer.
factory = Factory()
factory.add(output_slave)
for name in list(factory.values):
    if '-Set' in name:
        del factory.values[name]
io = digital.Terminal(PREFIX, output_slave, .01, factory)
io.connect()
assert io.read()['01'] == 1 and not io.writable
rejects(lambda: io.write('01', 0))
assert not factory.puts
io.close()
factory = Factory()
factory.add(input_slave)
factory.add(output_slave)
io = digital.Terminal(PREFIX, output_slave, .01, factory)
factory.values[io.write_types['01'].pvname] = 'mbbiDirect'
rejects(io.connect)
assert not factory.puts
io.close()
factory = Factory()
factory.add(input_slave)
factory.add(output_slave)
io = digital.Terminal(PREFIX, input_slave, .01, factory)
io.pvs['01'].write_access = False
rejects(io.connect)
assert not factory.puts
io.close()
io = digital.Terminal(PREFIX, output_slave, .01, factory)
io.readbacks['01'].read_access = False
rejects(io.connect)
io.close()
commands = {input_slave.path: queue.Queue(), output_slave.path: queue.Queue()}
commands[input_slave.path].put(('01', 0))
commands[output_slave.path].put(('01', 0))
mailbox = gui.Mailbox()
results = ioc.run_models([input_slave, output_slave], PREFIX, factory, threading.Thread,
                         duration=.05, report=mailbox.report, commands=commands)
assert results == {'ec0.s1': 0, 'ec0.s2': 0}
assert sorted(factory.puts) == [(PREFIX + 'ec0:s1:binaryInput01', 0),
                                (PREFIX + 'ec0:s2:binaryOutput01-Set', 0)]
updates = mailbox.take()
assert updates['ec0.s1'][0] == 'stopped'
assert updates['ec0.s1'][1][PREFIX + 'ec0:s1:binaryInput01'] == (0, '01')
assert updates['ec0.s2'][1][PREFIX + 'ec0:s2:binaryOutput01'] == (0, '01')
assert not mailbox.take()
for value in range(1000):
    mailbox.report('ec0.s1', 'running', {'value': (value, None)})
assert len(mailbox.pending) == 1
assert mailbox.take()['ec0.s1'][1]['value'][0] == 999

# Actual drive worker updates (same publication loop used by DS402).
factory = Factory()
base = PREFIX + 'ec0:s3:'
for field, record, value in [('driveControl01', 'mbbiDirect', 1),
                              ('velocitySetpoint01', 'longin', 16384),
                              ('driveStatus01', 'mbboDirect', 0),
                              ('encoderStatus01', 'mbboDirect', 0),
                              ('positionActual01', 'mbboDirect', 0),
                              ('positionActual01-RB', 'mbbiDirect', 0)]:
    factory.values[base + field] = value
    factory.values[base + field + '.RTYP'] = record
    if record == 'mbboDirect':
        factory.values[base + field + '.NOBT'] = 16
        factory.values[base + field + '-RB'] = value
        factory.values[base + field + '-RB.RTYP'] = 'mbbiDirect'
    if record in ('mbbiDirect', 'longin') and not field.endswith('-RB'):
        factory.values[base + field + '-Set'] = value
        factory.values[base + field + '-Set.RTYP'] = 'mbboDirect' if record == 'mbbiDirect' else 'longout'
        factory.values[base + field + '-Set.NOBT'] = 16
results = ioc.run_models([ioc.Slave(0, 3, 'EL7041')], PREFIX, factory, threading.Thread,
                         duration=.15, settings={'ec0.s3': {'enable_delay': 0}}, report=mailbox.report)
assert results == {'ec0.s3': 0}
fields = mailbox.take()['ec0.s3'][1]
assert fields[base + 'driveControl01'] == (1, base + 'driveControl01')
assert fields[base + 'positionActual01'][0] > 0


def test_window():
    import tkinter as tk
    root = tk.Tk()
    root.withdraw()
    try:
        mailbox = gui.Mailbox()
        stop = threading.Event()
        encoder_slave = ioc.Slave(0, 9, 'EL5042')
        commands = {slave.path: queue.Queue() for slave in (input_slave, output_slave, encoder_slave)}
        window = gui.Window(root, [input_slave, output_slave, encoder_slave], PREFIX, mailbox, commands, stop)
        assert 'EL1008' in window.tree.item('ec0.s1', 'text')
        assert 'EL2008' in window.tree.item('ec0.s2', 'text')
        assert window.tree.set('ec0.s1', 'role') == '8-channel digital input'
        assert window.tree.set('ec0.s2', 'role') == '8-channel digital output'
        assert window.tree.set('ec0.s9', 'role') == '2-channel encoder'
        inp, out = PREFIX + 'ec0:s1:binaryInput01', PREFIX + 'ec0:s2:binaryOutput01'
        mailbox.report('ec0.s1', 'manual inputs', {inp: (1, '01')})
        mailbox.report('ec0.s2', 'manual outputs', {out: (0, '01')})
        window.refresh()
        row = 'ec0.s1/' + inp
        window.tree.selection_set(row)
        window.select()
        window.zero.invoke()
        assert commands['ec0.s1'].get_nowait() == ('01', 0)
        window.tree.selection_set('ec0.s2/' + out)
        window.select()
        assert str(window.one['state']) == 'normal'
        window.one.invoke()
        assert commands['ec0.s2'].get_nowait() == ('01', 1)
        assert commands['ec0.s1'].empty()
        # Legacy IOC without -Set companions remains observable without edit controls.
        mailbox.report('ec0.s2', 'read-only (no writable -Set PVs)', {out: (0, None)})
        window.refresh()
        assert str(window.one['state']) == 'disabled'
        mailbox.report('ec0.s1', 'error: disconnected', {})
        window.refresh()
        assert row not in window.editable
        assert 'stale' in window.tree.item(row, 'tags')
        position = PREFIX + 'ec0:s9:positionActual01'
        mailbox.report('ec0.s9', 'manual encoder fields', {position: (0, position)})
        window.refresh()
        window.tree.selection_set('ec0.s9/' + position)
        window.select()
        window.value.insert(0, str((1 << 63) + 123))
        window.apply.invoke()
        assert commands['ec0.s9'].get_nowait() == (position, (1 << 63) + 123)
        window.value.delete(0, 'end')
        window.value.insert(0, 'not a number')
        window.apply.invoke()
        assert commands['ec0.s9'].empty()
    finally:
        root.update_idletasks()
        root.destroy()
    # Exercise the public GUI entry point: fake CA methods must stay off Tk's thread.
    original_tk = tk.Tk
    def timed_root():
        root = original_tk()
        root.withdraw()
        root.after(400, root.destroy)
        return root
    tk.Tk = timed_root
    try:
        factory = Factory()
        factory.add(input_slave)
        result = gui.run([input_slave], PREFIX, factory, threading.Thread, 100, .01,
                         .1, {}, ioc.run_models)
        assert result == 0 and not factory.puts
        assert factory.threads and threading.get_ident() not in factory.threads
    finally:
        tk.Tk = original_tk


display = None
original_display = os.environ.get('DISPLAY')
try:
    import tkinter
    display_ready = bool(original_display)
    if shutil.which('Xvfb'):
        display = subprocess.Popen(['Xvfb', '-displayfd', '1', '-nolisten', 'tcp'],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        if not select.select([display.stdout], [], [], 5)[0]:
            raise RuntimeError('Xvfb did not start')
        number = display.stdout.readline().strip()
        display_ready = number.isdigit()
        if display_ready:
            os.environ['DISPLAY'] = ':' + number
    if display_ready:
        test_window()
        print('Tk display smoke test passed.')
    else:
        print('SKIP Tk smoke test: no usable display (Xvfb sockets may be restricted); digital/controller tests passed.')
except ImportError:
    print('SKIP Tk smoke test: tkinter unavailable; digital/controller tests passed.')
finally:
    if display:
        display.terminate()
        display.wait(timeout=5)
    if original_display is None:
        os.environ.pop('DISPLAY', None)
    else:
        os.environ['DISPLAY'] = original_display
print('Digital I/O and GUI: layouts, direction validation, retained inputs, explicit writes, coalesced updates and worker isolation passed.')
