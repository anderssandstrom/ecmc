#!/usr/bin/env python3
"""Check the motor-record offset/direction PLC bridge and its RT contract."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / "devEcmcSup"

definitions = (SUP / "main/ecmcDefinitions.h").read_text()
axis_header = (SUP / "motion/ecmcAxisBase.h").read_text()
axis_source = (SUP / "motion/ecmcAxisBase.cpp").read_text()
plc_source = (SUP / "plc/ecmcPLCDataIF.cpp").read_text()
motor_source = (SUP / "motor/ecmcMotorRecordController.cpp").read_text()

assert '#define ECMC_AXIS_DATA_STR_MR_OFF "mr.off"' in definitions
assert '#define ECMC_AXIS_DATA_STR_MR_DIRECTION "mr.dir"' in definitions
assert "{ECMC_AXIS_DATA_STR_MR_OFF, ECMC_AXIS_DATA_MR_OFF, 0}" in axis_source
assert "{ECMC_AXIS_DATA_STR_MR_DIRECTION, ECMC_AXIS_DATA_MR_DIRECTION, 0}" in axis_source
assert "*value = axis->getMotorRecordOffset();" in axis_source
assert "*value = static_cast<double>(axis->getMotorRecordDirection());" in axis_source

# The cyclic PLC read is a relaxed atomic load from the axis. It must never
# enter the asyn parameter library or acquire the motor-controller mutex.
assert "ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2" in axis_header
assert "motorRecordOffsetBits_.load(std::memory_order_relaxed)" in axis_source
assert "motorRecordDirection_.load(std::memory_order_relaxed)" in axis_header
read_source = axis_source[axis_source.index("int ecmcAxisBase::readDataSource("):]
read_source = read_source[:read_source.index("std::shared_ptr<ecmcDataAccess>")]
assert "getDoubleParam" not in read_source
assert "getIntegerParam" not in read_source

# Values are published only after the ordinary motor-driver write succeeds.
assert "status == asynSuccess && function == motorRecOffset_" in motor_source
assert "pAxis->setMotorRecordOffset(value);" in motor_source
assert "status == asynSuccess && function == motorRecDirection_" in motor_source
assert "pAxis->setMotorRecordDirection(value);" in motor_source

# PLC assignments cannot write either motor-record field back through the RT
# axis interface.
assert ("case ECMC_AXIS_DATA_MR_OFF:\n  case ECMC_AXIS_DATA_MR_DIRECTION:\n"
        "  case ECMC_AXIS_DATA_MR_LOADED:\n    return 0;") in plc_source
assert "axis->bindMotorRecordCoordinates(record->off, record->dir);" in motor_source
assert "{ECMC_AXIS_DATA_STR_MR_LOADED, ECMC_AXIS_DATA_MR_LOADED, 1}" in axis_source


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# Compile and execute the production offset accessors independently. This
# verifies exact bit transport, including signed zero, without any IOC/asyn
# dependency and without weakening the lock-free requirement.
fixture = r'''
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <initializer_list>
class ecmcAxisBase {
  static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "test requires lock-free cache");
  std::atomic<unsigned long long> motorRecordOffsetBits_ {0};
public:
  void setMotorRecordOffset(double value);
  double getMotorRecordOffset() const;
};
'''
fixture += method(axis_source, "void ecmcAxisBase::setMotorRecordOffset(")
fixture += method(axis_source, "double ecmcAxisBase::getMotorRecordOffset() const")
fixture += r'''
int main() {
  ecmcAxisBase axis;
  for (double value : {0.0, -0.0, 1.25, -987654.125}) {
    axis.setMotorRecordOffset(value);
    const double actual = axis.getMotorRecordOffset();
    assert(actual == value && std::signbit(actual) == std::signbit(value));
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    source_path = Path(directory) / "mr_cache.cpp"
    executable = Path(directory) / "mr_cache"
    source_path.write_text(fixture)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall",
                    "-Wextra", "-Werror", str(source_path), "-o",
                    str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("Motor-record PLC variables: write bridge, read-only mapping and lock-free RT path passed.")
