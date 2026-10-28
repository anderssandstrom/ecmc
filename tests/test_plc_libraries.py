#!/usr/bin/env python3
"""Build and run real ExprTk/storage/EtherCAT-wrapper/LUT tests without hardware."""
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
                    '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    binary = str(Path(directory) / 'ecmcPlcTest')
    for script in ('test_runner.py', 'test_libraries.py'):
        subprocess.run([sys.executable, str(source / script), binary], check=True)
