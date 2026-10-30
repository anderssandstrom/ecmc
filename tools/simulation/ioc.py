#!/usr/bin/env python3
"""Discover ECMC simulated slaves and run supported models using one PV prefix."""
import argparse
from dataclasses import dataclass, replace
import json
import logging
import math
import numbers
import re
import signal
import sys
import threading
import queue
import time

import el7041
import ds402
import digital
import encoder
import manual

# Stepper descriptors; DS402 families are matched below. Native PV types are checked.
MODEL_MODULES = {'EL7041': el7041, 'EL7041-0052': el7041,
                 'EL7031': el7041, 'EL7047': el7041}


def model_module(hardware):
    if hardware in digital.LAYOUTS:
        return digital
    if encoder.supported(hardware):
        return encoder
    if re.fullmatch(r'(?:E[A-Z]+72\d{2}|[A-Z]+7062)(?:[-_].*)?', hardware) and not re.search(r'(?:^|[-_])(?:CST(?:CA)?|DMC)(?:[-_]|$)', hardware):
        return ds402
    module = MODEL_MODULES.get(hardware)
    if module is None and hardware.startswith('EL7031-') and len(hardware) > len('EL7031-'):
        module = MODEL_MODULES['EL7031']
    return module


@dataclass(frozen=True)
class Slave:
    master: int
    position: int
    hardware: str
    channel: str = None

    @property
    def path(self):
        return self.base_path + ('.ch' + self.channel if self.channel else '')

    @property
    def base_path(self):
        return f'ec{self.master}.s{self.position}'


def expand_channels(slaves, prefix, pv_factory, timeout=1, settings=None):
    """Expose independent configured channels of 7062 terminals."""
    expanded = []
    for slave in slaves:
        if not re.fullmatch(r'[A-Z]+7062(?:[-_].*)?', slave.hardware):
            expanded.append(slave)
            continue
        values = (settings or {}).get(slave.base_path, {})
        channels = [str(values['channel']).zfill(2)] if 'channel' in values else ['01', '02']
        found = []
        for channel in channels:
            pv = pv_factory(f'{prefix}ec{slave.master}:s{slave.position}:driveControl{channel}', auto_monitor=False)
            try:
                if pv.wait_for_connection(timeout=timeout):
                    found.append(replace(slave, channel=channel))
            finally:
                pv.disconnect()
        expanded.extend(found or [slave])  # Missing configuration is reported by its worker.
    return expanded


def discover(prefix, master, pv_factory, timeout=1, first_slave=None):
    def read(name, text=False):
        pv = pv_factory(name, auto_monitor=False)
        try:
            if not pv.wait_for_connection(timeout=timeout):
                raise RuntimeError('cannot connect to discovery PV ' + name)
            value = pv.get(as_string=text, use_monitor=False, timeout=timeout)
            if value is None:
                raise RuntimeError('cannot read discovery PV ' + name)
            return value
        finally:
            pv.disconnect()

    def index(value):
        if not isinstance(value, numbers.Real) or not math.isfinite(value) or value != int(value) or not -1 <= value <= 65535:
            raise RuntimeError('invalid slave index: ' + repr(value))
        return int(value)

    current = index(read(prefix + 'FirstSlave') if first_slave is None else first_slave)
    found, visited = [], set()
    while current >= 0:
        if current in visited or len(visited) >= 4096:
            raise RuntimeError(f'invalid/cyclic slave discovery chain at slave {current}')
        visited.add(current)
        base = f'{prefix}ec{master}:s{current}:'
        hardware = read(base + 'HWType', text=True)
        if not isinstance(hardware, str) or not hardware.strip():
            raise RuntimeError('invalid hardware type at ' + base)
        found.append(Slave(master, current, hardware.strip()))
        current = index(read(base + 'NxtObjId'))
    return found


def run_models(slaves, prefix, pv_factory, thread_factory, rate=100, timeout=1,
               duration=None, settings=None, stop=None, report=None, commands=None):
    """One external worker per slave; a failed model does not stop other slaves."""
    stop = stop if stop is not None else threading.Event()
    settings = settings or {}
    results, result_lock, threads = {}, threading.Lock(), []

    def worker(slave, module):
        io = None
        fields = None
        result = 0
        try:
            values = dict(settings.get(slave.base_path, {}))
            values.update(settings.get(slave.path, {}))
            if module in (digital, encoder):
                if values:
                    raise ValueError('manual terminals do not accept drive settings')
                io = module.Terminal(prefix, slave, timeout, pv_factory)
                io.connect()
                logging.info('%s %s: connected, manual %s', slave.path, slave.hardware, io.direction)
                digital.run(io, slave.path, stop, commands.get(slave.path) if commands else None,
                            report, duration)
                return
            initial_revs = values.pop('initial_position_revs', None)
            channel = values.pop('channel', '01')
            channel = str(slave.channel or channel).zfill(2)
            options = module.Settings(**values)
            io = module.EpicsIO(prefix, slave.master, slave.position, channel, timeout, pv_factory)
            if module is ds402:
                io.mode = values.get('mode')
            io.connect()
            if module is ds402:
                options = replace(options, mode=io.mode)
            initial = io.initial_position() if initial_revs is None else initial_revs * options.encoder_counts_per_rev
            model = module.Model(options, initial)
            logging.info('%s %s: connected, %s', slave.path, slave.hardware, options)
            if report:
                names = [pv.pvname for key, pv in io.pvs.items() if key != 'initial' and pv.connected]
                base = f'{prefix}ec{slave.master}:s{slave.position}:'
                # Optional native fields remain manual; models do not invent their behavior.
                names += [base + name + channel for name in (
                    'binaryInputs', 'voltageActual', 'temperatureActual', 'torqueActual',
                    'torqueOffset', 'followingError', 'touchProbeControl', 'touchProbeStatus')]
                names.append(base + 'positionActual' + channel + '_2')
                for probe in (1, 2):
                    names += [base + name + channel + '_' + str(probe) for name in (
                        'touchProbePositionPos', 'touchProbePositionNeg',
                        'touchProbeTimestampPos', 'touchProbeTimestampNeg')]
                names = list(dict.fromkeys(names))
                fields = manual.Fields(names, timeout, pv_factory)
                fields.connect()
            next_display = 0.0
            write_error = None
            def update(control, command, feedback):
                nonlocal next_display, write_error
                pending = commands.get(slave.path) if commands else None
                for _ in range(32):
                    if pending is None:
                        break
                    try:
                        name, value = pending.get_nowait()
                    except queue.Empty:
                        break
                    try:
                        fields.write(name, value)
                        write_error = None
                    except ValueError as error:
                        write_error = str(error)
                now = time.monotonic()
                if now < next_display:
                    return
                next_display = now + .1
                report(slave.path, 'write rejected: ' + write_error if write_error else 'running', fields.snapshot())
            extra = {'update': update} if report else {}
            module.run(io, model, 1 / rate, duration, sleep=stop.wait, stop=stop, **extra)
        except Exception as error:
            result = 1
            logging.error('%s %s: model stopped: %s', slave.path, slave.hardware, error)
            if report:
                report(slave.path, 'error: ' + str(error), {})
        finally:
            if fields is not None:
                fields.close()
            if io is not None:
                try:
                    io.close()
                except Exception as error:
                    result = 1
                    logging.error('%s: disconnect failed: %s', slave.path, error)
                    if report:
                        report(slave.path, 'error: disconnect failed: ' + str(error), {})
            if report and not result:
                report(slave.path, 'stopped', {})
            with result_lock:
                results[slave.path] = result

    try:
        for slave in slaves:
            module = model_module(slave.hardware)
            if module is None:
                continue
            thread = thread_factory(target=worker, args=(slave, module), name='sim-' + slave.path)
            threads.append(thread)
            thread.start()
        while any(thread.is_alive() for thread in threads):
            for thread in threads:
                thread.join(timeout=.1)
    except KeyboardInterrupt:
        stop.set()
    finally:
        stop.set()
        for thread in threads:
            if thread.ident is not None:
                thread.join()
    return results


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, help='one simulation PV prefix, e.g. c6025a-08:SIM:')
    parser.add_argument('--master', type=int, default=0)
    parser.add_argument('--first-slave', type=int, help='override FirstSlave for a master-specific chain')
    parser.add_argument('--slave', type=int, action='append', help='run only these discovered slaves; repeatable')
    parser.add_argument('--list', action='store_true', help='discover and list supported/unsupported slaves without writes')
    parser.add_argument('--gui', action='store_true', help='live Tk display with manual digital input controls')
    parser.add_argument('--rate', type=float, default=100)
    parser.add_argument('--timeout', type=float, default=1)
    parser.add_argument('--duration', type=float, help='stop models after this many seconds')
    parser.add_argument('--settings', help='JSON object: ec0.s14 -> model settings overrides')
    args = parser.parse_args(argv)
    if args.master < 0 or not args.prefix:
        parser.error('master must be nonnegative and prefix must not be empty')
    if any(not math.isfinite(value) or value <= 0
           for value in (args.rate,args.timeout,args.duration if args.duration is not None else 1)):
        parser.error('rate, timeout and duration must be finite and positive')
    settings = {}
    if args.settings:
        try:
            with open(args.settings) as source:
                settings = json.load(source)
            if not isinstance(settings, dict) or any(not isinstance(v,dict) for v in settings.values()):
                raise ValueError('expected an object mapping ecN.sM to setting objects')
        except (OSError, ValueError) as error:
            parser.error(str(error))
    try:
        from epics import PV
        from epics.ca import CAThread
    except ImportError:
        parser.exit(2,'pyepics is required: python3 -m pip install pyepics\n')
    logging.basicConfig(level=logging.INFO,format='%(levelname)s: %(message)s')
    def terminate(_signum, _frame):
        raise KeyboardInterrupt
    old_handler = signal.signal(signal.SIGTERM,terminate)
    try:
        slaves = discover(args.prefix,args.master,PV,args.timeout,args.first_slave)
        for slave in slaves:
            state = 'supported' if model_module(slave.hardware) is not None else 'unsupported; skipped'
            print(f'{slave.path:12} {slave.hardware:22} {state}',flush=True)
        if args.list:
            return 0
        selected = [slave for slave in slaves if model_module(slave.hardware) is not None
                    and (args.slave is None or slave.position in args.slave)]
        if not selected:
            logging.error('no supported slaves selected; no feedback was written')
            return 1
        unknown = set(settings) - {slave.path for slave in selected}
        selected = expand_channels(selected, args.prefix, PV, args.timeout, settings)
        unknown -= {slave.path for slave in selected}
        if unknown:
            parser.error('settings refer to unselected/unknown slaves: ' + ', '.join(sorted(unknown)))
        if args.gui:
            import gui
            return gui.run(selected, args.prefix, PV, CAThread, args.rate, args.timeout,
                           args.duration, settings, run_models)
        results = run_models(selected,args.prefix,PV,CAThread,args.rate,args.timeout,args.duration,settings)
        return int(any(results.values()))
    except KeyboardInterrupt:
        return 0
    except (RuntimeError, ValueError) as error:
        logging.error('%s',error)
        return 1
    finally:
        signal.signal(signal.SIGTERM,old_handler)


if __name__ == '__main__':
    raise SystemExit(main())
