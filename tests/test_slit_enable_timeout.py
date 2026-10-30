#!/usr/bin/env python3
"""Run live enable/brake test cases against a virtual 15-second drive timer.

The native drive timeout and axis auto-enable timer are separate. No IOC or
wall-clock delay: fake CA/plant boundaries advance a controlled clock while
production test-case methods and fault assertions run unchanged.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/simulation'))
from slit_test.cases.base import DRIVE_ENABLE_FAILURE_WAIT
from slit_test.cases.brake import BrakeTests
from slit_test.cases.enable import EnableTests
from slit_test.rig import Rig

header = (ROOT / 'devEcmcSup/motion/ecmcDriveBase.h').read_text()
DRIVE_SECONDS = float(re.search(r'#define ERROR_DRV_STATE_MACHINE_TIME_OUT_TIME\s+(\d+)', header)[1])
assert DRIVE_ENABLE_FAILURE_WAIT > DRIVE_SECONDS


class Client:
    timeout = 1
    write_access = True
    pvname = 'TEST:TR_LO-EnaCmd'
    def __init__(self, rig): self.rig = rig
    def pv(self, name): return self
    def put(self, *args, **kwargs):
        self.rig.requested, self.rig.seconds = True, 0
        return 1
    def get(self, name):
        return self.rig.drive_control('TR_LO') if 'driveControl' in name else 0


class Plant:
    def __init__(self, rig): self.rig = rig
    def inject(self, axis, **values):
        assert values == {'acknowledge_enable': False}
        assert not self.rig.requested and self.rig.drive_status(axis) & 2 == 0, \
            'injection on an enabled drive tests feedback loss, not missing acknowledgement'
        self.rig.injected = True
        self.rig.injections += 1
    def clear_faults(self): pass


class TimerRig(Rig):
    def __init__(self, error=0x14613, premature_brake=False, cap=None, off_ack_delay=1):
        super().__init__({'axes': {'TR_LO': 5}, 'physical': {
            'TR_LO': {'drive_slave': 14, 'brake': {'slave': 5, 'channel': '01'}}}},
            'TEST:', None, None)
        self.client = Client(self)
        self.plant = Plant(self)
        # Match Rig.prepare(): physical zero moves can leave the drive enabled.
        self.requested, self.seconds, self.brake_checks = True, 0, 0
        self.injected, self.injections = False, 0
        self.off_ack_delay, self.off_ack_at = off_ack_delay, None
        self.error, self.premature_brake, self.cap = error, premature_brake, cap
        self.observed_timeout = False
    def get(self, axis, suffix):
        if suffix == '-ErrId':
            return self.error if self.injected and self.requested and self.seconds >= DRIVE_SECONDS else 0
        if suffix == '-EnaCmd-RB': return int(self.requested)
        if suffix == '-EnaAct': return int(self.requested and not self.injected)
        raise AssertionError(suffix)
    def wait(self, predicate, description, timeout=15):
        if self.cap is not None: timeout = min(timeout, self.cap)
        deadline = self.seconds + timeout
        while self.seconds <= deadline:
            if predicate(): return
            self.seconds += .5
        raise AssertionError('timeout: ' + description)
    def brake(self, axis):
        self.brake_checks += 1
        if not self.injected: return int(self.requested)
        return int(self.premature_brake and self.requested and self.seconds >= 5)
    def drive_control(self, axis):
        return int(self.requested and self.seconds < DRIVE_SECONDS)
    def drive_status(self, axis):
        enabled = not self.injected and (self.requested or
                  (self.off_ack_at is not None and self.seconds < self.off_ack_at))
        return int(enabled) << 1
    def stop_all(self):
        self.requested = False
        self.off_ack_at = self.seconds + self.off_ack_delay
        return []
    def reset(self):
        self.observed_timeout = self.seconds >= DRIVE_SECONDS
        self.requested = False


def run(case_type, method, rig):
    case = case_type(method)
    case.rig = rig
    getattr(case, method)()


brake_method = 'test_failed_enable_never_releases_brake'
rig = TimerRig()
run(BrakeTests, brake_method, rig)
assert rig.observed_timeout and rig.brake_checks > DRIVE_SECONDS
rig = TimerRig()
run(EnableTests, 'test_missing_enable_acknowledgement', rig)
assert rig.seconds == DRIVE_SECONDS
assert rig.injections == 1

# Command OFF/readiness can be low before native feedback acknowledges OFF.
# Missing/stuck acknowledgement must fail setup before fault injection.
rig = TimerRig(off_ack_delay=6)
try:
    run(EnableTests, 'test_missing_enable_acknowledgement', rig)
except AssertionError as error:
    assert 'disabled command, feedback and brake' in str(error)
    assert rig.injections == 0
else:
    raise AssertionError('cannot inject a fresh-enable failure before disabled feedback')

# The previous eight-second budget must reproduce the reported timeout.
try:
    run(BrakeTests, brake_method, TimerRig(cap=8))
except AssertionError as error:
    assert 'timeout:' in str(error)
else:
    raise AssertionError('eight seconds cannot observe the native drive timeout')

# A longer wait must still reject brake release or a different fault code.
for rig in (TimerRig(premature_brake=True), TimerRig(error=0x14317)):
    try:
        run(BrakeTests, brake_method, rig)
    except AssertionError:
        pass
    else:
        raise AssertionError('unsafe brake release or unrelated error must not pass')
print('Enable timeout: native timer fits the wait; old budget fails; brake release and wrong fault remain failures.')
