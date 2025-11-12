#include <cassert>
#include <iostream>
#include <memory>

#define ECMC_TEST_STUBS 1

#include "devEcmcSup/motion/ecmcAxisGroup.h"
#include "devEcmcSup/motion/ecmcMasterSlaveStateMachine.h"
#include "devEcmcSup/main/ecmcErrorsList.h"

namespace {

std::unique_ptr<ecmcAxisBase> makeAxis(int id) {
  return std::make_unique<ecmcAxisBase>(id);
}

void advanceStateMachine(ecmcMasterSlaveStateMachine &sm, int iterations = 20) {
  for (int i = 0; i < iterations; ++i) {
    sm.execute();
  }
}

void testEnableRollsBackOnFailure() {
  ecmcAxisGroup group(0, "enableRollback");
  auto axis1 = makeAxis(1);
  auto axis2 = makeAxis(2);
  auto axis3 = makeAxis(3);
  group.addAxis(axis1.get());
  group.addAxis(axis2.get());
  group.addAxis(axis3.get());

  const int kErrorCode = 0xDEAD;
  axis2->setNextEnableError(kErrorCode);

  int rc = group.setEnable(true);
  assert(rc == kErrorCode && "group should propagate axis error");

  assert(axis1->getEnable() == false);
  assert(axis2->getEnable() == false);
  assert(axis3->getEnable() == false);
}

void testEnableSucceedsForAllAxes() {
  ecmcAxisGroup group(1, "enableAll");
  auto axis1 = makeAxis(10);
  auto axis2 = makeAxis(20);
  group.addAxis(axis1.get());
  group.addAxis(axis2.get());

  int rc = group.setEnable(true);
  assert(rc == 0);
  assert(axis1->getEnable() == true);
  assert(axis2->getEnable() == true);
}

void testTrajSourcePendingFlag() {
  ecmcAxisGroup group(2, "trajPending");
  auto axis1 = makeAxis(100);
  auto axis2 = makeAxis(200);
  group.addAxis(axis1.get());
  group.addAxis(axis2.get());

  axis1->setTrajBusy(true);
  axis2->setTrajBusy(true);

  int rc = group.setTrajSrc(ECMC_DATA_SOURCE_EXTERNAL);
  assert(rc == 0);
  assert(group.isTrajSrcChangeInProgress());

  axis1->setTrajBusy(false);
  axis2->setTrajBusy(false);
  assert(group.isTrajSrcChangeInProgress() == false);
}

void testTrajSourceNoPendingWhenNoChange() {
  ecmcAxisGroup group(3, "trajNoChange");
  auto axis = makeAxis(300);
  group.addAxis(axis.get());

  axis->setTrajSource(ECMC_DATA_SOURCE_EXTERNAL);
  axis->setTrajBusy(false);

  int rc = group.setTrajSrc(ECMC_DATA_SOURCE_EXTERNAL);
  assert(rc == 0);
  assert(group.isTrajSrcChangeInProgress() == false);
}

void testStateMachineWaitsForTrajSettling() {
  ecmcAsynPortDriver asynDriver;
  ecmcAxisGroup masterGrp(4, "master");
  ecmcAxisGroup slaveGrp(5, "slave");
  auto masterAxis = makeAxis(1);
  auto slaveAxis  = makeAxis(2);
  masterGrp.addAxis(masterAxis.get());
  slaveGrp.addAxis(slaveAxis.get());

  ecmcMasterSlaveStateMachine sm(&asynDriver,
                                 0,
                                 "test-ms",
                                 0.01,
                                 &masterGrp,
                                 &slaveGrp,
                                 0,
                                 0);
  assert(sm.validate() == 0);
  advanceStateMachine(sm);

  masterAxis->setEnable(true);
  masterAxis->setBusy(true);
  slaveAxis->setEnable(true);
  slaveAxis->setBusy(false);
  slaveAxis->setTrajBusy(true);
  slaveGrp.setTrajSrc(ECMC_DATA_SOURCE_EXTERNAL);

  assert(slaveGrp.isTrajSrcChangeInProgress());

  sm.execute();
  assert(sm.getState() == ECMC_MST_SLV_STATE_IDLE);

  slaveAxis->setTrajBusy(false);
  slaveGrp.isTrajSrcChangeInProgress();

  sm.execute();
  assert(sm.getState() == ECMC_MST_SLV_STATE_MASTERS);
}

}  // namespace

int main() {
  testEnableRollsBackOnFailure();
  testEnableSucceedsForAllAxes();
  testTrajSourcePendingFlag();
  testTrajSourceNoPendingWhenNoChange();
  testStateMachineWaitsForTrajSettling();
  std::cout << "All axis group tests passed\n";
  return 0;
}
