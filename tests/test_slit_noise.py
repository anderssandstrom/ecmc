#!/usr/bin/env python3
"""USER: bounded EL5042 noise must affect measurement, not mechanics.

Exercise live noise verdicts with negative controls. Production monitor timing
and target/lag bands are covered separately by test_monitor.py.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/simulation'))
from slit_test.plant import Blade, Plant
from slit_test.cases.noise import NoiseTests

config = dict(drive_slave=12, internal_counts_per_mm=12800,
              absolute_counts_per_mm=4096, absolute_offset_mm=-1380, absolute_bits=26)
a, b = Blade(config), Blade(config)
a.inject(encoder_noise_mm=.014)
b.inject(encoder_noise_mm=.014)
samples = []
for _ in range(500):
    drive, raw, ready = a.step(0, 0, .01)
    assert (drive, raw, ready) == b.step(0, 0, .01)
    measured = raw / 4096 - 1380
    assert abs(measured) <= .014 + .5 / 4096
    assert a.position_mm == 0 and drive.position == 0 and ready == 4
    samples.append(measured)
assert min(samples) < -.01 and max(samples) > .01
a.inject(frozen=True)
held = a.step(0, 0, .01)[1]
a.step(1, 0, .05)
assert a.step(1, 16384, .1)[1] == held and a.position_mm > 0
a.inject(frozen=False, encoder_noise_mm=0)
_, raw, _ = a.step(0, 0, .01)
assert raw == round((a.position_mm + 1380) * 4096)
for invalid in (-1, float('nan'), float('inf'), '0.01', True):
    try:
        a.inject(encoder_noise_mm=invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid noise amplitude accepted: ' + repr(invalid))
plant = Plant({}, '', None, None)
plant.blades['TR_HI'] = a
a.inject(encoder_noise_mm=.01)
assert plant.snapshot()['blades']['TR_HI']['encoder_noise_peak_mm'] == .01
plant.clear_faults()
assert a.faults.encoder_noise_mm == 0


class Rig:
    """Controlled readback responses; no CA or sleeps, with deliberate failures."""
    def __init__(self, defect=None):
        self.plant = self
        self.peak = 0
        self.injected = False
        self.defect = defect
    def move(self, axis, target): pass
    def inject(self, axis, encoder_noise_mm):
        self.peak = encoder_noise_mm
        self.injected = True
    def get(self, axis, suffix):
        if suffix == '-ErrId': return 0x14c08 if self.peak and self.defect == 'fault' else 0
        if suffix == '-EnaAct': return 0 if self.peak and self.defect == 'disabled' else 1
        if suffix == '-PosSet': return .2
        if suffix == '-PosAct': return .2 + (self.peak if self.defect != 'no_disturbance' else 0)
        assert suffix == '-AtTarget'
        if self.defect == 'never_lost': return 1
        if self.defect == 'never_recovers' and self.injected: return 0
        if self.defect == 'small_loses' and self.peak: return 0
        return int(self.peak < .01)
    def wait(self, predicate, description, timeout=15):
        assert predicate(), description
    def assert_stays(self, predicate, description, seconds=1):
        for _ in range(10): assert predicate(), description


for method, defects in (
        ('test_small_encoder_noise_preserves_at_target', ('small_loses', 'fault', 'disabled')),
        ('test_encoder_noise_loses_at_target_then_recovers', ('never_lost', 'never_recovers', 'no_disturbance', 'fault', 'disabled'))):
    for defect in (None, *defects):
        case = NoiseTests(method)
        case.loop, case.rig = 'closed', Rig(defect)
        try:
            getattr(case, method)()
        except AssertionError:
            assert defect is not None
        else:
            assert defect is None, 'noise verdict missed ' + defect
        assert case.rig.peak == 0, 'noise must be removed even after a failed verdict'
    case = NoiseTests(method)
    case.loop = 'open'
    try:
        getattr(case, method)()
    except unittest.SkipTest:
        pass
    else:
        raise AssertionError('noise case ran without primary EL5042 feedback')
print('Slit noise: bounded measurement, freeze, reset and strict live verdicts passed.')
