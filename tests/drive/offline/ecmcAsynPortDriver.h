#pragma once
#include "../../../tools/plc-test/offline/ecmcAsynPortDriver.h"
#include "ecmcDefinitions.h"
#include <limits>
#include <stdexcept>
// The drive fixture uses an S32 CSV output; other conversions are not emulated.
inline int64_t getEcDataTypeMinVal(ecmcEcDataType type) {
  if (type != ECMC_EC_S32) throw std::logic_error("drive fixture requires S32 CSV");
  return std::numeric_limits<int32_t>::min();
}
inline uint64_t getEcDataTypeMaxVal(ecmcEcDataType type) {
  if (type != ECMC_EC_S32) throw std::logic_error("drive fixture requires S32 CSV");
  return std::numeric_limits<int32_t>::max();
}
