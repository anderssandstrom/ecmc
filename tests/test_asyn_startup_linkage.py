#!/usr/bin/env python3
"""Compile startup-helper declarations with their production linkage scopes.

Retain the enclosing language linkage from the full source, unlike isolated
method tests which cannot catch an accidental surrounding extern-C block.
EPICS function bodies are replaced; a C caller verifies the public start ABI.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'devEcmcSup/com/ecmcAsynPortDriver.cpp').read_text()
HELPERS = ('collectSdoRestoreRecords', 'startQueuedSdosThenRuntime')
PUBLIC = ('ecmcAsynPortDriverConfigure', 'ecmcConfigOrDie', 'ecmcConfig',
          'ecmcStartAppModeAsync', 'ecmcAsynPortDriverRegister')
# Ignore comments/string contents when tracking braces and explicit linkage.
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|extern\s+"C(?:\+\+)?"\s*\{|'
                    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def linkage_at(text, offset):
    stack = []
    for token in TOKENS.finditer(text):
        if token.start() >= offset: break
        value = token.group()
        if value.startswith('extern'):
            stack.append('C++' if 'C++' in value else 'C')
        elif value == '{': stack.append(None)
        elif value == '}': stack.pop()
    return next((scope for scope in reversed(stack) if scope is not None), 'C++')


def skeleton(text, check=True):
    names = '|'.join(HELPERS+PUBLIC)
    declarations = list(re.finditer(r'\b(int|void)\s+('+names+r')\s*\(([^)]*)\)\s*([;{])', text))
    seen = {name: [] for name in HELPERS+PUBLIC}
    result = []
    for match in declarations:
        kind, name, params, ending = match.groups()
        linkage = linkage_at(text, match.start())
        seen[name].append(ending)
        if check: assert linkage == ('C++' if name in HELPERS else 'C'), (name, linkage)
        body = ';' if ending == ';' else (' {}' if kind == 'void' else ' { return 0; }')
        declaration = f'{kind} {name}({params}){body}'
        if name in HELPERS: declaration = 'namespace { '+declaration+' }'
        result.append(f'extern "{linkage}" {{ {declaration} }}')
    if check:
        for name in HELPERS: assert seen[name] == [';', '{'], (name, seen[name])
        for name in PUBLIC: assert seen[name] == ['{'], (name, seen[name])
    return '\n'.join(result)+'\n'


with tempfile.TemporaryDirectory(prefix='ecmc-startup-linkage-') as tmp:
    tmp = Path(tmp)
    cpp = tmp/'startup.cpp'
    cpp.write_text(skeleton(source))
    c = tmp/'caller.c'
    c.write_text('int ecmcStartAppModeAsync(double);\nint main(void) { return ecmcStartAppModeAsync(0); }\n')
    subprocess.run([os.environ.get('CC','cc'), '-c', str(c), '-o', str(tmp/'caller.o')], check=True)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', str(cpp), str(tmp/'caller.o'), '-o', str(tmp/'check')], check=True)
    subprocess.run([str(tmp/'check')], check=True)
    # Reproduce the reported regression: balanced scopes, but SDO definitions
    # inherit the outer C linkage while their early declarations remain C++.
    bad = source.replace('}  // extern "C"\n\n// Queued PREOP SDO', '// Queued PREOP SDO', 1)
    bad = bad.replace('extern "C" {\n\nstatic const iocshArg sdoRestoreSlaveArg', 'static const iocshArg sdoRestoreSlaveArg', 1)
    assert bad != source
    # GCC rejects this anonymous-namespace linkage conflict, whereas Clang
    # accepts it. Check the production scope invariant directly so the
    # regression check does not depend on a compiler-specific diagnostic.
    try:
        skeleton(bad)
    except AssertionError as error:
        assert error.args[0] == ('collectSdoRestoreRecords', 'C'), error
    else:
        raise AssertionError('incorrect helper linkage was not detected')
print('Startup helpers retain C++ linkage; public IOC functions link from C; original incorrect linkage is detected.')
