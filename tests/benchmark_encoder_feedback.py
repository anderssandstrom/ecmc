#!/usr/bin/env python3
"""Offline A/B benchmark of production encoder read/write work (-O3).

Builds cached/uncached entry-type variants and alternates nine measurements.
No IOC, EtherCAT master or realtime thread is started. Results describe this
isolated path with offline asyn stubs, not complete IOC execution time.
Run: python3 tests/benchmark_encoder_feedback.py
"""
from pathlib import Path
import subprocess,statistics,json,tempfile,os
root=Path(__file__).resolve().parents[1]
sup=root/'devEcmcSup'
temporary=tempfile.TemporaryDirectory(prefix='ecmc-encoder-feedback-bench-')
work=Path(temporary.name)
source=(sup/'motion/ecmcEncoder.cpp').read_text();a=source.index('int ecmcEncoder::readHwActPos(');b=source.index('\nint ecmcEncoder::readHwLatch',a)
method=source[a:b]
# Reconstruct the uncached variant when benchmarking an optimized checkout.
if 'const bool floatingPoint = actPosEntryUsesFloatingPoint();' in method:
 method=method.replace('  const bool floatingPoint = actPosEntryUsesFloatingPoint();\n', '')
 method=method.replace('floatingPoint', 'actPosEntryUsesFloatingPoint()')
assert method.count('actPosEntryUsesFloatingPoint()')==7
uncached=source[:a]+method+source[b:]
new=method.replace('actPosEntryUsesFloatingPoint()', 'floatingPoint')
marker='  uint64_t tempRaw = 0;';assert marker in new
new=new.replace(marker,'  const bool floatingPoint = actPosEntryUsesFloatingPoint();\n\n'+marker,1)
(work/'before.cpp').write_text(uncached)
(work/'after.cpp').write_text(source[:a]+new+source[b:])
prefix=(root/'tests/encoder-power/encoder_power_checks.cpp').read_text().split('int main() {',1)[0]
(work/'bench.cpp').write_text(prefix+r'''
#include <chrono>
int main() {
 Rig r;
 auto *encoder=r.axis->getPrimEnc();
 CHECK(encoder->setPowerEnable(false)==0);
 r.ready=1; r.alarm=0; r.raw=10000;
 for(int i=0;i<1000;++i) encoder->readEntries(true);
 CHECK(!encoder->getErrorID());
 constexpr int count=1500000;
 volatile double checksum=0;
 const auto start=std::chrono::steady_clock::now();
 for(int i=0;i<count;++i) {
  r.raw+=i&1;
  checksum=checksum+encoder->readEntries(true);
  encoder->writeEntries();
 }
 const auto end=std::chrono::steady_clock::now();
 CHECK(std::isfinite(checksum));
 std::cout<<std::chrono::duration<double,std::nano>(end-start).count()/count<<"\n";
}
''')
sources=[root/'tools/plc-test/offline/motion_support.cpp',sup/'main/ecmcError.cpp']+[sup/'com'/f'{n}.cpp' for n in ['ecmcDataLink','ecmcDataAccess','ecmcDataItem']]+[sup/'motion'/f'{n}.cpp' for n in 'ecmcAxisBase ecmcAxisVirt ecmcAxisData ecmcAxisSequencer ecmcMonitor ecmcTrajectoryBase ecmcTrajectoryTrapetz ecmcFilter ecmcMotionUtils ecmcPositionCompare ecmcPIDController ecmcAxisPVTSequence'.split()]
cmake='cmake_minimum_required(VERSION 3.16)\nproject(ecmcFeedbackBench LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 17)\n'
cmake+='include_directories('+ ' '.join('"'+str(p)+'"' for p in [root/'tools/plc-test/offline']+[sup/n for n in ['motion','main','misc','com','ethercat','sequence']])+')\n'
cmake+='add_library(common OBJECT '+' '.join('"'+str(p)+'"' for p in sources)+')\n'
for mode in ['before','after']: cmake+=f'add_executable({mode} bench.cpp {mode}.cpp $<TARGET_OBJECTS:common>)\n'
(work/'CMakeLists.txt').write_text(cmake)
subprocess.run(['cmake','-S',str(work),'-B',str(work/'build'),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+os.environ.get('CXX','clang++')],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['cmake','--build',str(work/'build'),'--parallel','2'],check=True,stdout=subprocess.DEVNULL)
results={'before':[],'after':[]}
for run in range(9):
 for mode in (['before','after'] if run%2==0 else ['after','before']):
  results[mode].append(float(subprocess.check_output([str(work/'build'/mode)],text=True).strip()))
summary={mode:{'median_ns':statistics.median(v),'min_ns':min(v),'max_ns':max(v),'runs_ns':v} for mode,v in results.items()}
summary['change_percent']=(summary['after']['median_ns']/summary['before']['median_ns']-1)*100
print(json.dumps(summary,indent=2))
temporary.cleanup()
