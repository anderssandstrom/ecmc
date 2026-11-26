"""Compile and exercise the production metadata store, without EPICS/RT."""
from pathlib import Path
import subprocess
import os
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp)/'test.cpp'
    source.write_text(r'''
#include "ecmcPlcDiagnostic.h"
#include <cassert>
#include <thread>
int main() {
  using namespace ecmcPlcDiagnostic;
  publish(0,"3\nabc\nL\tx\n");
  assert(!request(-1,1)); assert(!request(0,0));
  assert(request(0,12)); assert(status(0)==12);
  assert(!request(1,13)); assert(!request(0,14));
  assert(status(0)==12);
  char data[capacity]; size_t actual=999;
  assert(!read(0,data,1,&actual)); assert(actual==0);
  assert(read(0,data,capacity,&actual));
  assert(std::string(data,actual)=="ECMCPLC1 12\n3\nabc\nL\tx\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(1010));
  assert(request(1,15)); assert(status(1)==-15);
  assert(read(1,data,capacity,&actual)); assert(actual==0);
  publish(0,std::string(capacity,'x')); assert(status(0)==0);
  invalidate(1); assert(status(1)==0);
}
''')
    binary = str(Path(tmp)/'test')
    subprocess.run([os.environ.get('CXX', 'c++'),'-std=c++11','-pthread','-I'+str(root/'devEcmcSup/plc'),
                    '-I'+str(root/'devEcmcSup/main'),str(source),'-o',binary],check=True)
    subprocess.run([binary],check=True)
print('PLC metadata store tests passed')
