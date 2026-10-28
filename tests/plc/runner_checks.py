#!/usr/bin/env python3
"""Standalone PLC runner: examples, validation, sequencing, isolation and timeout."""
# Expectation basis: DOC: runner syntax/isolation; INFERRED: validation; COMPAT: formatting.
# Rationale, sources and open questions: ../EXPECTATIONS.md
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
examples = Path(__file__).resolve().parents[2] / 'tools' / 'plc-test' / 'examples'
result = subprocess.run([binary, '--suite', str(examples / 'smoke.suite')],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        universal_newlines=True, timeout=30)
assert result.returncode == 0, result.stdout + result.stderr
assert '3/3 passed' in result.stdout, result.stdout
result = subprocess.run([binary, '--suite', str(examples / 'libraries.suite')],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        universal_newlines=True, timeout=30)
assert result.returncode == 0, result.stdout + result.stderr
assert '2/2 passed' in result.stdout, result.stdout
result = subprocess.run([binary, '--suite', str(examples / 'motion.suite')],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        universal_newlines=True, timeout=30)
assert result.returncode == 0, result.stdout + result.stderr
assert '1/1 passed' in result.stdout, result.stdout

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)

    def run(suite, code='global.x := global.x + 1;', timeout=5):
        (root / 'logic.plc').write_text(code)
        (root / 'test.suite').write_text(suite)
        return subprocess.run([binary, '--suite', str(root / 'test.suite'),
                               '--timeout', str(timeout)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              universal_newlines=True, timeout=timeout + 5)

    def case(body, name='check'):
        return 'case ' + name + '\n' + body + '\nend\n'

    setup = 'var global.x 0\nplc logic.plc\nrun 1\n'
    # An assertion failure must not stop subsequent cases or contaminate them.
    result = run(case(setup + 'expect global.x 2', 'fails') +
                 case(setup + 'expect global.x 1', 'fresh'))
    assert result.returncode == 1, result
    assert 'expected 2, actual 1' in result.stdout, result.stdout
    assert '[PASS] fresh' in result.stdout, result.stdout

    for code in ('global.x := ;', 'global.x := mc_home(1, 1, 1, 1, 1, 1, 1);',
                 'global.x := ds_push_asyn(0);', 'global.x := global.typo;'):
        result = run(case(setup + 'expect global.x 1'), code)
        assert result.returncode == 1, result
        assert 'compile failed' in result.stdout, result.stdout

    for body in ('var global.x 0\nrun 1', setup + 'expect global.typo 1',
                 setup + 'expect global.x 1 -1', 'var global.x nan',
                 'storage 0 -1 0', 'storage 0 4 3', 'run 0',
                 'var global.x 0\nvar global.x 1', setup + 'bogus',
                 'plc logic.plc', 'ec_memmap 0 2', 'ec_time 0 0 0 1000 0',
                 'ec\nec_memmap -1 2', 'ec\nec_memmap 0 0', 'ec\nec',
                 'ec\nec_time -1 0 0 1000 0', 'ec\nec_time 0 0 0 0 0',
                 'lut 16 absent.lut', 'lut 0 absent.lut', 'axis 0 virtual',
                 'axis 1 real', 'axis 1 virtual\naxis 1 virtual',
                 'period 0', 'period nan', 'axis 1 virtual\nperiod 0.002',
                 'feedback 1 follow', 'axis 1 virtual\nfeedback 1 nan',
                 setup + 'step 1', setup + 'expect_axis 1 busy 0',
                 'axis 1 virtual\n' + setup + 'expect_axis 1 typo 0',
                 'axis_commands 1 1 0', 'limit_switches 1 1 1', 'soft_limits 1 -1 1',
                 'axis 1 virtual\naxis_commands 1 2 0',
                 'axis 1 virtual\nlimit_switches 1 1 0.5',
                 'axis 1 virtual\nsoft_limits 1 1 -1',
                 'axis 1 virtual\nsoft_limits 1 0 0',
                 'axis 1 virtual\nsoft_limits 1 -1 nan'):
        result = run(case(body))
        assert result.returncode == 1, (body, result.stdout, result.stderr)

    (root / 'malformed.lut').write_text('bad table\n')
    (root / 'unsorted.lut').write_text('0 0\n10 10\n5 5\n')
    for filename in ('malformed.lut', 'unsorted.lut'):
        result = run(case('lut 0 ' + filename + '\n' + setup + 'expect global.x 1'))
        assert result.returncode == 1, result.stdout + result.stderr
        assert 'LUT load failed' in result.stdout, result.stdout

    result = run(case(setup + 'expect global.x 0.3 1e-12'),
                 'global.x := 0.1 + 0.2;')
    assert result.returncode == 0, result.stdout + result.stderr
    result = run(case(setup + 'expect global.x 0'), 'global.x := sqrt(-1);')
    assert result.returncode == 1, result.stdout + result.stderr

    for suite in ('', 'case missing_end\n', 'run 1\n',
                  'case same\nend\ncase same\nend\n'):
        result = run(suite)
        assert result.returncode == 2, result

    result = run(case(setup + 'expect global.x 1'),
                 'while (global.x >= 0) { global.x := global.x + 1; };', timeout=1)
    assert result.returncode == 124, result.stdout + result.stderr
    assert 'timeout' in result.stderr, result.stderr

print('Standalone PLC runner: real expressions, library calls, isolation, '
      'diagnostics, invalid inputs and timeout passed.')
