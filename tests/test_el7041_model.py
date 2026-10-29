#!/usr/bin/env python3
"""EL7041 model physics and CA boundaries, without pyepics, an IOC or hardware.

DOC: tools/simulation/README.md. Scaling supplied by the user: signed 15-bit
magnitude, 2000 full steps/s, 200 steps/rev and 12800 encoder counts/rev.
Word mappings come from ecmccfg/ecmcEX70XX.cmd and ecmcEL7031-chX.template.
"""
import importlib.util
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT/'tools/simulation/el7041.py'
spec = importlib.util.spec_from_file_location('el7041_model', PATH)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
Settings, Model = module.Settings, module.EL7041


def rejects(function, kind=ValueError):
    try:
        function()
    except kind:
        return
    raise AssertionError('expected '+kind.__name__)


# Physical reference values are calculated independently from the model.
model = Model(Settings(enable_delay=0))
assert model.step(1,16384,1).position == 64000  # 1000 full steps * 64 microsteps.
assert model.position_counts == 64000
assert model.step(1,-32768,.5).position == 0   # -1000 full steps * 64.
assert model.position_counts == 0
assert model.step(1,32767,1).position == 62460
assert model.position_counts == 127996.09375  # Positive full scale is one raw LSB below 2000.
model = Model(Settings(enable_delay=0))
assert model.step(1,65535,.256).position == 65535  # Unsigned word 0xffff means velocity -1.
assert model.position_counts == -1
model = Model(Settings(enable_delay=0))
for _ in range(256):
    model.step(1,1,.001)
assert model.position_counts == 1 and model.feedback().position == 1

model = Model(Settings(enable_delay=.05))
assert not model.step(1,16384,.04).drive_status & 2
feedback = model.step(1,16384,.02)
assert feedback.drive_status & 2 and math.isclose(model.position_counts,640)
position = model.position_counts
assert not model.step(0,32767,1).drive_status & 2 and model.position_counts == position
assert not model.step(1,16384,.04).drive_status & 2  # A new enable waits again.
model.trip()
assert model.feedback().drive_status & 8 and not model.feedback().drive_status & 3
assert not model.step(1,16384,1).drive_status & 2
assert model.position_counts == position
assert model.step(2,0,0).drive_status & 1  # Reset clears fault, does not enable.
assert model.step(5,0,.1).drive_status & (1 << 6)  # Reduced torque is status B6.

model = Model(Settings(enable_delay=0,lower_limit_revs=-.1,upper_limit_revs=.1))
feedback = model.step(1,16384,1)
assert model.position_counts == 1280 and not feedback.drive_status & (1 << 12)
assert feedback.drive_status & (1 << 11)
feedback = model.step(1,-16384,1)
assert model.position_counts == -1280 and not feedback.drive_status & (1 << 11)
assert feedback.drive_status & (1 << 12)
feedback = model.step(1,16384,.02)
assert model.position_counts == 0 and feedback.drive_status & 0x1800 == 0x1800
for kwargs in ({'counter_bits':0}, {'enable_delay':-1}, {'full_steps_per_second':float('nan')},
               {'motor_steps_per_rev':0}, {'encoder_counts_per_rev':0},
               {'lower_limit_revs':2,'upper_limit_revs':1}):
    rejects(lambda: Settings(**kwargs))
rejects(lambda: Model(initial_counts=float('nan')))
rejects(lambda: Model(Settings(lower_limit_revs=1),initial_counts=0))
for value in (None, float('nan'), float('inf'), 1.5, 65536, -32769):
    rejects(lambda: Model().step(1,value,.01))
rejects(lambda: Model().step(1,0,-.1))


class PV:
    def __init__(self,name):
        self.pvname = name
        self.connected = True
        self.read_access = self.write_access = True
        self.value = 0
        self.puts = []
        self.fail = False
        if name.endswith('.RTYP'):
            self.value = ('mbbiDirect' if 'driveControl' in name or '-RB.' in name
                          else 'longin' if 'velocitySetpoint' in name else 'mbboDirect')
    def wait_for_connection(self,timeout):
        return self.connected
    def get(self,**kwargs):
        assert kwargs['use_monitor'] is False
        return self.value if self.connected else None
    def put(self,value,**kwargs):
        assert kwargs['wait'] is True
        self.puts.append(value)
        if self.fail:
            return -1
        self.value = value
        return 1
    def disconnect(self):
        self.connected = False


pvs = {}
def factory(name,auto_monitor):
    assert not auto_monitor
    pvs[name] = PV(name)
    return pvs[name]


io = module.EpicsIO('TEST:SIM:',0,14,'01',.1,factory)
io.connect()
io.pvs['initial'].value = 1234
assert io.initial_position() == 1234
assert io.pvs['control'].pvname == 'TEST:SIM:ec0:s14:driveControl01'
assert io.pvs['position'].pvname == 'TEST:SIM:ec0:s14:positionActual01'
io.types['position'].value = 'ai'
rejects(io.connect,RuntimeError)
assert not any(p.puts for p in pvs.values())
io.types['position'].value = 'mbboDirect'
io.pvs['status'].write_access = False
rejects(io.connect,RuntimeError)
io.pvs['status'].write_access = True
io.pvs['control'].value, io.pvs['velocity'].value = 1, 16384


class Clock:
    now = 0
    def time(self):
        return self.now
    def sleep(self,seconds):
        self.now += seconds


clock = Clock()
model = Model(Settings(enable_delay=0))
module.run(io,model,.01,duration=.05,clock=clock.time,sleep=clock.sleep)
assert math.isclose(model.position_counts,3200)
assert io.pvs['status'].puts[-1] & 8 and not io.pvs['status'].puts[-1] & 2
assert any(word & 2 for word in io.pvs['status'].puts[:-1])
assert not io.pvs['control'].puts and not io.pvs['velocity'].puts  # Never write MCU commands.
assert set(p.pvname for p in pvs.values() if p.puts) == {
    'TEST:SIM:ec0:s14:driveStatus01', 'TEST:SIM:ec0:s14:encoderStatus01',
    'TEST:SIM:ec0:s14:positionActual01'}
# A failed feedback put or lost command read withdraws enabled feedback.
io.pvs['position'].fail = True
rejects(lambda: module.run(io,Model(),.01,duration=.01),RuntimeError)
assert not io.pvs['status'].puts[-1] & 2
io.pvs['position'].fail = False
io.pvs['control'].connected = False
clock = Clock()
rejects(lambda: module.run(io,Model(),.01,duration=.02,clock=clock.time,sleep=clock.sleep))
assert io.pvs['status'].puts[-1] & 8
io.pvs['control'].connected = True
# A large scheduling pause must not integrate a stale command into a position jump.
clock = Clock()
def stalled_sleep(_seconds):
    clock.now += 1
model = Model(Settings(enable_delay=0))
rejects(lambda: module.run(io,model,.01,duration=.02,clock=clock.time,sleep=stalled_sleep),RuntimeError)
assert model.position_counts == 0
io.close()
assert all(not pv.connected for pv in pvs.values())
subprocess.run([sys.executable,str(PATH),'--help'],stdout=subprocess.DEVNULL,check=True)
result = subprocess.run([sys.executable,str(PATH),'--prefix','TEST:SIM:','--slave','14','--rate','0'],
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert result.returncode == 2 and 'rate, timeout' in result.stderr
print('EL7041 model: physical scaling, enable/reset, signed velocity, rollover, limits, PV directions and failure cleanup passed.')
