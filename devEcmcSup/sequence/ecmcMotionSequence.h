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
#define ECMC_SEQ_CMD_LEN 256
#define ECMC_SEQ_SOFT_TRIGGER_COUNT 8

enum ecmcSeqAction {
  ECMC_SEQ_ACTION_NOP = 0,
  ECMC_SEQ_ACTION_MC_RESET = 1,
  ECMC_SEQ_ACTION_MC_POWER = 2,
  ECMC_SEQ_ACTION_MC_HOME = 3,
  ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE = 4,
  ECMC_SEQ_ACTION_MC_MOVE_RELATIVE = 5,
  ECMC_SEQ_ACTION_WAIT_IN_POSITION = 6,
  ECMC_SEQ_ACTION_SET_ITEM = 7,
  ECMC_SEQ_ACTION_WAIT_ITEM = 8,
  ECMC_SEQ_ACTION_WAIT_TIME = 9,
  ECMC_SEQ_ACTION_RUN_SEQUENCE = 10,
  ECMC_SEQ_ACTION_ARM_POS_TRIGGER = 11,
  ECMC_SEQ_ACTION_WAIT_TRIGGER_DONE = 12,
  ECMC_SEQ_ACTION_ARM_TIME_TRIGGER = 13,
  ECMC_SEQ_ACTION_MC_MOVE_VELOCITY = 14,
  ECMC_SEQ_ACTION_MC_HALT = 15,
  ECMC_SEQ_ACTION_EXIT_ITEM = 16,
  ECMC_SEQ_ACTION_SET_ENC_HOMED = 17,
  ECMC_SEQ_ACTION_BRANCH_ITEM = 18,
  ECMC_SEQ_ACTION_GOTO_STEP = 19
};

enum ecmcSeqCompareOp {
  ECMC_SEQ_CMP_EQ = 0,
  ECMC_SEQ_CMP_NE = 1,
  ECMC_SEQ_CMP_GT = 2,
  ECMC_SEQ_CMP_GE = 3,
  ECMC_SEQ_CMP_LT = 4,
  ECMC_SEQ_CMP_LE = 5
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
extern "C" {
#endif

int createMotionSeq(int index, int maxSteps, const char *portName);

#ifdef __cplusplus
}
#endif

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
class ecmcMotionSequence;
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
  int32_t waitForDone = 1;
  int32_t compareOp = ECMC_SEQ_CMP_EQ;
  int32_t sourceStepIndex = -1;
  int32_t branchTrueStep = -1;
  int32_t branchFalseStep = -1;
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char transition[ECMC_SEQ_TEXT_LEN] = {0};
  char onError[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  ecmcDataItem *item = nullptr;
  ecmcMotionSequence *childSeq = nullptr;
  int32_t triggerSoft = 0;
  double itemValue = 0.0;
};

struct ecmcSeqPosTrigger {
  int32_t id = -1;
  int32_t axis = -1;
  int32_t count = 0;
  int32_t fired = 0;
  int32_t active = 0;
  int32_t pulseActive = 0;
  double startPos = 0.0;
  double period = 0.0;
  double value = 0.0;
  double pulseMs = 0.0;
  double pulseElapsedMs = 0.0;
  double nextPos = 0.0;
  double lastPos = 0.0;
  double elapsedMs = 0.0;
  double nextTimeMs = 0.0;
  int32_t timeBased = 0;
  int32_t soft = 0;
  ecmcDataItem *item = nullptr;
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
  int applyCommandLine();
  int insertStep(int stepIndex);
  int deleteStep(int stepIndex);
  int requestCompile(bool waitForCompletion);
  int arm();
  int start();
  int stop();
  int reset();
  int setCurrentStep(int configuredStepIndex);
  int requestArmRTSafe();
  int requestCurrentStepRTSafe(int configuredStepIndex);
  void executeRT(double cycleTimeS);
  int report(int stepIndex);
  int readStep();
  int readStepOffset(int offset);
  int copyReadToCommandLine();

  int getIndex() const { return index_; }
  int getMaxSteps() const { return maxSteps_; }
  int getState() const { return statState_; }
  int getValid() const { return statValid_; }
  int getArmed() const { return statArmed_; }
  int getRunning() const { return statRunning_; }
  int getCompileBusy() const { return statCompileBusy_; }
  int getStepIndex() const { return statStepIndex_; }
  int getStepId() const;
  int getAction() const { return statAction_; }
  int getErrorId() const { return statErrorId_; }
  int getStepCount() const { return statStepCount_; }
  double getElapsedMs() const { return statElapsedMs_; }

private:
  friend int createMotionSeq(int index, int maxSteps, const char *portName);

  static asynStatus asynWriteApply(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteCommandLineApply(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteInsert(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteDelete(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteCompile(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteArm(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteStart(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteStop(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReset(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteRead(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReadNext(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReadPrev(void *data, size_t bytes, asynParamType type, void *userObj);
  static asynStatus asynWriteReadToCommandLine(void *data, size_t bytes, asynParamType type, void *userObj);
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
  void setCommandLineResult(const char *message);
  void refreshEditParams();
  int parseCommandLine(const char *line);
  void formatStepCommandLine(int stepIndex, const ecmcSeqStep &step, char *buffer, size_t bytes) const;
  int validateStep(int stepIndex, const ecmcSeqStep &step);
  int prepareStep(int stepIndex, ecmcSeqStep &step);
  int prepareItemStep(int stepIndex, ecmcSeqStep &step);
  int prepareBranchStep(int stepIndex, ecmcSeqStep &step);
  bool jumpToConfiguredStepRT(int configuredStepIndex);
  int runCompile();
  void compileLoop();
  int startCompileWorker();
  void stopCompileWorker();
  void advanceStepRT();
  void finishSequenceRT();
  void failStepRT(int errorId, const char *message);
  void resetStepRuntimeRT();
  bool axisInPosition(int axisIndex) const;
  bool writeItemScalarRT(ecmcSeqStep &step);
  bool readItemScalarRT(ecmcSeqStep &step, double *value);
  bool compareItemValue(double actual, const ecmcSeqStep &step) const;
  bool startFromActivePlanRT();
  void stopRT();
  int preparePosTriggerStep(int stepIndex, ecmcSeqStep &step);
  void clearTriggersRT();
  void evalTriggersRT(double cycleTimeS);
  void armPosTriggerRT(const ecmcSeqStep &step);
  bool waitTriggerDoneRT(int triggerId) const;
  void writeTriggerOutputRT(ecmcSeqPosTrigger &trig, double value, int pulseActive);
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
  int32_t cmdLineApply_ = 0;
  int32_t cmdInsert_ = 0;
  int32_t cmdDelete_ = 0;
  int32_t cmdRead_ = 0;
  int32_t cmdReadNext_ = 0;
  int32_t cmdReadPrev_ = 0;
  int32_t cmdReadToCmdLine_ = 0;
  int32_t cmdCompile_ = 0;
  int32_t cmdArm_ = 0;
  int32_t cmdStart_ = 0;
  int32_t cmdStop_ = 0;
  int32_t cmdReset_ = 0;
  char cmdLine_[ECMC_SEQ_CMD_LEN] = {0};
  char cmdLineResult_[ECMC_SEQ_TEXT_LEN] = {0};
  char readCommandLine_[ECMC_SEQ_CMD_LEN] = {0};
  int editIndexParam_ = -1;
  int editEnabledParam_ = -1;
  int editActionParam_ = -1;
  int editAxisParam_ = -1;
  int editPositionParam_ = -1;
  int editVelocityParam_ = -1;
  int editAccelerationParam_ = -1;
  int editDecelerationParam_ = -1;
  int editTimeoutMsParam_ = -1;
  int editNameParam_ = -1;
  int editTransitionParam_ = -1;
  int editOnErrorParam_ = -1;
  int editArgsParam_ = -1;
  int cmdLineParam_ = -1;
  int cmdLineResultParam_ = -1;
  int readCommandLineParam_ = -1;
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
  int32_t statTriggerId_ = -1;
  int32_t statTriggerCount_ = 0;
  int32_t statSoftTriggerId_ = -1;
  int32_t statSoftTriggerCount_ = 0;
  int32_t statSoftTriggerCounts_[ECMC_SEQ_SOFT_TRIGGER_COUNT] = {0};
  int32_t statSoftTriggerPulses_[ECMC_SEQ_SOFT_TRIGGER_COUNT] = {0};

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
  int statTriggerIdParam_ = -1;
  int statTriggerCountParam_ = -1;
  int statSoftTriggerIdParam_ = -1;
  int statSoftTriggerCountParam_ = -1;
  int statSoftTriggerCountParams_[ECMC_SEQ_SOFT_TRIGGER_COUNT] = {-1};
  int statSoftTriggerPulseParams_[ECMC_SEQ_SOFT_TRIGGER_COUNT] = {-1};

  epicsEventId compileRequestEvent_ = nullptr;
  epicsEventId compileDoneEvent_ = nullptr;
  epicsThreadId compileThread_ = nullptr;
  std::atomic<int> compileStop_ {0};
  std::atomic<int> compileRequestPending_ {0};
  std::atomic<int> compileResult_ {0};
  std::atomic<int> compileRequestGeneration_ {0};
  std::atomic<int> compileCompletedGeneration_ {0};
  std::atomic<int> requestArm_ {0};
  std::atomic<int> requestStart_ {0};
  std::atomic<int> requestStop_ {0};
  std::atomic<int> requestReset_ {0};
  std::atomic<int> requestStepSetPending_ {0};
  std::atomic<int> requestStepSetConfigured_ {-1};
  bool rtStepEntered_ = false;
  double rtStepElapsedMs_ = 0.0;
  ecmcMotionSequence *rtChildSeq_ = nullptr;
  std::vector<ecmcSeqPosTrigger> rtTriggers_;
  ecmcMcReset rtReset_;
  ecmcMcPower rtPower_;
  ecmcMcHome rtHome_;
  ecmcMcMoveAbsolute rtMoveAbsolute_;
  ecmcMcMoveRelative rtMoveRelative_;
  ecmcMcMoveVelocity rtMoveVelocity_;
  ecmcMcHalt rtHalt_;
};

extern ecmcMotionSequence *motionSeqs[ECMC_MAX_MOTION_SEQUENCES];

extern "C" {
#endif

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
int requestMotionSeqArmRTSafe(int seqIndex);
int startMotionSeq(int seqIndex);
int stopMotionSeq(int seqIndex);
int resetMotionSeq(int seqIndex);
int setMotionSeqCurrentStep(int seqIndex, int configuredStepIndex);
int requestMotionSeqCurrentStepRTSafe(int seqIndex, int configuredStepIndex);
int reportMotionSeq(int seqIndex, int stepIndex);
int getMotionSeqState(int seqIndex);
int getMotionSeqValid(int seqIndex);
int getMotionSeqArmed(int seqIndex);
int getMotionSeqRunning(int seqIndex);
int getMotionSeqCompileBusy(int seqIndex);
int getMotionSeqStepIndex(int seqIndex);
int getMotionSeqStepId(int seqIndex);
int getMotionSeqAction(int seqIndex);
int getMotionSeqErrorId(int seqIndex);
int getMotionSeqStepCount(int seqIndex);
double getMotionSeqElapsedMs(int seqIndex);
int setMotionSeqNop(int seqIndex, int stepIndex);
int setMotionSeqWaitTime(int seqIndex, int stepIndex, double waitMs);
int setMotionSeqRunSeq(int seqIndex, int stepIndex, int childSeqIndex, double timeoutMs);
int setMotionSeqArmPosTrigger(int seqIndex,
                              int stepIndex,
                              int triggerId,
                              int axis,
                              const char *item,
                              double startPos,
                              double interval,
                              double endPos,
                              double value,
                              double pulseMs);
int setMotionSeqArmTimeTrigger(int seqIndex,
                               int stepIndex,
                               int triggerId,
                               const char *item,
                               double delayMs,
                               double periodMs,
                               int count,
                               double value,
                               double pulseMs);
int setMotionSeqWaitTriggerDone(int seqIndex, int stepIndex, int triggerId, double timeoutMs);
int setMotionSeqReset(int seqIndex, int stepIndex, int axis, double timeoutMs);
int setMotionSeqResetWait(int seqIndex, int stepIndex, int axis, double timeoutMs, int waitForDone);
int setMotionSeqPower(int seqIndex, int stepIndex, int axis, int enable, double timeoutMs);
int setMotionSeqPowerWait(int seqIndex,
                          int stepIndex,
                          int axis,
                          int enable,
                          double timeoutMs,
                          int waitForDone);
int setMotionSeqHome(int seqIndex,
                     int stepIndex,
                     int axis,
                     int homeSeq,
                     double homePosition,
                     double velocityTowardsCam,
                     double velocityOffCam,
                     double acceleration,
                     double deceleration,
                     double timeoutMs);
int setMotionSeqHomeWait(int seqIndex,
                         int stepIndex,
                         int axis,
                         int homeSeq,
                         double homePosition,
                         double velocityTowardsCam,
                         double velocityOffCam,
                         double acceleration,
                         double deceleration,
                         double timeoutMs,
                         int waitForDone);
int setMotionSeqMoveAbs(int seqIndex,
                        int stepIndex,
                        int axis,
                        double position,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs);
int setMotionSeqMoveAbsWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double position,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForDone);
int setMotionSeqMoveRel(int seqIndex,
                        int stepIndex,
                        int axis,
                        double distance,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs);
int setMotionSeqMoveRelWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double distance,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForDone);
int setMotionSeqMoveVel(int seqIndex,
                        int stepIndex,
                        int axis,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs);
int setMotionSeqMoveVelWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForVelocity,
                            double tolerance);
int setMotionSeqHalt(int seqIndex, int stepIndex, int axis, double timeoutMs);
int setMotionSeqHaltWait(int seqIndex, int stepIndex, int axis, double timeoutMs, int waitForDone);
int setMotionSeqWaitInPos(int seqIndex, int stepIndex, int axis, double timeoutMs);
int setMotionSeqSetItem(int seqIndex,
                        int stepIndex,
                        const char *item,
                        double value,
                        double timeoutMs);
int setMotionSeqWaitItem(int seqIndex,
                         int stepIndex,
                         const char *item,
                         const char *op,
                         double value,
                         double timeoutMs);
int setMotionSeqExitItem(int seqIndex,
                         int stepIndex,
                         const char *item,
                         const char *op,
                         double value,
                         double timeoutMs);
int setMotionSeqSetEncHomed(int seqIndex, int stepIndex, int axis, int homed);
int setMotionSeqBranchItem(int seqIndex,
                           int stepIndex,
                           const char *item,
                           const char *op,
                           double value,
                           int trueStep,
                           int falseStep);
int setMotionSeqGotoStep(int seqIndex, int stepIndex, int targetStep);
int insertMotionSeqStep(int seqIndex, int stepIndex);
int deleteMotionSeqStep(int seqIndex, int stepIndex);

#ifdef __cplusplus
}
#endif

#endif  /* ECMC_MOTION_SEQUENCE_H_ */
