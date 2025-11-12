#include <cassert>
#include <iostream>

#define ECMC_TEST_STUBS 1

#include "devEcmcSup/motion/ecmcEncoder.h"
#include "devEcmcSup/motion/ecmcAxisData.h"

asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;

namespace {

constexpr double kSampleTime = 0.001;

ecmcAxisData makeAxisData(int axisId = 0) {
  ecmcAxisData data;
  data.status_.axisId = axisId;
  data.status_.sampleTime = kSampleTime;
  data.status_.statusWord_.enabled = 1;
  data.status_.statusWord_.enable  = 1;
  data.status_.statusWord_.trajsource = ECMC_DATA_SOURCE_INTERNAL;
  data.control_.moduloRange = 0;
  data.control_.moduloType  = ECMC_MOD_MOTION_NORMAL;
  data.control_.command     = ECMC_CMD_MOVEABS;
  data.control_.accelerationTarget = 0.0;
  data.control_.decelerationTarget = 0.0;
  return data;
}

class TestEncoder : public ecmcEncoder {
public:
  TestEncoder(ecmcAxisData *data, int idx = 0)
    : ecmcEncoder(&driver_, data, kSampleTime, idx) {}

  int64_t invokeHandleOverUnderFlow(uint64_t oldPos,
                                    uint64_t newPos,
                                    int64_t  turns,
                                    uint64_t limit,
                                    int      bits) {
    return handleOverUnderFlow(oldPos, newPos, turns, limit, bits);
  }

private:
  ecmcAsynPortDriver driver_;
};

void testHandleOverUnderFlowDoesNotResetFor64Bit() {
  auto data = makeAxisData(1);
  std::cout << "Creating encoder for 64-bit check\n";
  TestEncoder encoder(&data);
  std::cout << "Encoder created\n";
  int64_t result = encoder.invokeHandleOverUnderFlow(1000, 10, 7, 100, 64);
  assert(result == 7);
}

void testHandleOverUnderFlowDetectsOverflow() {
  auto data = makeAxisData(2);
  TestEncoder encoder(&data);
  int64_t result = encoder.invokeHandleOverUnderFlow(1000, 10, 1, 50, 32);
  assert(result == 2);
}

void testSetBitsSupportsLargeWidths() {
  auto data = makeAxisData(3);
  TestEncoder encoder(&data);
  int rc = encoder.setBits(64);
  assert(rc == 0);
  assert(encoder.getBits() == 64);
}

void testSetAbsBitsLargeRange() {
  auto data = makeAxisData(4);
  TestEncoder encoder(&data);
  assert(encoder.setBits(48) == 0);
  assert(encoder.setAbsBits(32) == 0);
  assert(encoder.getAbsBits() == 32);
}

}  // namespace

int main() {
  std::cout << "Starting encoder tests\n";
  try {
    testHandleOverUnderFlowDoesNotResetFor64Bit();
    std::cout << "handleOverUnderFlow64 ok\n";
    testHandleOverUnderFlowDetectsOverflow();
    std::cout << "handleOverUnderFlowOverflow ok\n";
    testSetBitsSupportsLargeWidths();
    std::cout << "setBits ok\n";
    testSetAbsBitsLargeRange();
    std::cout << "setAbsBits ok\n";
    std::cout << "Encoder tests passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Encoder tests failed: " << ex.what() << "\n";
  }
  return 1;
}
