// Shared production LUT PLC function, independent of shared memory and IOC state.
#ifndef ECMC_PLC_LIB_LUT_INC_
#define ECMC_PLC_LIB_LUT_INC_
#include <cmath>
extern ecmcLookupTable<double, double> *luts[ECMC_MAX_LUTS];
static int lut_errorCode = 0;
inline double lut_get_value(double lutObjIndex, double index) {
  lut_errorCode = 0;
  // Validate the double before conversion and before indexing the pointer array.
  if (!std::isfinite(lutObjIndex) || lutObjIndex < 0 ||
      lutObjIndex >= ECMC_MAX_LUTS || std::trunc(lutObjIndex) != lutObjIndex) {
    lut_errorCode = ERROR_LUT_INDEX_OUT_OF_RANGE;
    ecmcRtLoggerLogError("LUT index out of range\n");
    return 0;
  }
  int objIndex = static_cast<int>(lutObjIndex);
  if (!luts[objIndex]) {
    lut_errorCode = ERROR_LUT_INDEX_OUT_OF_RANGE;
    ecmcRtLoggerLogError("LUT object is NULL\n");
    return 0;
  }
  return luts[objIndex]->getValue(index);
}
#endif
