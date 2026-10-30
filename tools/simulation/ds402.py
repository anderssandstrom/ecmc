"""Functional DS402 CSV/CSP model for ECMC's native simulation field records."""
from dataclasses import dataclass
import math

from el7041 import EpicsIO as BaseIO, run, word16


@dataclass(frozen=True)
class Settings:
    mode: str = 'csv'
    encoder_counts_per_rev: int = 1048576
    velocity_units_per_rev_per_second: float = 268435.456

    def __post_init__(self):
        if self.mode not in ('csv', 'csp'):
            raise ValueError('mode must be csv or csp')
        for value in (self.encoder_counts_per_rev, self.velocity_units_per_rev_per_second):
            if not math.isfinite(value) or value <= 0:
                raise ValueError('position/velocity scaling must be finite and positive')


@dataclass(frozen=True)
class Feedback:
    drive_status: int
    position: int
    velocity: int


def signed32(value):
    if value is None or not math.isfinite(value) or int(value) != value or not -(1 << 31) <= value < (1 << 32):
        raise ValueError('expected a native 32-bit integer, got ' + repr(value))
    value = int(value) & 0xffffffff
    return value - (1 << 32) if value & (1 << 31) else value


class Model:
    def __init__(self, settings=Settings(), initial_counts=0):
        if not math.isfinite(initial_counts):
            raise ValueError('initial position must be finite')
        self.settings = settings
        self.position_counts = float(initial_counts)
        self.state = 'disabled'
        self.previous_control = 0
        self.velocity = 0

    def trip(self):
        self.state = 'fault'
        self.velocity = 0

    def feedback(self):
        status = {'disabled': 0x40, 'ready': 0x21, 'switched': 0x23,
                  'enabled': 0x27, 'quickstop': 0x07, 'fault': 0x08}[self.state]
        return Feedback(status, math.floor(self.position_counts) % (1 << 32), self.velocity)

    def step(self, control, setpoint, seconds):
        control, setpoint = word16(control), signed32(setpoint)
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError('elapsed seconds must be finite and nonnegative')
        reset = control & 0x80 and not self.previous_control & 0x80
        self.previous_control = control
        state = self.state
        if state == 'fault':
            if reset:
                self.state = 'disabled'
        elif not control & 2:
            self.state = 'disabled'
        elif state in ('enabled', 'quickstop') and not control & 4:
            self.state = 'quickstop'
        elif state == 'disabled':
            if control & 0x87 == 6:
                self.state = 'ready'
        elif control & 0x87 == 6:
            self.state = 'ready'
        elif state == 'ready' and control & 0x8f == 7:
            self.state = 'switched'
        elif state in ('switched', 'enabled', 'quickstop'):
            if control & 0x8f == 15:
                self.state = 'enabled'
            elif control & 0x8f == 7:
                self.state = 'switched'
        self.velocity = 0
        if self.state == 'enabled':
            s = self.settings
            if s.mode == 'csv':
                self.velocity = setpoint
                self.position_counts += setpoint / s.velocity_units_per_rev_per_second * s.encoder_counts_per_rev * seconds
            elif seconds > 0:
                # Follow the nearest modulo-32 target, including counter rollover.
                delta = (setpoint - self.position_counts + (1 << 31)) % (1 << 32) - (1 << 31)
                raw_velocity = delta / seconds / s.encoder_counts_per_rev * s.velocity_units_per_rev_per_second
                self.velocity = max(-(1 << 31), min((1 << 31) - 1, round(raw_velocity)))
                self.position_counts += delta
        return self.feedback()


class EpicsIO(BaseIO):
    def __init__(self, prefix, master, slave, channel, timeout, pv_factory):
        self.base = f'{prefix}ec{master}:s{slave}:'
        self.timeout = timeout
        fields = {'control': 'driveControl', 'csv': 'velocitySetpoint',
                  'csp': 'positionSetpoint', 'status': 'driveStatus',
                  'position': 'positionActual', 'velocity_actual': 'velocityActual'}
        self.pvs = {key: pv_factory(self.base + name + channel, auto_monitor=False)
                    for key, name in fields.items()}
        self.pvs['initial'] = pv_factory(self.base + 'positionActual' + channel + '-RB', auto_monitor=False)
        self.types = {key: pv_factory(pv.pvname + '.RTYP', auto_monitor=False)
                      for key, pv in self.pvs.items()}
        self.mode = None
        self.velocity_unsigned = False

    def connect(self):
        modes = [mode for mode in ('csv', 'csp')
                 if self.pvs[mode].wait_for_connection(timeout=self.timeout)]
        if self.mode is None:
            if len(modes) != 1:
                raise RuntimeError('expected exactly one CSV/CSP command field; specify mode when both exist')
            self.mode = modes[0]
        if self.mode not in modes:
            raise RuntimeError('missing command field for ' + self.mode)
        self.active = {'control': ('mbbiDirect',), self.mode: ('longin',), 'status': ('mbboDirect',),
                       'position': ('mbboDirect',), 'initial': ('mbbiDirect',)}
        if self.pvs['velocity_actual'].wait_for_connection(timeout=self.timeout):
            # Legacy EcAddEntryComplete(...,32,...) declares a U32 word;
            # newer EcAddEntryDT(...,S32,...) declares a signed value.
            self.active['velocity_actual'] = ('longout', 'mbboDirect')
        for key, expected in self.active.items():
            pv, record = self.pvs[key], self.types[key]
            if not pv.wait_for_connection(timeout=self.timeout) or not record.wait_for_connection(timeout=self.timeout):
                raise RuntimeError('cannot connect to ' + pv.pvname)
            actual = record.get(as_string=True, use_monitor=False, timeout=self.timeout)
            if actual not in expected:
                raise RuntimeError(f'{pv.pvname}: expected {" or ".join(expected)}, got {actual!r}')
            if key == 'velocity_actual':
                self.velocity_unsigned = actual == 'mbboDirect'
            writing = key in ('status', 'position', 'velocity_actual')
            if not (pv.write_access if writing else pv.read_access):
                raise RuntimeError('missing access to ' + pv.pvname)

    def read(self):
        return tuple(self.pvs[key].get(use_monitor=False, timeout=self.timeout)
                     for key in ('control', self.mode))

    def publish(self, feedback):
        velocity = feedback.velocity & 0xffffffff if self.velocity_unsigned else feedback.velocity
        values = [('position', feedback.position), ('velocity_actual', velocity),
                  ('status', feedback.drive_status)]
        for key, value in values:
            if key not in self.active:
                continue
            pv = self.pvs[key]
            if not pv.connected or pv.put(value, wait=True, timeout=self.timeout) != 1:
                raise RuntimeError('feedback write failed: ' + pv.pvname)
