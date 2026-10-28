#!/usr/bin/env python3
"""Build and run real ExprTk/library/virtual-axis tests without hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(__file__).resolve().parents[1] / 'tools' / 'plc-test'
with tempfile.TemporaryDirectory(prefix='ecmc-plc-build-') as directory:
    # No debug symbols or optimizer work on the large ExprTk templates: keep
    # the default regression run practical. CMake honors the existing CXX env.
    subprocess.run(['cmake', '-S', str(source), '-B', directory,
                    '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Debug',
                    '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0',
                    '-DECMC_PLC_TEST_SANITIZERS=ON'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    binary = str(Path(directory) / 'ecmcPlcTest')
    checks = Path(__file__).resolve().parent / 'plc'
    for script in ('runner_checks.py', 'storage_checks.py',
                   'ethercat_checks.py', 'lut_checks.py', 'motion_checks.py',
                   'motion_safety_checks.py'):
        subprocess.run([sys.executable, str(checks / script), binary], check=True)
