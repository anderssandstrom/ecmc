#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

#include "devEcmcSup/main/ecmcDefinitions.h"
#include "devEcmcSup/motion/ecmcTrajectoryTrapetz.h"
#include "devEcmcSup/motion/ecmcAxisData.h"

// Stubs for logging globals expected by ecmcOctetIF/ecmcError.
asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;

namespace {

constexpr double kSampleTime = 0.001;  // 1 kHz update

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

void testTrapetzAbsoluteMove() {
  auto data = makeAxisData(1);
  ecmcTrajectoryTrapetz traj(&data, kSampleTime);

  traj.setStartPos(0.0);
  traj.setTargetPos(0.25);
  traj.setTargetVel(0.05);
  traj.setAcc(0.2);
  traj.setDec(0.2);
  traj.setMotionMode(ECMC_MOVE_MODE_POS);
  traj.setEnable(true);
  int error = traj.setExecute(true);
  assert(error == 0);

  bool reached = false;
  double finalPos = 0.0;
  for (int i = 0; i < 20000; ++i) {
    data.status_.statusWord_.enabled = 1;
    finalPos = traj.getNextPosSet();
    if (std::abs(finalPos - 0.25) < 5e-3 && std::abs(traj.getNextVel()) < 1e-4) {
      reached = true;
      break;
    }
  }
  assert(reached);
}

void testTrapetzVelocityRamp() {
  auto data = makeAxisData(2);
  ecmcTrajectoryTrapetz traj(&data, kSampleTime);

  traj.setStartPos(0.0);
  traj.setTargetVel(0.1);
  traj.setAcc(0.5);
  traj.setDec(0.5);
  traj.setMotionMode(ECMC_MOVE_MODE_VEL);
  traj.setEnable(true);
  traj.setExecute(true);

  double maxObservedVel = 0.0;
  for (int i = 0; i < 1000; ++i) {
    traj.getNextPosSet();
    maxObservedVel = std::max(maxObservedVel, std::abs(traj.getNextVel()));
  }
  assert(maxObservedVel <= 0.10001);

  traj.setTargetVel(0.0);
  bool stopped = false;
  for (int i = 0; i < 20000; ++i) {
    data.status_.statusWord_.enabled = 1;
    traj.getNextPosSet();
    if (std::abs(traj.getNextVel()) < 1e-4) {
      stopped = true;
      break;
    }
  }
  assert(stopped);
}

}  // namespace

int main() {
  testTrapetzAbsoluteMove();
  testTrapetzVelocityRamp();
  std::cout << "Trajectory tests passed\n";
  return 0;
}
