#!/usr/bin/env python3
"""Exercise production startup timing collection using deterministic timestamps."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'devEcmcSup/main/ecmcMainThread.cpp').read_text()
a=source.index('struct StartupState {')
state=source[a:source.index('}  // namespace',a)]
code=r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#define ECMC_MAX_MST_SLVS_SMS 16
std::vector<std::string> logs;
void ecmcRtLoggerLogInfo(const char *format,...) {
 char line[1024]; va_list args; va_start(args,format);
 vsnprintf(line,sizeof(line),format,args); va_end(args); logs.push_back(line);
}
timespec ts(long long ns) { return {ns/1000000000LL,ns%1000000000LL}; }
'''+state+r'''
int main() {
 StartupState state(true);
 const long long base=100000000000LL, first=base+101000000LL;
 state.recordTiming(0,ts(base),ts(first),ts(first+12000),ts(first+16000),ts(first+21000));
 assert(logs.size()==1 && logs[0].find("first send 101.016 ms")!=std::string::npos);
 assert(logs[0].find("wake latency 12.000 us")!=std::string::npos);
 assert(state.timingMinPeriodNs==0 && state.timingMaxPeriodNs==0);
 for(int i=1;i<=5000;++i) {
  const long long scheduled=first+i*1000000LL;
  const long long start=scheduled+(i==1000 ? 250000LL : 12000LL);
  const long long execution=i==2000 ? 1200000LL : 9000LL;
  state.recordTiming(0,ts(base),ts(scheduled),ts(start),ts(start+4000),ts(start+execution));
 }
 assert(logs.size()==2);
 assert(logs[1].find("max wake latency 250.000 us")!=std::string::npos);
 assert(logs[1].find("max execution 1200.000 us")!=std::string::npos);
 assert(logs[1].find("cycle period min/max 762.000/1238.000 us")!=std::string::npos);
 assert(!state.axisInitializationReleased && !state.commandsReleased && !state.timedOut);
 // A new runtime entry starts a fresh window and reports its first send again.
 StartupState next(false);
 next.recordTiming(0,ts(base),ts(first),ts(first),ts(first+1000),ts(first+2000));
 assert(logs.size()==3 && next.timingMaxWakeNs==0 && next.timingMaxExecutionNs==2000);
 puts("Startup cycle timing: first-send delay, skipped initial period, jitter extrema, overruns and bounded report cadence passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
 cpp=Path(directory)/'startup_cycle_timing.cpp';exe=Path(directory)/'startup_cycle_timing'
 cpp.write_text(code)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
