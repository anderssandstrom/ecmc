#!/usr/bin/env python3
"""Build production encoder power/startup/enable checks, without building an IOC."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / 'encoder-power'
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-power-') as directory:
    subprocess.run(['cmake', '-S', str(source), '-B', directory,
                    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run([str(Path(directory)/'encoderPowerTests')], check=True)
