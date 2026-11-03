#!/usr/bin/env python3
"""Offline checks for the relay-coupled slit plant and live test discovery.

No IOC is accessed. Production controller timers are covered separately by
encoder-power/encoder_power_checks.cpp; live CA timing is tested by the group.
"""
from itertools import product
from pathlib import Path
import sys
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root/'tools/simulation'), str(root/'tests/integration')]
import slit.plant as module
from slit.run import make_suite

config = dict(drive_slave=12, encoder_slave=9, encoder_channel='01',
              internal_counts_per_mm=12800, absolute_counts_per_mm=4096,
              absolute_offset_mm=-1380, absolute_bits=26,
              encoder_power={'slave': 5, 'channel': '04'})
for high, off_raw in product((False, True), (0, 0xffffffff)):
    blade = module.Blade(config, .2)
    blade.encoder_power_polarity = high
    blade.encoder_power_delay = .05
    blade.encoder_power_off_raw = off_raw
    # An off supply overrides even frozen feedback, without moving mechanics.
    blade.inject(frozen=True)
    for _ in range(10):
        feedback, raw, ready = blade.step(0, 0, .01, encoder_power=not high)
        assert raw == off_raw and ready == 0 and blade.position_mm == .2
        assert feedback.position == 2560
    for _ in range(4):
        assert blade.step(0, 0, .01, encoder_power=high)[1:] == (off_raw, 0)
    feedback, raw, ready = blade.step(0, 0, .011, encoder_power=high)
    assert raw == round(1380.2*4096) and ready == 4
    blade.inject(ready=False)
    assert blade.step(0, 0, .01, encoder_power=high)[2] == 0
    blade.step(0, 0, .01, encoder_power=not high)
    assert blade.encoder_supply_seconds == 0
    assert blade.step(0, 0, .01, encoder_power=high)[1:] == (off_raw, 0)

blade = module.Blade(config, .2)
blade.inject(encoder_bias_mm=.004)
assert blade.step(0, 0, .01)[1] == round(1380.204*4096)
assert blade.position_mm == .2
for value in (float('nan'), float('inf'), True):
    try: blade.inject(encoder_bias_mm=value)
    except ValueError: pass
    else: raise AssertionError('invalid bias accepted')

# Execute the actual Plant.run I/O path with a deterministic clock and relay
# inputs. This verifies the EL5042 output PV receives zero, rather than only
# testing the Blade return value.
class Clock:
    now = 1.
    def monotonic(self): return self.now
    def advance(self, dt): self.now += dt

for high, off_raw in product((False, True), (0, 0xffffffff)):
    clock = Clock()
    writes = []
    class Stop:
        count = 0
        def is_set(self):
            if self.count == 0:
                plant.configure_encoder_power('TR_HI', high, .025, off_raw=off_raw)
            return self.count >= 5
        def set(self): self.count = 5
        def wait(self, seconds):
            clock.advance(max(.01, seconds)); self.count += 1
            return self.is_set() if self.count >= 5 else False
    class PV:
        connected = read_access = write_access = True
        def __init__(self, name, **_): self.pvname = name
        def wait_for_connection(self, **_): return True
        def get(self, **_):
            clock.advance(.001)
            if self.pvname.endswith('.RTYP'):
                return ('int64in' if '-RB.' in self.pvname else
                        'mbboDirect' if 'encoderStatus' in self.pvname else 'int64out')
            if self.pvname.endswith('binaryOutput04'):
                powered = plant.stop.count in (1, 2, 3)
                return int(high if powered else not high)
            return 5652480
        def put(self, value, **_):
            writes.append((plant.stop.count, self.pvname, value)); clock.advance(.001)
            return 1
        def disconnect(self): self.connected = False
    class IO:
        def __init__(self, *_): pass
        def connect(self): pass
        def initial_position(self): return 0
        def read(self): clock.advance(.001); return 0, 0
        def publish(self, feedback): pass
        def withdraw(self, feedback): pass
        def close(self): pass
    plant = module.Plant({'physical': {'TR_HI': config}}, 'TEST:SIM:', PV, None)
    plant.stop = Stop()
    with patch.object(module.el7041, 'EpicsIO', IO), patch.object(module.time, 'monotonic', clock.monotonic):
        plant.run()
    assert plant.error is None, plant.error
    raw = {cycle: value for cycle, name, value in writes if name.endswith('positionActual01')}
    status = {cycle: value for cycle, name, value in writes if name.endswith('encoderStatus01')}
    assert raw[0] == raw[1] == raw[4] == off_raw, raw
    assert raw[3] == 5652480 and status[3] == 4
    assert status[0] == status[4] == 0

cases = list(make_suite(None, ('open', 'closed'), ('encoder_power',)))
assert len(cases) == 22 and {c.loop for c in cases} == {'open', 'closed'}
assert all(c.rig is None for c in cases)
print('Slit encoder power: zero on EL5042 PV while off, warmup, polarities, physical counter isolation, and 22 live cases pass offline checks.')
