"""Shared fixture writer for PLC library checks against an already-built binary."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile


class Suite:
    def __init__(self, root):
        self.root = Path(root)
        self.names = []
        self.cases = []
        self.output_contains = []

    def case(self, name, code, expected, setup='', steps='run 1\n'):
        if name in self.names:
            raise ValueError('duplicate case: ' + name)
        self.names.append(name)
        (self.root / (name + '.plc')).write_text(code)
        self.cases.append('case ' + name + '\n' + setup + '\n' + ''.join(
            'var global.' + key + ' 0\n' for key in expected) +
            'plc ' + name + '.plc\n' + steps + '\n' + ''.join(
            'expect global.{} {} 1e-12\n'.format(key, value)
            for key, value in expected.items()) + 'end\n')

    def run(self, binary):
        path = self.root / 'checks.suite'
        path.write_text('\n'.join(self.cases))
        result = subprocess.run([str(Path(binary).resolve()), '--suite', str(path)],
                                env=dict(os.environ, TZ='UTC0'),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                universal_newlines=True, timeout=60)
        print(result.stdout, end='')
        assert result.returncode == 0, 'PLC library cases failed'
        assert '{0}/{0} passed'.format(len(self.names)) in result.stdout
        for expected in self.output_contains:
            assert expected in result.stdout, 'missing output: ' + expected


def run_cases(add_cases):
    if len(sys.argv) != 2:
        sys.exit('Usage: {} /path/to/ecmcPlcTest'.format(sys.argv[0]))
    with tempfile.TemporaryDirectory(prefix='ecmc-plc-checks-') as directory:
        suite = Suite(directory)
        add_cases(suite)
        suite.run(sys.argv[1])
