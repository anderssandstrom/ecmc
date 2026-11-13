#!/usr/bin/env python3
"""Compile the production encoder diagnostic dump against the real encoder header.

The encoder-control-word refactor removed separate arm parameter members. The
native encoder tests did not compile the diagnostic writer that still used them.
Only the diagnostic output sinks are stand-ins here; member access is checked
against the actual production declaration, including its friend access rules.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'
source = (SUP / 'motion/ecmcMotionDiag.cpp').read_text()
start = source.index('  void dumpEncoder(const ecmcEncoder& o, const ecmcDiagPath& p) {')
brace = source.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
code = r'''
#include "ecmcMotionDiagSnapshot.h"
#include "ecmcEncoder.h"
class ecmcMotionStateYamlWriter {
  ecmcDiagPath child(const ecmcDiagPath&, const char*) { return {}; }
  template<class T> void value(const ecmcDiagPath&, const T&) {}
  template<class T> void pointer(const ecmcDiagPath&, T*) {}
  void dumpEcEntryLink(const ecmcDataLink&, const ecmcDiagPath&) {}
  void dumpFilter(const ecmcFilter&, const ecmcDiagPath&) {}
''' + method + '\n};\n'
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-diagnostics-') as tmp:
    cpp = Path(tmp) / 'checks.cpp'
    cpp.write_text(code)
    includes = [ROOT / 'tools/plc-test/offline'] + [
        SUP / part for part in ('motion', 'main', 'com', 'misc', 'ethercat')]
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-fsyntax-only'] +
                   ['-I' + str(path) for path in includes] + [str(cpp)], check=True)
print('Encoder diagnostic dump compiles against the current production encoder members.')
