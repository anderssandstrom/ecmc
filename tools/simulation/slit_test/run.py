#!/usr/bin/env python3
"""Run modular slit acceptance tests against the dedicated SIM IOC (never hardware)."""
import argparse
from datetime import datetime, timezone
import importlib
import json
from pathlib import Path
import signal
import sys
import time
import unittest
import xml.etree.ElementTree as ET

SIMULATION = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIMULATION))
from slit_test.plant import Plant
from slit_test.rig import Client, Rig

MODULES = ('motion', 'limits', 'enable', 'feedback', 'sync', 'brake')
DEFAULT_CONFIG = (Path(__file__).resolve().parents[4].parent / 'ecmccfg/examples/PSI/lab_setup/'
                  'master_slave/slit_epics_2026/slit_matrix_sim_test')


def make_suite(rig, loops, modules):
    tests = []
    for loop in loops:
        for module in modules:
            loaded = unittest.defaultTestLoader.loadTestsFromModule(importlib.import_module('slit_test.cases.' + module))
            for group in loaded:
                for test in group:
                    test.rig, test.loop = rig, loop
                    tests.append(test)
    return unittest.TestSuite(tests)


class Result(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = []

    def getDescription(self, test):
        return f'[{test.loop}] {test.id()}'

    def startTest(self, test):
        self.started = time.monotonic()
        super().startTest(test)

    def record(self, test, status, detail=''):
        self.records.append({'test': test.id(), 'loop': test.loop, 'status': status,
                             'seconds': time.monotonic() - self.started, 'detail': detail,
                             'snapshot': test.rig.snapshot()})

    def addSuccess(self, test):
        self.record(test, 'passed')
        super().addSuccess(test)

    def addFailure(self, test, error):
        self.record(test, 'failed', self._exc_info_to_string(error, test))
        super().addFailure(test, error)

    def addError(self, test, error):
        self.record(test, 'error', self._exc_info_to_string(error, test))
        super().addError(test, error)

    def addSkip(self, test, reason):
        self.record(test, 'skipped', reason)
        super().addSkip(test, reason)


def write_reports(path, document):
    path = Path(path)
    path.write_text(json.dumps(document, indent=2) + '\n')
    suite = ET.Element('testsuite', name='ECMC slit simulation', tests=str(len(document['tests'])))
    for item in document['tests']:
        case = ET.SubElement(suite, 'testcase', name=item['loop'] + ':' + item['test'], time=str(item['seconds']))
        if item['status'] in ('failed', 'error', 'skipped'):
            tag = {'failed': 'failure', 'error': 'error', 'skipped': 'skipped'}[item['status']]
            ET.SubElement(case, tag).text = item['detail']
    ET.ElementTree(suite).write(path.with_suffix('.xml'), encoding='utf-8', xml_declaration=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', help='IOC prefix including trailing colon, e.g. c6025a-08:')
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG, help='separate slit_matrix_sim_test directory')
    parser.add_argument('--loop', choices=('open', 'closed', 'both'), default='both')
    parser.add_argument('--case', choices=MODULES, action='append', help='select modules; repeatable')
    parser.add_argument('--run', action='store_true', help='connect, start the coupled plant and execute motion/fault tests')
    parser.add_argument('--plant-only', action='store_true', help='run the coupled plant without issuing motion test commands')
    parser.add_argument('--keep-going', action='store_true', help='continue after a failed test (default: fail fast)')
    parser.add_argument('--report', default='slit-report.json', help='JSON report; matching JUnit XML is also written')
    args = parser.parse_args(argv)
    if args.run and args.plant_only:
        parser.error('choose --run or --plant-only')
    profile = json.loads((args.config / 'profile.json').read_text())
    loops = ('open', 'closed') if args.loop == 'both' else (args.loop,)
    modules = args.case or MODULES
    if not (args.run or args.plant_only):
        for test in make_suite(None, loops, modules):
            print(f'[{test.loop}] {test.id()}')
        print('Plan only; no EPICS connection or writes. Add --prefix IOC: --run to execute.')
        return 0
    if not args.prefix or not args.prefix.endswith(':'):
        parser.error('--prefix ending in : is required for live tests')
    from epics import PV
    from epics.ca import CAThread
    client = Client(PV)
    plant = Plant(profile, args.prefix + 'SIM:', PV, CAThread)
    rig = Rig(profile, args.prefix, client, plant)
    document = {'utc': datetime.now(timezone.utc).isoformat(), 'prefix': args.prefix,
                'profile': profile, 'tests': [], 'cleanup_errors': []}
    def interrupted(_signal, _frame):
        raise KeyboardInterrupt
    old_handler = signal.signal(signal.SIGTERM, interrupted)
    authorized = False
    result = None
    exit_code = 0
    try:
        rig.guard()  # Before any plant writes or motion commands.
        authorized = True
        plant.start()
        rig.wait(lambda: all(rig.get(axis, '-InStartup') == 0 for axis in rig.names), 'IOC axis startup completes', 15)
        if args.plant_only:
            print('Coupled slit plant running. Ctrl+C stops it; do not run another model on slaves 9/12/14.')
            while True:
                plant.check()
                time.sleep(.2)
        suite = make_suite(rig, loops, modules)
        def make_result(*args):
            nonlocal result
            result = Result(*args)
            return result
        result = unittest.TextTestRunner(verbosity=2, resultclass=make_result, failfast=not args.keep_going).run(suite)
        document['tests'] = result.records
        document['trace'] = rig.trace
        exit_code = 0 if result.wasSuccessful() else 1
    except KeyboardInterrupt:
        document['interrupted'] = True
        exit_code = 130
    except Exception as error:
        document['error'] = str(error)
        print('Slit tests stopped: ' + str(error), file=sys.stderr)
        exit_code = 1
    finally:
        if authorized:
            plant.clear_faults()
            document['cleanup_errors'] = rig.stop_all()
        plant.close()
        client.close()
        signal.signal(signal.SIGTERM, old_handler)
        if args.run:
            if result is not None:
                document['tests'] = result.records
            write_reports(args.report, document)
    return 1 if document['cleanup_errors'] else exit_code


if __name__ == '__main__':
    raise SystemExit(main())
