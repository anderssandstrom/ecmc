#!/usr/bin/env python3
"""Run hardware-free regression scripts, keeping failure diagnostics visible."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def probe_toolchain(env):
    """Check both languages, including the sanitizer linker and runtime."""
    with tempfile.TemporaryDirectory(prefix='ecmc-toolchain-') as directory:
        for key, default, suffix, standard, source in (
                ('CC', 'cc', '.c', 'c99', 'int main(void) { return 0; }\n'),
                ('CXX', 'c++', '.cpp', 'c++17',
                 '#include <vector>\nint main() { std::vector<int> v{0}; return v[0]; }\n')):
            compiler = env.get(key, default)
            path = Path(directory) / ('probe' + suffix)
            path.write_text(source)
            binary = str(path) + '.out'
            for command in (
                    [compiler, '-std=' + standard, '-fsanitize=address,undefined',
                     str(path), '-o', binary], [binary]):
                try:
                    result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT,
                                            universal_newlines=True, timeout=30)
                except (OSError, subprocess.TimeoutExpired) as error:
                    return '{}={}: {}'.format(key, compiler, error)
                if result.returncode:
                    return '{}={}: {} exited with status {}\n{}'.format(
                        key, compiler, command[0], result.returncode, result.stdout.strip())
    return None


def select_toolchain(env):
    """Keep explicit compiler choices; try Clang if implicit defaults are broken."""
    selected = dict(env)
    error = probe_toolchain(selected)
    if error is None:
        return selected
    if 'CC' not in env and 'CXX' not in env:
        alternative = dict(env, CC='clang', CXX='clang++')
        alternative_error = probe_toolchain(alternative)
        if alternative_error is None:
            print('Default compiler/sanitizer check failed:\n' + error, flush=True)
            print('Using CC=clang CXX=clang++ for this test run.', flush=True)
            return alternative
        error += '\nClang fallback also failed:\n' + alternative_error
    raise RuntimeError(
        'Compiler/sanitizer preflight failed; no tests were run.\n' + error +
        '\nInstall matching sanitizer runtime libraries or select a working pair:\n'
        '  CC=clang CXX=clang++ python3 tests/run_tests.py\n'
        'If the diagnostic says LeakSanitizer cannot run under ptrace, use\n'
        'ASAN_OPTIONS=detect_leaks=0 for that environment. ASan/UBSan remain enabled.')


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

    try:
        environment = select_toolchain(os.environ)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
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
                    env=environment, stdout=output, stderr=subprocess.STDOUT,
                    timeout=args.timeout)
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
