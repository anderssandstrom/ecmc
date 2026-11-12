#!/usr/bin/env python3
"""Exercise and benchmark the actual change-driven RT publication block."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'devEcmcSup/motion/ecmcAxisBase.cpp').read_text()
a=s.index('  if (ignoredMotionMessage_',s.index('void ecmcAxisBase::refreshStatusWd()'))
block=s[a:s.index('  // bit 0 enable',a)]
h=(root/'devEcmcSup/motion/ecmcAxisBase.h').read_text()
fields=h[h.index('  bool diagnosticTextPublished_'):h.index('  bool ignoreDisabledMotionCmds_')]
code=r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
constexpr int ECMC_CMD_NOCMD=0;
unsigned calls=0;
std::atomic<int> output[4];
void ecmcRtLoggerPortDriverSetAxisStatusText(int,int e,int w,int l,int i) {
 ++calls;
 output[0].store(e,std::memory_order_relaxed); output[1].store(w,std::memory_order_relaxed);
 output[2].store(l,std::memory_order_relaxed); output[3].store(i,std::memory_order_relaxed);
}
struct Axis {
 struct { struct { int axisId=1,errorCode=0,warningCode=0; } status_;
          struct { int lastActiveInterlock=0; } interlocks_; } data_;
 int ignoredMotionMessage_=0;
 bool disabled=true;
 bool motionCommandDisabled(int) { return disabled; }
''' + fields + '\n __attribute__((noinline)) void refresh() {\n'+block+r'''
 }
 __attribute__((noinline)) void before() {
  if (!motionCommandDisabled(ignoredMotionMessage_)) ignoredMotionMessage_=0;
  output[0].store(data_.status_.errorCode); output[1].store(data_.status_.warningCode);
  output[2].store(data_.interlocks_.lastActiveInterlock); output[3].store(ignoredMotionMessage_);
 }
};
int main() {
 Axis a;
 a.refresh(); assert(calls==1);
 for(int n=0;n<10000;++n) a.refresh(); assert(calls==1);
 a.data_.status_.errorCode=1; a.refresh(); assert(calls==2 && output[0]==1);
 a.data_.status_.warningCode=2; a.refresh(); assert(calls==3 && output[1]==2);
 a.data_.interlocks_.lastActiveInterlock=3; a.refresh(); assert(calls==4 && output[2]==3);
 a.ignoredMotionMessage_=4; a.refresh(); assert(calls==5 && output[3]==4);
 a.disabled=false; a.refresh(); assert(calls==6 && output[3]==0);
 a.data_.status_.errorCode=0; a.refresh(); assert(calls==7 && output[0]==0);
 Axis replacement; replacement.refresh(); assert(calls==8 && output[2]==0);
 constexpr int count=5000000;
 for(int repeat=0;repeat<5;++repeat) {
  for(int mode=0;mode<2;++mode) {
   auto start=std::chrono::steady_clock::now();
   for(int n=0;n<count;++n) {
    asm volatile("" : : "g"(&a) : "memory");
    if(mode) a.refresh(); else a.before();
   }
   auto end=std::chrono::steady_clock::now();
   printf("%s %.3f ns/call\n", mode?"changed-only":"previous",
    std::chrono::duration<double,std::nano>(end-start).count()/count);
  }
 }
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(code);exe=Path(d)/'test'
 subprocess.run(['c++','-std=c++17','-O3',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Publication transitions passed; timings are isolated local microbenchmarks, not IOC jitter measurements.')
