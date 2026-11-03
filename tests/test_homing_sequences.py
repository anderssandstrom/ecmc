#!/usr/bin/env python3
"""Step production homing with an ideal follower; no IOC or motor record build."""
# USER/DOC: sequence definitions, physical switch references and absolute phase.
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / 'homing'
with tempfile.TemporaryDirectory(prefix='ecmc-homing-') as directory:
    subprocess.run(['cmake', '-S', str(source), '-B', directory,
                    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run([str(Path(directory) / 'homingTests')], check=True)
