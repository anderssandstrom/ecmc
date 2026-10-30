"""Checked PV access, bounded waits and reusable motion assertions."""
import json
import math
import threading
import time


class Client:
    def __init__(self, factory, timeout=1):
        self.factory, self.timeout, self.pvs = factory, timeout, {}

    def pv(self, name):
        if name not in self.pvs:
            pv = self.factory(name, auto_monitor=False)
            self.pvs[name] = pv
            if not pv.wait_for_connection(timeout=self.timeout):
                raise RuntimeError('cannot connect: ' + name)
        return self.pvs[name]

    def get(self, name):
        value = self.pv(name).get(use_monitor=False, timeout=self.timeout)
        if value is None or not math.isfinite(value):
            raise RuntimeError('cannot read numeric PV: ' + name)
        return value

    def put(self, name, value, *, wait=True, notify=True):
        pv = self.pv(name)
        if not pv.connected or not pv.write_access:
            raise RuntimeError(f'write refused: {name}, connected={pv.connected}, write_access={pv.write_access}')
        complete = threading.Event() if not wait and notify else None
        extra = {'use_complete': True, 'callback': lambda **_kwargs: complete.set()} if complete is not None else {}
        status = pv.put(value, wait=wait, timeout=self.timeout, **extra)
        if status != 1:
            raise RuntimeError(f'write failed: {name}, value={value!r}, result={status!r}, '
                               f'wait={wait}, timeout={self.timeout}s')
        return complete

    def close(self):
        for pv in self.pvs.values():
            pv.disconnect()


class Rig:
    def __init__(self, profile, prefix, client, plant):
        self.profile, self.prefix, self.client, self.plant = profile, prefix, client, plant
        self.names = list(profile['axes'])
        self.trace = []

    def name(self, axis, suffix): return self.prefix + axis + suffix
    def get(self, axis, suffix): return self.client.get(self.name(axis, suffix))
    def put(self, axis, suffix, value):
        # STOP must be an ordinary put: a second put-notify on the same motor
        # would queue behind the outstanding VAL put-notify until motion ends.
        if suffix == '.STOP':
            return self.client.put(self.name(axis, suffix), value, wait=False, notify=False)
        return self.client.put(self.name(axis, suffix), value, wait=suffix != '.VAL')

    def brake(self, axis):
        brake = self.profile['physical'][axis]['brake']
        return self.client.get(self.prefix + f'SIM:ec0:s{brake["slave"]}:binaryOutput{brake["channel"]}')

    def drive_control(self, axis):
        slave = self.profile['physical'][axis]['drive_slave']
        return int(self.client.get(self.prefix + f'SIM:ec0:s{slave}:driveControl01'))

    def drive_status(self, axis):
        slave = self.profile['physical'][axis]['drive_slave']
        return int(self.client.get(self.prefix + f'SIM:ec0:s{slave}:driveStatus01'))

    def guard(self):
        if self.client.get(self.prefix + 'MCU-Cfg-Sim-Mode') != 1:
            raise RuntimeError('refusing test writes: IOC does not report SIM=1')
        expected = self.profile.get('schema', 1)
        actual = self.client.get(self.prefix + 'SIM:SlitTest:Schema')
        if actual != expected:
            raise RuntimeError(f'refusing test writes: slit configuration schema {actual:g}, expected {expected}; '
                               'restart the dedicated IOC with the matching updated slit_matrix_sim_test configuration')
        for axis, number in self.profile['axes'].items():
            if self.get(axis, '-Id') != number:
                raise RuntimeError('axis identity mismatch: ' + axis)
            if self.get(axis, '.DIR') != 0 or self.get(axis, '.OFF') != 0:
                raise RuntimeError(axis + ': tests require motor DIR=0 and OFF=0')

    def wait(self, predicate, description, timeout=15):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.plant.check()
            if predicate(): return
            time.sleep(.02)
        raise AssertionError('timeout: ' + description + '\n' + json.dumps(self.snapshot(), indent=2))

    def snapshot(self):
        result = {}
        for axis in self.names:
            result[axis] = {}
            for suffix in ('-PosAct', '-PosSet', '-EnaAct', '-EnaCmd', '-EnaCmd-RB', '-ExeCmd-RB',
                           '-Busy', '-ErrId', '-LimFwd', '-LimBwd', '-TrjSrcTyp-RB',
                           '-LastIlock', '-SumIlockFwd', '-SumIlockBwd',
                           '-WrnId', '-SftLimFwdIlock', '-SftLimBwdIlock', '-SftLimFwdEna-RB', '-SftLimBwdEna-RB',
                           '-TgtPosCmd', '-MtnCmd', '-MtnCmdData', '.LLM', '.HLM', '.VAL', '.RBV', '.DVAL',
                           '-Blocked', '-InStartup', '-StartupHeld', '-StartupMotorReady', '-StartupLimitsReady',
                           '.DMOV', '.MOVN', '.MSTA', '.LVIO', '.STAT', '.SEVR'):
                try: result[axis][suffix] = float(self.get(axis, suffix))
                except Exception as error: result[axis][suffix] = str(error)
            if 'brake' in self.profile['physical'].get(axis, {}):
                try: result[axis]['brake_output'] = int(self.brake(axis))
                except Exception as error: result[axis]['brake_output'] = str(error)
            if axis in self.profile['physical']:
                slave = self.profile['physical'][axis]['drive_slave']
                for field in ('driveControl01', 'driveStatus01', 'velocitySetpoint01'):
                    try: result[axis][field] = int(self.client.get(self.prefix + f'SIM:ec0:s{slave}:' + field))
                    except Exception as error: result[axis][field] = str(error)
        result['state_machine'] = {}
        for suffix in ('-Stat', '-StateCmd'):
            try: result['state_machine'][suffix] = int(self.client.get(self.prefix + 'SM00' + suffix))
            except Exception as error: result['state_machine'][suffix] = str(error)
        if hasattr(self.plant, 'snapshot'):
            result['plant'] = self.plant.snapshot()
        return result

    def stop_all(self):
        errors = []
        for axis in self.names:
            for suffix in ('.STOP', '-StpCmd'):
                try: self.put(axis, suffix, 1)
                except Exception as error: errors.append(str(error))
        # Keep the plant and drive enable alive during deceleration. Disabling
        # immediately after requesting STOP can itself introduce following error.
        try:
            self.wait(lambda: all(self.get(axis, '-Busy') == 0 for axis in self.names)
                      and all(self._drive_stationary(axis) for axis in self.profile['physical']),
                      'cleanup ramp-down before disabling drives', 5)
        except Exception as error:
            errors.append(str(error))
        finally:
            # Also attempt disable if the plant/IOC failed; preserve the failure.
            for axis in self.names:
                try: self.put(axis, '-EnaCmd', 0)
                except Exception as error: errors.append(str(error))
        return errors

    def _drive_stationary(self, axis):
        slave = self.profile['physical'][axis]['drive_slave']
        raw = int(self.client.get(self.prefix + f'SIM:ec0:s{slave}:velocitySetpoint01'))
        if raw >= 32768:
            raw -= 65536
        # Dedicated profile: Q15 = 10 mm/s. Allow <0.01 mm/s holding correction
        # from quantized closed-loop feedback, after trajectory Busy is clear.
        return abs(raw) <= 32

    def stop_motion(self, axis, timeout=5):
        try:
            self.put(axis, '.STOP', 1)
            self.trace.append({'action': 'motor STOP requested', 'axis': axis, 'snapshot': self.snapshot()})
            def stopped():
                error = int(self.get(axis, '-ErrId'))
                if error:
                    raise RuntimeError(f'{axis}: error during STOP: 0x{error:x}')
                return (self.get(axis, '.DMOV') == 1
                        and self.get(axis, '-Busy') == 0 and abs(self.get(axis, '-VelAct')) < .01)
            self.wait(stopped, axis + ' decelerates and completes motor STOP', timeout)
            if self.get(axis, '.SEVR'):
                raise RuntimeError(axis + ': motor alarm after STOP')
            self.trace.append({'action': 'motor STOP completed', 'axis': axis, 'snapshot': self.snapshot()})
        except Exception as error:
            snapshot = self.snapshot()
            self.trace.append({'action': 'motor STOP failed', 'axis': axis, 'error': str(error), 'snapshot': snapshot})
            raise RuntimeError(str(error) + '\nSTOP diagnostics (before cleanup):\n'
                               + json.dumps(snapshot, indent=2)) from error

    def reset(self):
        for axis in self.names:
            self.put(axis, '-RstCmd', 1)
        self.client.put(self.prefix + 'SM00-StateCmd', 3)
        self.wait(lambda: all(self.get(axis, '-ErrId') == 0 for axis in self.names), 'axis fault reset', 5)

    def prepare(self, mode):
        self.plant.clear_faults()
        errors = self.stop_all()
        if errors:
            raise RuntimeError('preparation stop failed: ' + '; '.join(errors))
        self.wait(lambda: all(self.get(axis, '-Busy') == 0 for axis in self.names), 'stopped axes', 5)
        self.client.put(self.prefix + 'SM00-EnaCmd', 1)
        self.reset()
        for axis in self.profile['physical']:
            self.put(axis, '-SetEncIdCmd-RB', 1 if mode == 'open' else 2)
            self.put(axis, '-Ctrl-Kp', 0 if mode == 'open' else 2)
            self.put(axis, '-Ctrl-Ki', 0)
            self.put(axis, '-Ctrl-Kd', 0)
            self.put(axis, '-Ctrl-Kff', 1)
        for axis in self.names:
            self.put(axis, '.VELO', .4)
            self.put(axis, '.ACCL', .4)
        self.reset()
        for axis in self.profile['physical']:
            self.move(axis, 0)
        self.kinematics()

    def start_move(self, axis, target):
        try:
            self.wait(lambda: self.get(axis, '.DMOV') == 1 and self.get(axis, '-Busy') == 0,
                      axis + ' idle before next motion request', 5)
            if not self.get(axis, '-EnaAct'):
                self.put(axis, '-EnaCmd', 1)
                self.wait(lambda: self.get(axis, '-EnaAct') == 1, axis + ' enabled', 6)
            complete = self.put(axis, '.VAL', target)
            def accepted():
                error = int(self.get(axis, '-ErrId'))
                if error:
                    raise RuntimeError(f'{axis}: motion request raised axis error 0x{error:x}')
                if complete.is_set() and self.get(axis, '.LVIO'):
                    raise RuntimeError(axis + ': motor rejected target at a limit (LVIO=1)')
                return (self.get(axis, '-Busy') == 1 or self.get(axis, '.DMOV') == 0
                        or (complete.is_set() and abs(self.get(axis, '-PosAct') - target) < .015))
            self.wait(accepted, axis + ' accepts motion request', 5)
            return complete
        except Exception as error:
            snapshot = self.snapshot()
            self.trace.append({'action': 'motion request failed', 'axis': axis, 'target': target,
                               'error': str(error), 'snapshot': snapshot})
            raise RuntimeError(str(error) + '\nMotion-request diagnostics (before cleanup):\n'
                               + json.dumps(snapshot, indent=2)) from error

    def move(self, axis, target, timeout=15):
        complete = self.start_move(axis, target)
        def arrived():
            error = int(self.get(axis, '-ErrId'))
            if error:
                snapshot = self.snapshot()
                self.trace.append({'action': 'move failed', 'axis': axis, 'target': target,
                                   'error': error, 'snapshot': snapshot})
                raise AssertionError(f'{axis}: axis fault during motion 0x{error:x}\n'
                                     'Motion diagnostics (before cleanup):\n' + json.dumps(snapshot, indent=2))
            return (complete.is_set() and self.get(axis, '.DMOV') == 1 and self.get(axis, '-Busy') == 0
                    and abs(self.get(axis, '-PosAct') - target) < .015)
        self.wait(arrived, f'{axis} reaches {target}', timeout)
        error = self.get(axis, '-ErrId')
        if error: raise AssertionError(f'{axis}: unexpected error 0x{int(error):x}')
        if self.get(axis, '.LVIO') or self.get(axis, '.SEVR'):
            raise AssertionError(axis + ': motor completed with a limit violation/alarm\n'
                                 + json.dumps(self.snapshot(), indent=2))
        self.trace.append({'action': 'move', 'axis': axis, 'target': target, 'snapshot': self.snapshot()})

    def kinematics(self):
        lo, hi = (self.get(axis, '-PosAct') for axis in ('TR_LO', 'TR_HI'))
        self.wait(lambda: abs(self.get('CENTERY', '-PosAct') - (lo + hi) / 2) < .02
                  and abs(self.get('GAPY', '-PosAct') - (hi - lo)) < .02, 'forward slit kinematics', 3)

    def fault_stopped(self, axis, code=None):
        self.wait(lambda: self.get(axis, '-ErrId') != 0 and self.get(axis, '-EnaAct') == 0,
                  axis + ' faults and disables', 8)
        if code is not None and self.get(axis, '-ErrId') != code:
            raise AssertionError(f'{axis}: expected 0x{code:x}, got 0x{int(self.get(axis,"-ErrId")):x}')
        config = self.profile['physical'][axis]
        base = self.prefix + f'SIM:ec0:s{config["drive_slave"]}:'
        # An invalid/frozen encoder may retain its last velocity; it cannot
        # establish stopping. Check the controller's outgoing drive commands.
        self.wait(lambda: self.client.get(base + 'velocitySetpoint01') == 0
                  and int(self.client.get(base + 'driveControl01')) & 1 == 0,
                  axis + ' drive commands settle to disabled/zero', 5)
        if 'brake' in config:
            self.wait(lambda: self.brake(axis) == 0, axis + ' brake applied after fault', 3)
