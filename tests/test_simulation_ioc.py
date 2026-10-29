#!/usr/bin/env python3
"""Prefix-only discovery and supported model dispatch with controlled CA PVs.

DOC: tools/simulation/README.md. Normal ecmccfg FirstSlave/HWType/NxtObjId
metadata are exposed as aliases under the simulation prefix. No real CA access.
"""
from pathlib import Path
import subprocess
import sys
import threading
import types

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/simulation'))
import ioc

PREFIX = 'TEST:SIM:'


class PV:
    def __init__(self,name,values,puts):
        self.pvname, self.values, self.puts = name, values, puts
        self.connected = name in values
        self.read_access = self.write_access = True
    def wait_for_connection(self,timeout):
        return self.connected
    def get(self,**kwargs):
        assert kwargs['use_monitor'] is False
        return self.values.get(self.pvname) if self.connected else None
    def put(self,value,**kwargs):
        assert kwargs['wait']
        self.puts.append((self.pvname,value))
        self.values[self.pvname] = value
        return 1
    def disconnect(self):
        self.connected = False


class Factory:
    def __init__(self):
        self.values = {PREFIX+'FirstSlave':0}
        self.puts = []
        self.lock = threading.Lock()
        devices = [(0,'EK1100',12),(12,'EL7031',13),(13,'EL7031-0030',14),
                   (14,'EL7041-0052',18),(18,'EL7047',20),(20,'EL7041',22),(22,'EL7047_CSP',-1)]
        for slave,hardware,next_slave in devices:
            base = PREFIX+f'ec0:s{slave}:'
            self.values[base+'HWType'] = hardware
            self.values[base+'NxtObjId'] = next_slave
            if ioc.model_module(hardware) is None:
                continue
            fields = [('driveControl01','mbbiDirect',1),('velocitySetpoint01','longin',16384),
                      ('driveStatus01','mbboDirect',0),('encoderStatus01','mbboDirect',0),
                      ('positionActual01','mbboDirect',0),('positionActual01-RB','mbbiDirect',0)]
            for field,record,value in fields:
                self.values[base+field] = value
                self.values[base+field+'.RTYP'] = record
    def __call__(self,name,auto_monitor):
        assert not auto_monitor
        with self.lock:
            return PV(name,self.values,self.puts)


def rejects(function):
    try:
        function()
    except RuntimeError:
        return
    raise AssertionError('expected discovery failure')


assert ioc.model_module('EL7031') is ioc.el7041
assert ioc.model_module('EL7031-0030') is ioc.el7041
assert ioc.model_module('EL7047') is ioc.el7041
for unsupported in ('EL70310', 'EL7031_CSP', 'EL7047_CSP', 'EL7047_PosIF'):
    assert ioc.model_module(unsupported) is None
factory = Factory()
slaves = ioc.discover(PREFIX,0,factory)
assert [(s.position,s.hardware) for s in slaves] == [(0,'EK1100'),(12,'EL7031'),(13,'EL7031-0030'),
                                                       (14,'EL7041-0052'),(18,'EL7047'),(20,'EL7041'),(22,'EL7047_CSP')]
assert not factory.puts
factory.values[PREFIX+'ec0:s20:NxtObjId'] = 12
rejects(lambda: ioc.discover(PREFIX,0,factory))
factory.values[PREFIX+'ec0:s20:NxtObjId'] = 22
factory.values[PREFIX+'FirstSlave'] = float('nan')
rejects(lambda: ioc.discover(PREFIX,0,factory))
factory.values[PREFIX+'FirstSlave'] = 65536
rejects(lambda: ioc.discover(PREFIX,0,factory))
factory.values[PREFIX+'FirstSlave'] = -1
assert ioc.discover(PREFIX,0,factory) == []
assert len(ioc.discover(PREFIX,0,factory,first_slave=14)) == 4
factory.values[PREFIX+'FirstSlave'] = 0
old = factory.values.pop(PREFIX+'ec0:s14:HWType')
rejects(lambda: ioc.discover(PREFIX,0,factory))
factory.values[PREFIX+'ec0:s14:HWType'] = old

# Use a separate deterministic clock in each worker; this test checks dispatch,
# not operating-system scheduling or CA throughput under CI load.
actual_run = ioc.el7041.run
def deterministic_run(io,model,period,duration=None,sleep=None,stop=None):
    class Clock:
        now = 0
        def time(self): return self.now
        def advance(self,seconds): self.now += seconds
    clock = Clock()
    return actual_run(io,model,period,duration,clock=clock.time,sleep=clock.advance,stop=stop)
ioc.el7041.run = deterministic_run
results = ioc.run_models(slaves,PREFIX,factory,threading.Thread,rate=1000,duration=.03,
                         settings={slave.path:{'enable_delay':0} for slave in slaves if ioc.model_module(slave.hardware)})
assert results == {f'ec0.s{n}':0 for n in (12,13,14,18,20)}
assert factory.values[PREFIX+'ec0:s12:positionActual01'] > 0
assert factory.values[PREFIX+'ec0:s13:positionActual01'] > 0
assert factory.values[PREFIX+'ec0:s18:positionActual01'] > 0
assert factory.values[PREFIX+'ec0:s14:positionActual01'] > 0
assert factory.values[PREFIX+'ec0:s20:positionActual01'] > 0
assert not factory.values[PREFIX+'ec0:s14:driveStatus01'] & 2
assert not factory.values[PREFIX+'ec0:s20:driveStatus01'] & 2
assert all('driveStatus' in name or 'encoderStatus' in name or 'positionActual' in name
           for name,_ in factory.puts)
assert not any('s0:' in name or 's22:' in name for name,_ in factory.puts)
# Failure to attach one supported model does not prevent another model running.
factory = Factory()
factory.values.pop(PREFIX+'ec0:s14:driveControl01')
results = ioc.run_models(slaves,PREFIX,factory,threading.Thread,rate=1000,duration=.02,
                         settings={slave.path:{'enable_delay':0} for slave in slaves if ioc.model_module(slave.hardware)})
assert results == {f'ec0.s{n}':int(n==14) for n in (12,13,14,18,20)}
assert factory.values[PREFIX+'ec0:s20:positionActual01'] > 0
assert not any('s14:' in name for name,_ in factory.puts)
# --list is read-only and requires just the SIM prefix; import CA lazily.
factory = Factory()
epics = types.ModuleType('epics'); epics.PV = factory
ca = types.ModuleType('epics.ca'); ca.CAThread = threading.Thread
sys.modules['epics'],sys.modules['epics.ca'] = epics,ca
assert ioc.main(['--prefix',PREFIX,'--list']) == 0
assert not factory.puts
ioc.el7041.run = actual_run
subprocess.run([sys.executable,str(ROOT/'tools/simulation/ioc.py'),'--help'],
               check=True,stdout=subprocess.DEVNULL)
print('IOC model discovery: sparse chains, malformed/missing metadata, support catalog, independent workers and read-only listing passed.')
