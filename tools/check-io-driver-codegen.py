#!/usr/bin/env python3
"""Compare optimized direct/adapter code for representative cyclic I/O phases.

No IOC or hardware is built. This is a compiler check, not a cycle/jitter test.
Run with CXX=clang++ or CXX=g++; --optimization=-O3 selects production optimization.
"""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess
import tempfile

SUP = Path(__file__).resolve().parents[1] / 'devEcmcSup'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--optimization', choices=['-O2', '-O3'], default='-O2')
args = parser.parse_args()
# Out-of-line endpoint declarations keep the real forwarding calls visible.
provider = r'''
#include <time.h>
struct ecmcEc {
  bool getInitDone();
  void receive(timespec, timespec);
  void checkDomainsState();
  int statusOK();
  void send(timespec);
  void setTimeOffest(timespec);
  bool timingStartupReady(bool = false) const;
  void resetStartupCommunicationErrors();
  bool checkState();
  void slowExecute();
};
extern void control(int);
'''
signature = 'extern "C" void cycle(ecmcEc &master, int masterId, timespec input, timespec offset) {\n'
direct = signature + '''
  const bool active = master.getInitDone();
  int status = 1;
  if (active) {
    master.receive(input, offset);
    master.checkDomainsState();
    status = master.statusOK();
  }
  control(status);
  if (active) master.send(offset);
  else if (masterId < 0) master.setTimeOffest(offset);
}
'''
adapted = '#define ECMCEC_H_\n#include "ecmcEthercatDriver.h"\n' + signature + '''
  ecmcEthercatDriver io(master);
  const bool active = io.isActive();
  int status = 1;
  if (active) {
    io.readInputs(input, offset);
    status = io.updateIoStatus();
  }
  control(status);
  if (active) io.writeOutputs(offset);
  else if (masterId < 0) io.setTimeOffset(offset);
}
'''


def instructions(assembly):
    body = assembly[assembly.index('cycle:'):]
    body = body[:re.search(r'\s*\.size\s+cycle\b', body).start()]
    result, labels = [], {}
    for line in body.splitlines():
        line = line.split('#', 1)[0].strip()
        if not line or (line.startswith('.') and not line.endswith(':')):
            continue
        if re.match(r'\.L(?:FB|FE|func_begin|func_end)\d+:', line):
            continue
        line = re.sub(r'\.L[\w.]+', lambda m: labels.setdefault(m[0], '.L' + str(len(labels))), line)
        result.append(re.sub(r'\s+', ' ', line))
    return result


with tempfile.TemporaryDirectory(prefix='ecmc-io-codegen-') as directory:
    versions = []
    for name, body in [('direct', direct), ('adapter', adapted)]:
        cpp, assembly = Path(directory) / (name + '.cpp'), Path(directory) / (name + '.s')
        cpp.write_text(provider + body)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', args.optimization, '-S',
                        '-I' + str(SUP / 'com'), '-I' + str(SUP / 'ethercat'),
                        str(cpp), '-o', str(assembly)], check=True)
        versions.append(instructions(assembly.read_text()))
    if versions[0] != versions[1]:
        print('\n'.join(difflib.unified_diff(*versions, fromfile='direct', tofile='adapter')))
        raise SystemExit('Code differs; inspect before claiming equivalent generated code.')
print('Direct and concrete-adapter phase probes have identical optimized instructions.')
print('This does not measure full IOC cycle time, jitter or hardware deadlines.')
