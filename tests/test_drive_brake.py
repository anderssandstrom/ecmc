#!/usr/bin/env python3
"""Compile and step production drive brake logic with controlled I/O and feedback."""
# Expectation basis: DOC: sequence/units; INFERRED: safety; COMPAT/REVIEW: timing/restart.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / 'drive'
with tempfile.TemporaryDirectory(prefix='ecmc-drive-brake-') as directory:
    subprocess.run(['cmake', '-S', str(source), '-B', directory,
                    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run([str(Path(directory) / 'driveBrakeTests')], check=True)
