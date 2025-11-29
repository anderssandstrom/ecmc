#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

#include "asynDriver.h"

#define ECMC_TEST_STUBS 1

#include "devEcmcSup/com/ecmcAsynPortDriverUtils.h"

asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;

namespace {

void testStringToTypeMapping() {
  assert(getEcDataTypeFromStr(EC_DT_U32) == ECMC_EC_U32);
  assert(getEcDataTypeFromStr(EC_DT_S32_TO_U32) == ECMC_EC_S32_TO_U32);
  assert(getEcDataTypeFromStr(EC_DT_S8) == ECMC_EC_S8);
  assert(getEcDataTypeFromStr("not-a-type") == ECMC_EC_NONE);
}

void testTypeToStringRoundTrip() {
  const auto type = ECMC_EC_S16_TO_U16;
  const char *typeStr = getEcDataTypeStr(type);
  assert(std::strcmp(typeStr, EC_DT_S16_TO_U16) == 0);
}

void testBitAndByteSizes() {
  assert(getEcDataTypeBits(ECMC_EC_B4) == 4);
  assert(getEcDataTypeBits(ECMC_EC_U64) == 64);
  assert(getEcDataTypeByteSize(ECMC_EC_U16) == 2);
  assert(getEcDataTypeByteSize(ECMC_EC_F64) == 8);
}

void testLegacyLookup() {
  assert(getEcDataType(1, false) == ECMC_EC_B1);
  assert(getEcDataType(16, true) == ECMC_EC_S16);
  assert(getEcDataType(64, false) == ECMC_EC_U64);
  assert(getEcDataType(24, false) == ECMC_EC_NONE);
}

void testSignednessAndRanges() {
  assert(getEcDataTypeSigned(ECMC_EC_S16) == 1);
  assert(getEcDataTypeSigned(ECMC_EC_U16) == 0);
  assert(getEcDataTypeIsInt(ECMC_EC_S64_TO_U64) == 1);
  assert(getEcDataTypeIsFloat(ECMC_EC_F32) == 1);
  assert(getEcDataTypeIsFloat(ECMC_EC_S8) == 0);

  assert(getEcDataTypeMaxVal(ECMC_EC_S16) ==
         static_cast<uint64_t>(std::numeric_limits<int16_t>::max()));
  assert(getEcDataTypeMinVal(ECMC_EC_S16) ==
         static_cast<int64_t>(std::numeric_limits<int16_t>::min()));
  assert(getEcDataTypeMaxVal(ECMC_EC_B3) == 7);
  assert(getEcDataTypeMinVal(ECMC_EC_B3) == 0);
}

}  // namespace

int main() {
  testStringToTypeMapping();
  testTypeToStringRoundTrip();
  testBitAndByteSizes();
  testLegacyLookup();
  testSignednessAndRanges();
  std::cout << "EtherCAT utility tests passed\n";
  return 0;
}
