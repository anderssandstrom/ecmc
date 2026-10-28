#!/usr/bin/env python3
"""Exercise the experimental native read binding, not an IOC connection API.

Real ecmcDataItem storage and extracted PLC accessors/axis setpoint methods are
used. No ExprTk, full axis execution, motion safety or scheduling is simulated.
"""
# Expectation basis: INFERRED: proposed binding contract; COMPAT: existing accessors.
# See EXPECTATIONS.md and tools/data-binding-prototype/README.md.
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


plc = (SUP / 'plc/ecmcPLCDataIF.cpp').read_text()
axis = (SUP / 'motion/ecmcAxisBase.cpp').read_text()
fixture = r'''
#include <cassert>
#include <cstdio>
#include <limits>
#include "ecmcDataReadBinding.h"
#include "ecmcDataAccess.h"
using namespace ecmcDataBindingPrototype;
// Only the fields needed by the unmodified production methods below.
class ecmcPLCDataIF {
  double data_ = 0;
public:
  double &getDataRef();
  void setData(double data);
};
class ecmcAxisBase {
  struct { struct { double externalTrajectoryPosition = 0; } status_; } data_;
public:
  int setExtSetPos(double pos);
  double getExtSetPos();
};
'''
fixture += method(plc, 'double& ecmcPLCDataIF::getDataRef()')
fixture += method(plc, 'void ecmcPLCDataIF::setData(double data)')
fixture += method(axis, 'int ecmcAxisBase::setExtSetPos(double pos)')
fixture += method(axis, 'double ecmcAxisBase::getExtSetPos()')
checks = r'''
template <typename T> void nativeRoundTrip(T input) {
  // Deliberately unaligned storage: no typed pointer reinterpretation required.
  uint8_t storage[sizeof(T) + 1] = {};
  std::memcpy(storage + 1, &input, sizeof(input));
  ecmcDataItem item("native");
  item.setEcmcDataPointer(storage + 1, sizeof(T));
  item.setEcmcDataType(NativeType<T>::value);
  item.setAllowWriteToEcmc(false);
  ReadBinding<T> binding;
  assert(binding.bind(item));
  T output = {};
  assert(binding.read(output) && output == input);
}
int main() {
  nativeRoundTrip<uint8_t>(255);
  nativeRoundTrip<int8_t>(-128);
  nativeRoundTrip<uint16_t>(65535);
  nativeRoundTrip<int16_t>(-32768);
  nativeRoundTrip<uint32_t>(UINT32_MAX);
  nativeRoundTrip<int32_t>(INT32_MIN);
  nativeRoundTrip<uint64_t>(UINT64_MAX); // Must not round through double.
  nativeRoundTrip<int64_t>(INT64_MIN);
  nativeRoundTrip<float>(1.25f);
  nativeRoundTrip<double>(-123.5);
  // Validate metadata before cyclic access, including failed rebind cleanup.
  for (int scenario = 0; scenario < 8; ++scenario) {
    double value = 1.25, output = 99;
    ecmcDataItem item("invalid");
    item.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&value), sizeof(value));
    item.setEcmcDataType(ECMC_EC_F64);
    item.setAllowWriteToEcmc(false);
    ReadBinding<double> binding;
    assert(binding.bind(item));
    auto *info = item.getDataItemInfo();
    switch (scenario) {
      case 0: info->data = nullptr; break;
      case 1: info->dataPointerValid = 0; break;
      case 2: info->dataType = ECMC_EC_U64; break;
      case 3: info->dataSize = sizeof(value) * 2; break;
      case 4: info->dataSize = sizeof(value) - 1; break;
      case 5: info->dataElementSize = 4; break;
      case 6: info->dataBitCount = 1; break;
      case 7: info->dataDirection = ECMC_DIR_INVALID; break;
    }
    assert(!binding.bind(item));
    assert(!binding.read(output) && output == 99);
  }
  ReadBinding<double> unbound;
  double untouched = 42;
  assert(!unbound.read(untouched) && untouched == 42);

  // Registered data items expose the production neutral contract. The old
  // standalone ReadBinding above remains a separate prototype comparison.
  double scalar = 12;
  ecmcDataItem scalarItem("native.scalar");
  scalarItem.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&scalar), sizeof(scalar));
  scalarItem.setEcmcDataType(ECMC_EC_F64);
  scalarItem.setAllowWriteToEcmc(false);
  auto exported = scalarItem.getDataAccess();
  assert(exported && exported->valid());
  double observed = 0;
  assert(exported->readDouble(&observed) == 0 && observed == 12);
  ecmcDataAccess integer;
  assert(ecmcDataAccess::bind(scalarItem, nullptr, 2, &integer));
  uint64_t exact = 0;
  assert(integer.valid() && integer.readValue(&exact) == 0 && exact == 12);
  scalar = 12.5; assert(!integer.valid());
  scalar = -1; assert(!integer.valid());
  scalar = 18446744073709551616.0; assert(!integer.valid());
  scalar = std::numeric_limits<double>::infinity(); assert(!integer.valid());
  scalar = 5;
  double replacement = 7;
  scalarItem.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&replacement), sizeof(replacement));
  assert(!exported->valid());
  assert(scalarItem.getDataAccess()->readDouble(&observed) == 0 && observed == 7);
  std::shared_ptr<ecmcDataAccess> retained;
  {
    ecmcDataItem transient("transient");
    transient.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&scalar), sizeof(scalar));
    transient.setEcmcDataType(ECMC_EC_F64); transient.setAllowWriteToEcmc(false);
    retained = transient.getDataAccess();
    assert(retained && retained->valid());
  }
  assert(!retained->valid());

  // Pilot connection: expose the real PLC double reference as a data item,
  // bind once, and use the real axis external-setpoint setter. No direct write
  // into axis internals, command dispatch, execute/enable or auto-power behavior.
  ecmcPLCDataIF plc;
  ecmcAxisBase axis;
  ecmcDataItem item("global.position");
  item.setEcmcDataType(ECMC_EC_F64);
  item.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&plc.getDataRef()), sizeof(double));
  item.setEcmcDataElementSize(sizeof(double));
  item.setEcmcBitCount(64);
  item.setAllowWriteToEcmc(true); // Write permission does not forbid observation.
  bool valid = true;
  ReadBinding<double> position;
  assert(position.bind(item, &valid));
  auto transfer = [&]() {
    double sample = 0;
    if (!position.read(sample)) return false;
    return axis.setExtSetPos(sample) == 0;
  };
  plc.setData(10.5);
  assert(transfer() && axis.getExtSetPos() == 10.5);
  plc.setData(-3.25); // New value visible without rebinding.
  assert(axis.getExtSetPos() == 10.5); // Explicit sampling, no callback transfer.
  assert(transfer() && axis.getExtSetPos() == -3.25);
  valid = false;
  plc.setData(88);
  assert(!transfer() && axis.getExtSetPos() == -3.25);
  valid = true;
  assert(transfer() && axis.getExtSetPos() == 88);
  // Failed source reads leave the consumer untouched. This is not an axis
  // stop/interlock policy; a production consumer must decide how to handle loss.
  puts("Data binding prototype: native types, metadata rejection, validity and PLC-to-axis setter passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'binding.cpp', Path(directory) / 'binding'
    cpp.write_text(fixture + checks)
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-I' + str(SUP / 'com'), '-I' + str(SUP / 'main'),
        '-I' + str(ROOT / 'tools/data-binding-prototype'), str(cpp),
        str(SUP / 'com/ecmcDataItem.cpp'), str(SUP / 'com/ecmcDataAccess.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
