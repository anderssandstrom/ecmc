#!/usr/bin/env python3
"""Discover ECMC simulated slaves and run supported models using one PV prefix."""
import argparse
from dataclasses import dataclass
import json
import logging
import math
import numbers
import signal
import sys
import threading

import el7041

# Explicit compatible velocity-mode descriptors. Native PV types are also checked.
MODEL_MODULES = {'EL7041': el7041, 'EL7041-0052': el7041,
                 'EL7031': el7041, 'EL7047': el7041}


def model_module(hardware):
    module = MODEL_MODULES.get(hardware)
    if module is None and hardware.startswith('EL7031-') and len(hardware) > len('EL7031-'):
        module = MODEL_MODULES['EL7031']
    return module


@dataclass(frozen=True)
class Slave:
    master: int
    position: int
    hardware: str

    @property
    def path(self):
        return f'ec{self.master}.s{self.position}'


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
               duration=None, settings=None, stop=None):
    """One external worker per slave; a failed model does not stop other slaves."""
    stop = stop if stop is not None else threading.Event()
    settings = settings or {}
    results, result_lock, threads = {}, threading.Lock(), []

    def worker(slave, module):
        io = None
        result = 0
        try:
            values = dict(settings.get(slave.path, {}))
            initial_revs = values.pop('initial_position_revs', None)
            options = module.Settings(**values)
            io = module.EpicsIO(prefix, slave.master, slave.position, '01', timeout, pv_factory)
            io.connect()
            initial = io.initial_position() if initial_revs is None else initial_revs * options.encoder_counts_per_rev
            model = module.Model(options, initial)
            logging.info('%s %s: connected, %g full steps/s, %d steps/rev, %d counts/rev',
                         slave.path, slave.hardware, options.full_steps_per_second,
                         options.motor_steps_per_rev, options.encoder_counts_per_rev)
            module.run(io, model, 1 / rate, duration, sleep=stop.wait, stop=stop)
        except Exception as error:
            result = 1
            logging.error('%s %s: model stopped: %s', slave.path, slave.hardware, error)
        finally:
            if io is not None:
                try:
                    io.close()
                except Exception as error:
                    result = 1
                    logging.error('%s: disconnect failed: %s', slave.path, error)
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
        if unknown:
            parser.error('settings refer to unselected/unknown slaves: ' + ', '.join(sorted(unknown)))
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
