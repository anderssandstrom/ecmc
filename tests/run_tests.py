#!/usr/bin/env python3
"""Run hardware-free regression scripts, keeping failure diagnostics visible."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='also print output from passing tests')
    parser.add_argument('--timeout', type=float, default=120,
                        help='maximum seconds per script (default: 120)')
    args = parser.parse_args()
    if not 0 < args.timeout < float('inf'):
        parser.error('--timeout must be a finite positive number')

    directory = Path(__file__).resolve().parent
    tests = sorted(directory.glob('test_*.py'))
    if not tests:
        print('No test_*.py scripts found.', file=sys.stderr)
        return 1

    failures = 0
    for test in tests:
        print(f'RUN  {test.name}', flush=True)
        started = time.monotonic()
        # A temporary log avoids retaining potentially large sanitizer output
        # in memory and preserves diagnostics when a script times out.
        with tempfile.TemporaryFile(mode='w+t') as output:
            try:
                result = subprocess.run(
                    [sys.executable, str(test)], cwd=directory.parent,
                    stdout=output, stderr=subprocess.STDOUT, timeout=args.timeout)
                passed = result.returncode == 0
                detail = '' if passed else f' (exit {result.returncode})'
            except subprocess.TimeoutExpired:
                passed = False
                detail = f' (timeout after {args.timeout:g}s)'
            failures += not passed
            print(f'{"PASS" if passed else "FAIL"} {test.name}{detail} '
                  f'[{time.monotonic() - started:.2f}s]', flush=True)
            if args.verbose or not passed:
                output.seek(0)
                for line in output:
                    print(line, end='')
    print(f'\n{len(tests) - failures}/{len(tests)} passed; {failures} failed.')
    return 1 if failures else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
