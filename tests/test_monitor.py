#!/usr/bin/env python3
"""Step production axis monitor timing, interlocks and recovery without hardware."""
# Expectation basis: DOC: thresholds; INFERRED: sustained trip; COMPAT/REVIEW: timing/latches.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / 'monitor'
with tempfile.TemporaryDirectory(prefix='ecmc-monitor-') as directory:
    subprocess.run(['cmake', '-S', str(source), '-B', directory,
                    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run([str(Path(directory) / 'monitorTests')], check=True)
