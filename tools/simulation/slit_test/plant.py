"""Coupled stepper/EL5042 plant with independent fault injection per blade."""
from dataclasses import dataclass
import math
import random
import threading
import time

import el7041


@dataclass
class Faults:
    ready: bool = True
    frozen: bool = False
    acknowledge_enable: bool = True
    forward_ok: bool = True
    backward_ok: bool = True
    drive_fault: bool = False
    encoder_noise_mm: float = 0.0  # Peak amplitude of uniform measurement noise.


class Blade:
    def __init__(self, config, position_mm=0):
        self.config = config
        self.model = el7041.Model(el7041.Settings(), position_mm * config['internal_counts_per_mm'])
        self.faults = Faults()
        self.held_mm = position_mm
        self.encoder_mm = position_mm
        self.encoder_noise_mm = 0.0
        self.random = random.Random(config.get("noise_seed", config["drive_slave"]))

    @property
    def position_mm(self):
        return self.model.position_counts / self.config['internal_counts_per_mm']

    def inject(self, **values):
        for name, value in values.items():
            if name not in Faults.__dataclass_fields__:
                raise ValueError('unknown fault ' + name)
            if name == 'encoder_noise_mm':
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                    raise ValueError('encoder_noise_mm must be a finite nonnegative amplitude')
                self.faults.encoder_noise_mm = float(value)
                continue
            if name == 'frozen' and value and not self.faults.frozen:
                self.held_mm = self.encoder_mm
            setattr(self.faults, name, bool(value))
        if values.get('drive_fault') is False:
            self.model.fault = False

    def step(self, control, velocity, seconds):
        if self.faults.drive_fault:
            self.model.trip()
            control = el7041.word16(control) & ~3  # Fault injection persists through reset/enable requests.
        feedback = self.model.step(control, velocity, seconds)
        status = feedback.drive_status
        if self.faults.drive_fault:
            status = (status | 8) & ~3
        if not self.faults.acknowledge_enable:
            status &= ~2
        if not self.faults.forward_ok:
            status &= ~(1 << 12)
        if not self.faults.backward_ok:
            status &= ~(1 << 11)
        if self.faults.frozen:
            mm = self.held_mm
        else:
            peak = self.faults.encoder_noise_mm
            mm = self.position_mm + (self.random.uniform(-peak, peak) if peak else 0.0)
        self.encoder_mm = mm
        self.encoder_noise_mm = mm - self.position_mm
        raw = round((mm - self.config['absolute_offset_mm']) * self.config['absolute_counts_per_mm'])
        raw %= 1 << self.config['absolute_bits']
        return (el7041.Feedback(status, feedback.encoder_status, feedback.position),
                raw, 4 if self.faults.ready else 0)


class Plant:
    def __init__(self, profile, prefix, pv_factory, thread_factory, timeout=1, rate=100):
        self.profile, self.prefix, self.factory = profile, prefix, pv_factory
        self.thread_factory, self.timeout, self.period = thread_factory, timeout, 1 / rate
        self.stop = threading.Event()
        self.started = threading.Event()
        self.lock = threading.Lock()
        self.error, self.thread = None, None
        self.blades = {}
        self.last_elapsed, self.max_elapsed, self.last_commands = 0.0, 0.0, {}
        self.last_published = None

    def snapshot(self):
        with self.lock:
            return {'last_step_seconds': self.last_elapsed, 'max_step_seconds': self.max_elapsed,
                    'seconds_since_publish': time.monotonic() - self.last_published if self.last_published else None,
                    'error': str(self.error) if self.error else None,
                    'blades': {axis: {'position_mm': blade.position_mm,
                                     'encoder_mm': blade.encoder_mm,
                                     'encoder_noise_peak_mm': blade.faults.encoder_noise_mm,
                                     'encoder_measurement_error_mm': blade.encoder_noise_mm,
                                     'encoder_frozen': blade.faults.frozen,
                                     'encoder_ready': blade.faults.ready,
                                     'drive_fault': blade.faults.drive_fault,
                                     'acknowledge_enable': blade.faults.acknowledge_enable,
                                     **self.last_commands.get(axis, {})}
                               for axis, blade in self.blades.items()}}

    def inject(self, axis, **values):
        with self.lock:
            self.blades[axis].inject(**values)

    def clear_faults(self):
        with self.lock:
            for blade in self.blades.values():
                blade.faults = Faults()
                blade.model.fault = False

    def check(self):
        if self.error is not None:
            raise RuntimeError('plant stopped: ' + str(self.error))

    def start(self):
        self.thread = self.thread_factory(target=self.run, name='slit-test-plant')
        self.thread.start()
        if not self.started.wait(self.timeout * 30):
            self.close()
            raise RuntimeError('plant initialization timed out')
        self.check()

    def run(self):
        ios, encoders, owned = {}, {}, []
        try:
            for axis, config in self.profile['physical'].items():
                io = el7041.EpicsIO(self.prefix, 0, config['drive_slave'], '01', self.timeout, self.factory)
                ios[axis] = io
                io.connect()
                base = f'{self.prefix}ec0:s{config["encoder_slave"]}:'
                channel = config['encoder_channel']
                fields = {}
                for key, field, record in (('position', 'positionActual', 'int64out'),
                                           ('status', 'encoderStatus', 'mbboDirect'),
                                           ('initial', 'positionActual', 'int64in')):
                    name = base + field + channel + ('-RB' if key == 'initial' else '')
                    pv = self.factory(name, auto_monitor=False)
                    kind = self.factory(name + '.RTYP', auto_monitor=False)
                    owned.extend((pv, kind))
                    if not pv.wait_for_connection(timeout=self.timeout) or not kind.wait_for_connection(timeout=self.timeout):
                        raise RuntimeError('cannot connect to ' + name)
                    if kind.get(as_string=True, use_monitor=False, timeout=self.timeout) != record:
                        raise RuntimeError(name + ': expected ' + record)
                    if not (pv.read_access if key == 'initial' else pv.write_access):
                        raise RuntimeError('missing access: ' + name)
                    fields[key] = pv
                raw = fields['initial'].get(use_monitor=False, timeout=self.timeout)
                if raw is None or not math.isfinite(raw):
                    raise RuntimeError('cannot read initial absolute encoder')
                mm = (int(raw) & ((1 << config['absolute_bits']) - 1)) / config['absolute_counts_per_mm'] + config['absolute_offset_mm']
                # Counter images must agree before moving: never silently reset the IOC.
                expected = round(mm * config['internal_counts_per_mm']) % 65536
                actual = io.initial_position()
                if abs((actual - expected + 32768) % 65536 - 32768) > 8:
                    raise RuntimeError(axis + ': internal/absolute encoder images disagree; restart dedicated test IOC')
                self.blades[axis] = Blade(config, mm)
                encoders[axis] = fields
            previous = time.monotonic()
            self.started.set()
            while not self.stop.is_set():
                commands = {axis: io.read() for axis, io in ios.items()}
                now = time.monotonic()
                elapsed = now - previous
                with self.lock:
                    self.last_elapsed = elapsed
                    self.max_elapsed = max(self.max_elapsed, elapsed)
                    self.last_commands = {axis: {'control': int(command[0]), 'velocity': int(command[1])}
                                          for axis, command in commands.items()}
                if elapsed > max(.25, self.period * 5):
                    raise RuntimeError(f'plant loop stalled for {elapsed:.3f}s')
                with self.lock:
                    feedback = {axis: self.blades[axis].step(*command, elapsed) for axis, command in commands.items()}
                for axis, (drive, position, status) in feedback.items():
                    for key, value in (('position', position), ('status', status)):
                        pv = encoders[axis][key]
                        if not pv.connected or pv.put(value, wait=True, timeout=self.timeout) != 1:
                            raise RuntimeError('encoder write failed: ' + pv.pvname)
                    ios[axis].publish(drive)
                with self.lock:
                    self.last_published = time.monotonic()
                previous = now
                self.stop.wait(max(0, self.period - (time.monotonic() - now)))
        except Exception as error:
            self.error = error
        finally:
            self.started.set()
            for axis, io in ios.items():
                blade = self.blades.get(axis)
                if blade:
                    blade.model.trip()
                    io.withdraw(blade.model.feedback())
                io.close()
            for pv in owned:
                pv.disconnect()

    def close(self):
        self.stop.set()
        if self.thread is not None:
            self.thread.join()
