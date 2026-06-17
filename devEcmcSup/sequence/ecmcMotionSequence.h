/*************************************************************************\
* Copyright (c) 2026 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMotionSequence.h
*
\*************************************************************************/

#ifndef ECMC_MOTION_SEQUENCE_H_
#define ECMC_MOTION_SEQUENCE_H_

#include <stdint.h>

#define ECMC_MAX_MOTION_SEQUENCES 16
#define ECMC_SEQ_TEXT_LEN 128

enum ecmcSeqAction {
  ECMC_SEQ_ACTION_NOP = 0,
  ECMC_SEQ_ACTION_MC_RESET = 1,
  ECMC_SEQ_ACTION_MC_POWER = 2,
  ECMC_SEQ_ACTION_MC_HOME = 3,
  ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE = 4,
  ECMC_SEQ_ACTION_MC_MOVE_RELATIVE = 5,
  ECMC_SEQ_ACTION_WAIT_IN_POSITION = 6,
  ECMC_SEQ_ACTION_SET_ITEM = 7,
  ECMC_SEQ_ACTION_WAIT_ITEM = 8
};

enum ecmcSeqState {
  ECMC_SEQ_STATE_IDLE = 0,
  ECMC_SEQ_STATE_ARMED = 1,
  ECMC_SEQ_STATE_RUNNING = 2,
  ECMC_SEQ_STATE_DONE = 3,
  ECMC_SEQ_STATE_ERROR = 4,
  ECMC_SEQ_STATE_STOPPED = 5
};

#ifdef __cplusplus

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "epicsEvent.h"
#include "epicsThread.h"
#include "asynPortDriver.h"
#include "ecmcDefinitions.h"
#include "ecmcMcRuntime.h"

class ecmcAsynPortDriver;
class ecmcMotionSequencePort;
class ecmcDataItem;

struct ecmcSeqStep {
  int32_t enabled = 0;
  int32_t action = ECMC_SEQ_ACTION_NOP;
  int32_t axis = -1;
  double position = 0.0;
  double velocity = 0.0;
  double acceleration = 0.0;
  double deceleration = 0.0;
  double timeoutMs = 0.0;
  int32_t cmdData = 0;
  int32_t enable = 1;
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char transition[ECMC_SEQ_TEXT_LEN] = {0};
  char onError[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  ecmcDataItem *item = nullptr;
  double itemValue = 0.0;
};

class ecmcMotionSequence {
public:
  ecmcMotionSequence(int index, int maxSteps, const char *portName);
  ~ecmcMotionSequence();

  int createAsynParams(ecmcMotionSequencePort *port);
  int setStep(int stepIndex,
              int enabled,
              int action,
              int axis,
              double position,
              double velocity,
              double acceleration,
              double deceleration,
              double timeoutMs);
  int setStepText(int stepIndex,
                  const char *name,
                  const char *transition,
                  const char *onError,
                  const char *args);
  int applyEditStep();
  int requestCompile(bool waitForCompletion);
  int arm();
  int start();
  int stop();
  int reset();
  void executeRT(double cycleTimeS);
  int report(int stepIndex);
  int readStep();
  int readStepOffset(int offset);

  int getIndex() const { return index_; }
  int getMaxSteps() const { return maxSteps_; }

private:
  friend int createMotionSeq(int index, int maxSteps, const char *portName);

  static asynStatus asynWriteApply(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteCompile(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteArm(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteStart(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteStop(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReset(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteRead(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReadNext(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReadPrev(void *data, size_t bytes, asynParamType type, void *userObj);
  static void compileThreadEntry(void *userObj);

  int addIntParam(ecmcMotionSequencePort *port,
                  const char *suffix,
                  int32_t *value,
                  bool writable,
                  int *paramOut = nullptr);
  int addDoubleParam(ecmcMotionSequencePort *port,
                     const char *suffix,
                     double *value,
                     bool writable,
                     int *paramOut = nullptr);
  int addStringParam(ecmcMotionSequencePort *port,
                     const char *suffix,
                     char *value,
                     size_t bytes,
                     bool writable,
                     int *paramOut = nullptr);
  std::string paramName(const char *suffix) const;
  int setError(int errorId, const char *message);
  void setValidationText(const char *message);
  int validateStep(int stepIndex, const ecmcSeqStep &step);
  int prepareStep(int stepIndex, ecmcSeqStep &step);
  int prepareItemStep(int stepIndex, ecmcSeqStep &step);
  int runCompile();
  void compileLoop();
  int startCompileWorker();
  void stopCompileWorker();
  void advanceStepRT();
  void failStepRT(int errorId, const char *message);
  void resetStepRuntimeRT();
  bool axisInPosition(int axisIndex) const;
  bool writeItemScalarRT(ecmcSeqStep &step);
  bool readItemScalarRT(ecmcSeqStep &step, double *value);
  void refreshStatus();

  int index_ = -1;
  int maxSteps_ = 0;
  char portName_[ECMC_SEQ_TEXT_LEN] = {0};
  ecmcMotionSequencePort *seqAsynPort_ = nullptr;
  std::vector<ecmcSeqStep> steps_;
  std::mutex stepsMutex_;
  std::vector<ecmcSeqStep> compiledPlan_;
  std::vector<ecmcSeqStep> activePlan_;
  std::mutex planMutex_;

  ecmcSeqStep edit_;
  ecmcSeqStep read_;
  int32_t editIndex_ = 0;
  int32_t readIndex_ = 0;
  int32_t cmdApply_ = 0;
  int32_t cmdRead_ = 0;
  int32_t cmdReadNext_ = 0;
  int32_t cmdReadPrev_ = 0;
  int32_t cmdCompile_ = 0;
  int32_t cmdArm_ = 0;
  int32_t cmdStart_ = 0;
  int32_t cmdStop_ = 0;
  int32_t cmdReset_ = 0;
  int readIndexParam_ = -1;
  int readEnabledParam_ = -1;
  int readActionParam_ = -1;
  int readAxisParam_ = -1;
  int readPositionParam_ = -1;
  int readVelocityParam_ = -1;
  int readAccelerationParam_ = -1;
  int readDecelerationParam_ = -1;
  int readTimeoutMsParam_ = -1;
  int readNameParam_ = -1;
  int readTransitionParam_ = -1;
  int readOnErrorParam_ = -1;
  int readArgsParam_ = -1;

  int32_t statState_ = ECMC_SEQ_STATE_IDLE;
  int32_t statValid_ = 0;
  int32_t statArmed_ = 0;
  int32_t statRunning_ = 0;
  int32_t statCompileBusy_ = 0;
  int32_t statStepIndex_ = -1;
  int32_t statAction_ = ECMC_SEQ_ACTION_NOP;
  int32_t statErrorId_ = 0;
  int32_t statStepCount_ = 0;
  double statElapsedMs_ = 0.0;
  char statStepName_[ECMC_SEQ_TEXT_LEN] = {0};
  char statErrorText_[ECMC_SEQ_TEXT_LEN] = {0};
  char statValidationText_[ECMC_SEQ_TEXT_LEN] = {0};

  int statStateParam_ = -1;
  int statValidParam_ = -1;
  int statArmedParam_ = -1;
  int statRunningParam_ = -1;
  int statCompileBusyParam_ = -1;
  int statStepIndexParam_ = -1;
  int statActionParam_ = -1;
  int statErrorIdParam_ = -1;
  int statStepCountParam_ = -1;
  int statElapsedMsParam_ = -1;
  int statStepNameParam_ = -1;
  int statErrorTextParam_ = -1;
  int statValidationTextParam_ = -1;

  epicsEventId compileRequestEvent_ = nullptr;
  epicsEventId compileDoneEvent_ = nullptr;
  epicsThreadId compileThread_ = nullptr;
  std::atomic<int> compileStop_ {0};
  std::atomic<int> compileRequestPending_ {0};
  std::atomic<int> compileResult_ {0};
  std::atomic<int> compileRequestGeneration_ {0};
  std::atomic<int> compileCompletedGeneration_ {0};
  std::atomic<int> requestStart_ {0};
  std::atomic<int> requestStop_ {0};
  std::atomic<int> requestReset_ {0};
  bool rtStepEntered_ = false;
  double rtStepElapsedMs_ = 0.0;
  ecmcMcReset rtReset_;
  ecmcMcPower rtPower_;
  ecmcMcHome rtHome_;
  ecmcMcMoveAbsolute rtMoveAbsolute_;
  ecmcMcMoveRelative rtMoveRelative_;
};

extern "C" {
#endif

int createMotionSeq(int index, int maxSteps, const char *portName);
int setMotionSeqStep(int seqIndex,
                     int stepIndex,
                     int enabled,
                     int action,
                     int axis,
                     double position,
                     double velocity,
                     double acceleration,
                     double deceleration,
                     double timeoutMs);
int setMotionSeqStepText(int seqIndex,
                         int stepIndex,
                         const char *name,
                         const char *transition,
                         const char *onError,
                         const char *args);
int compileMotionSeq(int seqIndex);
int armMotionSeq(int seqIndex);
int startMotionSeq(int seqIndex);
int stopMotionSeq(int seqIndex);
int resetMotionSeq(int seqIndex);
int reportMotionSeq(int seqIndex, int stepIndex);

#ifdef __cplusplus
}
#endif

#endif  /* ECMC_MOTION_SEQUENCE_H_ */
