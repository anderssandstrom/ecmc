#!/usr/bin/env python3
"""A/B benchmark of complete production virtual-axis execution with offline I/O.

Compare a header-compatible AxisBase revision with the working tree:
  python3 tests/benchmark_encoder_power_path.py --baseline HEAD
Only AxisBase is varied; all other sources and compiler settings are identical.
This does not start an IOC/master/RT thread or measure real asyn publication.
"""
from pathlib import Path
import argparse
import json
import os
import statistics
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', required=True, help='Header-compatible Git revision of AxisBase')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
sup = root / 'devEcmcSup'
relative = 'devEcmcSup/motion/ecmcAxisBase.cpp'
before = subprocess.check_output(['git', '-C', str(root), 'show', args.baseline + ':' + relative], text=True)
after = (root / relative).read_text()
prefix = (root / 'tests/encoder-power/encoder_power_checks.cpp').read_text().split('int main() {', 1)[0]
main = r'''
#include <chrono>
#include <cstdlib>
int main(int argc, char **argv) {
  const bool power = argc > 1 && std::atoi(argv[1]);
  Rig r;
  auto *encoder = r.axis->getPrimEnc();
  if (power) r.idle();
  else {
    CHECK(encoder->setPowerEnable(false)==0);
    r.ready=1; r.alarm=0;
  }
  for (int i=0;i<1000;++i) r.axis->execute(true);
  CHECK(!r.axis->getErrorID()); CHECK(!r.axis->getInStartupPhase());
  constexpr int count=300000;
  const auto start=std::chrono::steady_clock::now();
  for (int i=0;i<count;++i) r.axis->execute(true);
  const auto end=std::chrono::steady_clock::now();
  CHECK(!r.axis->getErrorID());
  std::cout << std::chrono::duration<double,std::nano>(end-start).count()/count
            << " " << encoder->getActPos() << " " << encoder->getPowerState() << "\n";
}
'''
sources = [root / 'tools/plc-test/offline/motion_support.cpp', sup / 'main/ecmcError.cpp']
sources += [sup / 'com' / (name + '.cpp') for name in ('ecmcDataLink', 'ecmcDataAccess', 'ecmcDataItem')]
sources += [sup / 'motion' / (name + '.cpp') for name in (
    'ecmcAxisVirt', 'ecmcAxisData', 'ecmcAxisSequencer', 'ecmcEncoder', 'ecmcMonitor',
    'ecmcTrajectoryBase', 'ecmcTrajectoryTrapetz', 'ecmcFilter', 'ecmcMotionUtils',
    'ecmcPositionCompare', 'ecmcPIDController', 'ecmcAxisPVTSequence')]
includes = [root / 'tools/plc-test/offline'] + [sup / n for n in ('motion', 'main', 'misc', 'com', 'ethercat', 'sequence')]
def quoted(paths):
    return ' '.join('"' + str(p) + '"' for p in paths)
with tempfile.TemporaryDirectory(prefix='ecmc-power-path-bench-') as directory:
    work = Path(directory)
    (work / 'before.cpp').write_text(before)
    (work / 'after.cpp').write_text(after)
    (work / 'bench.cpp').write_text(prefix + main)
    cmake = 'cmake_minimum_required(VERSION 3.16)\nproject(EncoderPowerPath LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 17)\n'
    cmake += 'include_directories(' + quoted(includes) + ')\n'
    cmake += 'add_library(common OBJECT ' + quoted(sources) + ')\n'
    for mode in ('before', 'after'):
        cmake += f'add_executable({mode} bench.cpp {mode}.cpp $<TARGET_OBJECTS:common>)\n'
    (work / 'CMakeLists.txt').write_text(cmake)
    subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build'),
                    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + os.environ.get('CXX', 'clang++')], check=True)
    subprocess.run(['cmake', '--build', str(work / 'build'), '--parallel', '2'], check=True)
    results = {}
    for power in (0, 1):
        times = {'before': [], 'after': []}
        states = {}
        for run in range(9):
            for mode in (('before', 'after') if run % 2 == 0 else ('after', 'before')):
                output = subprocess.check_output([str(work / 'build' / mode), str(power)], text=True).split()
                times[mode].append(float(output[0]))
                states[mode] = output[1:]
        if states['before'] != states['after']:
            raise RuntimeError('Final encoder position/power state differs between variants')
        medians = {mode: statistics.median(values) for mode, values in times.items()}
        results['power_control_off' if not power else 'power_control_enabled_idle'] = {
            'median_ns_per_axis_cycle': medians,
            'change_percent': (medians['after'] / medians['before'] - 1) * 100,
            'runs_ns': times,
        }
    print(json.dumps(results, indent=2))
