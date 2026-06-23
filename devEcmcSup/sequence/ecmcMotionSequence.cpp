/*************************************************************************\
* Copyright (c) 2026 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMotionSequence.cpp
*
\*************************************************************************/

#include "ecmcMotionSequence.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <stdio.h>
#include <string.h>

#include "ecmcAsynPortDriver.h"
#include "ecmcAxisBase.h"
#include "ecmcAxisData.h"
#include "ecmcDataItem.h"
#include "ecmcErrorsList.h"
#include "ecmcGlobalsExtern.h"
#include "ecmcOctetIF.h"

namespace {
constexpr int kDefaultMaxSteps = 32;
constexpr int kHardMaxSteps = 256;

enum class SeqParamKind {
  Int32,
  Float64,
  String
};

struct SeqParamBinding {
  int reason = -1;
  SeqParamKind kind = SeqParamKind::Int32;
  void *data = nullptr;
  size_t size = 0;
  bool writable = false;
  asynStatus (*callback)(void *, size_t, asynParamType, void *) = nullptr;
  void *callbackUser = nullptr;
};

void copyText(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dstSize, "%s", src);
}

bool actionNeedsAxis(int action) {
  switch (action) {
  case ECMC_SEQ_ACTION_MC_RESET:
  case ECMC_SEQ_ACTION_MC_POWER:
  case ECMC_SEQ_ACTION_MC_HOME:
  case ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE:
  case ECMC_SEQ_ACTION_MC_MOVE_RELATIVE:
  case ECMC_SEQ_ACTION_MC_MOVE_VELOCITY:
  case ECMC_SEQ_ACTION_MC_HALT:
  case ECMC_SEQ_ACTION_WAIT_IN_POSITION:
  case ECMC_SEQ_ACTION_ARM_POS_TRIGGER:
    return true;
  default:
    return false;
  }
}

bool validAction(int action) {
  return action >= ECMC_SEQ_ACTION_NOP && action <= ECMC_SEQ_ACTION_MC_HALT;
}

std::string getArgValue(const char *args, const char *key) {
  if (!args || !key) {
    return "";
  }
  const std::string text(args);
  const std::string wanted = std::string(key) + "=";
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t end = text.find(';', pos);
    const std::string token = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (token.rfind(wanted, 0) == 0) {
      return token.substr(wanted.size());
    }
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  return "";
}

bool isScalarNumericType(ecmcEcDataType type) {
  switch (type) {
  case ECMC_EC_U8:
  case ECMC_EC_S8:
  case ECMC_EC_U16:
  case ECMC_EC_S16:
  case ECMC_EC_U32:
  case ECMC_EC_S32:
  case ECMC_EC_U64:
  case ECMC_EC_S64:
  case ECMC_EC_F32:
  case ECMC_EC_F64:
    return true;
  default:
    return false;
  }
}

bool parseCompareOp(const std::string &text, int32_t *op) {
  if (!op) {
    return false;
  }
  if (text.empty() || text == "==" || text == "eq") {
    *op = ECMC_SEQ_CMP_EQ;
  } else if (text == "!=" || text == "ne") {
    *op = ECMC_SEQ_CMP_NE;
  } else if (text == ">") {
    *op = ECMC_SEQ_CMP_GT;
  } else if (text == ">=") {
    *op = ECMC_SEQ_CMP_GE;
  } else if (text == "<") {
    *op = ECMC_SEQ_CMP_LT;
  } else if (text == "<=") {
    *op = ECMC_SEQ_CMP_LE;
  } else {
    return false;
  }
  return true;
}

size_t scalarTypeSize(ecmcEcDataType type) {
  switch (type) {
  case ECMC_EC_U8:
  case ECMC_EC_S8:
    return 1;
  case ECMC_EC_U16:
  case ECMC_EC_S16:
    return 2;
  case ECMC_EC_U32:
  case ECMC_EC_S32:
  case ECMC_EC_F32:
    return 4;
  case ECMC_EC_U64:
  case ECMC_EC_S64:
  case ECMC_EC_F64:
    return 8;
  default:
    return 0;
  }
}

}  // namespace

class ecmcMotionSequencePort : public asynPortDriver {
public:
  explicit ecmcMotionSequencePort(const char *portName, int maxParams)
    : asynPortDriver(portName,
                     1,
                     asynInt32Mask | asynFloat64Mask | asynInt8ArrayMask | asynDrvUserMask,
                     asynInt32Mask | asynFloat64Mask | asynInt8ArrayMask | asynDrvUserMask,
                     ASYN_CANBLOCK,
                     1,
                     0,
                     0) {
    bindings_.reserve(maxParams > 0 ? maxParams : 1);
  }

  int addIntParam(const char *name, int32_t *value, bool writable, int *paramOut) {
    int reason = -1;
    if (createParam(name, asynParamInt32, &reason) != asynSuccess) {
      return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    }
    bindings_.push_back({reason, SeqParamKind::Int32, value, sizeof(*value), writable, nullptr, nullptr});
    if (paramOut) *paramOut = reason;
    refreshParam(reason);
    return 0;
  }

  int addDoubleParam(const char *name, double *value, bool writable, int *paramOut) {
    int reason = -1;
    if (createParam(name, asynParamFloat64, &reason) != asynSuccess) {
      return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    }
    bindings_.push_back({reason, SeqParamKind::Float64, value, sizeof(*value), writable, nullptr, nullptr});
    if (paramOut) *paramOut = reason;
    refreshParam(reason);
    return 0;
  }

  int addStringParam(const char *name, char *value, size_t bytes, bool writable, int *paramOut) {
    int reason = -1;
    if (createParam(name, asynParamInt8Array, &reason) != asynSuccess) {
      return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    }
    bindings_.push_back({reason, SeqParamKind::String, value, bytes, writable, nullptr, nullptr});
    if (paramOut) *paramOut = reason;
    refreshParam(reason);
    return 0;
  }

  int setWriteCallback(int reason,
                       asynStatus (*callback)(void *, size_t, asynParamType, void *),
                       void *user) {
    auto *binding = bindingForReason(reason);
    if (!binding) return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    binding->callback = callback;
    binding->callbackUser = user;
    return 0;
  }

  void refreshParam(int reason) {
    auto *binding = bindingForReason(reason);
    if (!binding || !binding->data) return;
    switch (binding->kind) {
    case SeqParamKind::Int32:
      setIntegerParam(reason, *static_cast<int32_t *>(binding->data));
      break;
    case SeqParamKind::Float64:
      setDoubleParam(reason, *static_cast<double *>(binding->data));
      break;
    case SeqParamKind::String:
      doCallbacksInt8Array(static_cast<epicsInt8 *>(binding->data),
                           strlen(static_cast<char *>(binding->data)),
                           reason,
                           0);
      break;
    }
    callParamCallbacks();
  }

  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !binding->writable || binding->kind != SeqParamKind::Int32) return asynError;
    *static_cast<int32_t *>(binding->data) = value;
    refreshParam(binding->reason);
    return executeCallback(binding, &value, sizeof(value), asynParamInt32);
  }

  asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !value || binding->kind != SeqParamKind::Int32) return asynError;
    *value = *static_cast<int32_t *>(binding->data);
    return asynSuccess;
  }

  asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !binding->writable || binding->kind != SeqParamKind::Float64) return asynError;
    *static_cast<double *>(binding->data) = value;
    refreshParam(binding->reason);
    return executeCallback(binding, &value, sizeof(value), asynParamFloat64);
  }

  asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !value || binding->kind != SeqParamKind::Float64) return asynError;
    *value = *static_cast<double *>(binding->data);
    return asynSuccess;
  }

  asynStatus writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !binding->writable || binding->kind != SeqParamKind::String || !binding->data) return asynError;
    const size_t copyBytes = std::min(nElements, binding->size > 0 ? binding->size - 1 : 0);
    memcpy(binding->data, value, copyBytes);
    static_cast<char *>(binding->data)[copyBytes] = '\0';
    refreshParam(binding->reason);
    return executeCallback(binding, value, nElements, asynParamInt8Array);
  }

  asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn) override {
    auto *binding = bindingForReason(pasynUser ? pasynUser->reason : -1);
    if (!binding || !value || binding->kind != SeqParamKind::String || !binding->data) return asynError;
    const size_t bytes = std::min(strlen(static_cast<char *>(binding->data)), nElements);
    memcpy(value, binding->data, bytes);
    if (nIn) *nIn = bytes;
    return asynSuccess;
  }

private:
  SeqParamBinding *bindingForReason(int reason) {
    for (auto &binding : bindings_) {
      if (binding.reason == reason) return &binding;
    }
    return nullptr;
  }

  asynStatus executeCallback(SeqParamBinding *binding, void *data, size_t bytes, asynParamType type) {
    if (!binding || !binding->callback) return asynSuccess;
    return binding->callback(data, bytes, type, binding->callbackUser);
  }

  std::vector<SeqParamBinding> bindings_;
};

ecmcMotionSequence *motionSeqs[ECMC_MAX_MOTION_SEQUENCES] = {nullptr};

ecmcMotionSequence::ecmcMotionSequence(int index, int maxSteps, const char *portName)
  : index_(index), maxSteps_(maxSteps), steps_(maxSteps) {
  rtTriggers_.reserve(maxSteps > 0 ? maxSteps : 1);
  copyText(portName_, sizeof(portName_), portName);
  copyText(edit_.name, sizeof(edit_.name), "Step");
  copyText(edit_.transition, sizeof(edit_.transition), "Done");
  copyText(edit_.onError, sizeof(edit_.onError), "Abort");
  copyText(read_.name, sizeof(read_.name), "");
  copyText(read_.transition, sizeof(read_.transition), "");
  copyText(read_.onError, sizeof(read_.onError), "");
  copyText(statValidationText_, sizeof(statValidationText_), "Not compiled.");
}

ecmcMotionSequence::~ecmcMotionSequence() {
  stopCompileWorker();
  delete seqAsynPort_;
  seqAsynPort_ = nullptr;
}

std::string ecmcMotionSequence::paramName(const char *suffix) const {
  return std::string(suffix ? suffix : "");
}

int ecmcMotionSequence::addIntParam(ecmcMotionSequencePort *port,
                                    const char *suffix,
                                    int32_t *value,
                                    bool writable,
                                    int *paramOut) {
  const std::string name = paramName(suffix);
  return port->addIntParam(name.c_str(), value, writable, paramOut);
}

int ecmcMotionSequence::addDoubleParam(ecmcMotionSequencePort *port,
                                       const char *suffix,
                                       double *value,
                                       bool writable,
                                       int *paramOut) {
  const std::string name = paramName(suffix);
  return port->addDoubleParam(name.c_str(), value, writable, paramOut);
}

int ecmcMotionSequence::addStringParam(ecmcMotionSequencePort *port,
                                       const char *suffix,
                                       char *value,
                                       size_t bytes,
                                       bool writable,
                                       int *paramOut) {
  const std::string name = paramName(suffix);
  return port->addStringParam(name.c_str(), value, bytes, writable, paramOut);
}

int ecmcMotionSequence::createAsynParams(ecmcMotionSequencePort *port) {
  if (!port) {
    return ERROR_MAIN_ASYN_PORT_DRIVER_NULL;
  }

  int error = 0;
#define ADD_PARAM(call) do { error = (call); if (error) return error; } while (0)

  ADD_PARAM(addIntParam(port, "edit.index", &editIndex_, true));
  ADD_PARAM(addIntParam(port, "edit.enabled", &edit_.enabled, true));
  ADD_PARAM(addIntParam(port, "edit.action", &edit_.action, true));
  ADD_PARAM(addIntParam(port, "edit.axis", &edit_.axis, true));
  ADD_PARAM(addDoubleParam(port, "edit.position", &edit_.position, true));
  ADD_PARAM(addDoubleParam(port, "edit.velocity", &edit_.velocity, true));
  ADD_PARAM(addDoubleParam(port, "edit.acceleration", &edit_.acceleration, true));
  ADD_PARAM(addDoubleParam(port, "edit.deceleration", &edit_.deceleration, true));
  ADD_PARAM(addDoubleParam(port, "edit.timeout_ms", &edit_.timeoutMs, true));
  ADD_PARAM(addStringParam(port, "edit.name", edit_.name, sizeof(edit_.name), true));
  ADD_PARAM(addStringParam(port, "edit.transition", edit_.transition, sizeof(edit_.transition), true));
  ADD_PARAM(addStringParam(port, "edit.onerror", edit_.onError, sizeof(edit_.onError), true));
  ADD_PARAM(addStringParam(port, "edit.args", edit_.args, sizeof(edit_.args), true));

  ADD_PARAM(addIntParam(port, "read.index", &readIndex_, true, &readIndexParam_));
  ADD_PARAM(addIntParam(port, "read.enabled", &read_.enabled, false, &readEnabledParam_));
  ADD_PARAM(addIntParam(port, "read.action", &read_.action, false, &readActionParam_));
  ADD_PARAM(addIntParam(port, "read.axis", &read_.axis, false, &readAxisParam_));
  ADD_PARAM(addDoubleParam(port, "read.position", &read_.position, false, &readPositionParam_));
  ADD_PARAM(addDoubleParam(port, "read.velocity", &read_.velocity, false, &readVelocityParam_));
  ADD_PARAM(addDoubleParam(port, "read.acceleration", &read_.acceleration, false, &readAccelerationParam_));
  ADD_PARAM(addDoubleParam(port, "read.deceleration", &read_.deceleration, false, &readDecelerationParam_));
  ADD_PARAM(addDoubleParam(port, "read.timeout_ms", &read_.timeoutMs, false, &readTimeoutMsParam_));
  ADD_PARAM(addStringParam(port, "read.name", read_.name, sizeof(read_.name), false, &readNameParam_));
  ADD_PARAM(addStringParam(port, "read.transition", read_.transition, sizeof(read_.transition), false, &readTransitionParam_));
  ADD_PARAM(addStringParam(port, "read.onerror", read_.onError, sizeof(read_.onError), false, &readOnErrorParam_));
  ADD_PARAM(addStringParam(port, "read.args", read_.args, sizeof(read_.args), false, &readArgsParam_));

  int cmdParam = -1;
  ADD_PARAM(addIntParam(port, "cmd.apply", &cmdApply_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteApply, this);
  ADD_PARAM(addIntParam(port, "cmd.read", &cmdRead_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteRead, this);
  ADD_PARAM(addIntParam(port, "cmd.read_next", &cmdReadNext_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteReadNext, this);
  ADD_PARAM(addIntParam(port, "cmd.read_prev", &cmdReadPrev_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteReadPrev, this);
  ADD_PARAM(addIntParam(port, "cmd.compile", &cmdCompile_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteCompile, this);
  ADD_PARAM(addIntParam(port, "cmd.arm", &cmdArm_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteArm, this);
  ADD_PARAM(addIntParam(port, "cmd.start", &cmdStart_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteStart, this);
  ADD_PARAM(addIntParam(port, "cmd.stop", &cmdStop_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteStop, this);
  ADD_PARAM(addIntParam(port, "cmd.reset", &cmdReset_, true, &cmdParam));
  port->setWriteCallback(cmdParam, asynWriteReset, this);

  ADD_PARAM(addIntParam(port, "stat.state", &statState_, false, &statStateParam_));
  ADD_PARAM(addIntParam(port, "stat.valid", &statValid_, false, &statValidParam_));
  ADD_PARAM(addIntParam(port, "stat.armed", &statArmed_, false, &statArmedParam_));
  ADD_PARAM(addIntParam(port, "stat.running", &statRunning_, false, &statRunningParam_));
  ADD_PARAM(addIntParam(port, "stat.compile_busy", &statCompileBusy_, false, &statCompileBusyParam_));
  ADD_PARAM(addIntParam(port, "stat.step_index", &statStepIndex_, false, &statStepIndexParam_));
  ADD_PARAM(addIntParam(port, "stat.action", &statAction_, false, &statActionParam_));
  ADD_PARAM(addIntParam(port, "stat.error_id", &statErrorId_, false, &statErrorIdParam_));
  ADD_PARAM(addIntParam(port, "stat.step_count", &statStepCount_, false, &statStepCountParam_));
  ADD_PARAM(addDoubleParam(port, "stat.elapsed_ms", &statElapsedMs_, false, &statElapsedMsParam_));
  ADD_PARAM(addStringParam(port, "stat.step_name", statStepName_, sizeof(statStepName_), false, &statStepNameParam_));
  ADD_PARAM(addStringParam(port, "stat.error_text", statErrorText_, sizeof(statErrorText_), false, &statErrorTextParam_));
  ADD_PARAM(addStringParam(port, "stat.validation_text", statValidationText_, sizeof(statValidationText_), false, &statValidationTextParam_));
  ADD_PARAM(addIntParam(port, "stat.soft_trigger_id", &statSoftTriggerId_, false, &statSoftTriggerIdParam_));
  ADD_PARAM(addIntParam(port, "stat.soft_trigger_count", &statSoftTriggerCount_, false, &statSoftTriggerCountParam_));
  ADD_PARAM(startCompileWorker());

#undef ADD_PARAM
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::setError(int errorId, const char *message) {
  statErrorId_ = errorId;
  statState_ = ECMC_SEQ_STATE_ERROR;
  statRunning_ = 0;
  statArmed_ = 0;
  copyText(statErrorText_, sizeof(statErrorText_), message);
  refreshStatus();
  return errorId;
}

void ecmcMotionSequence::setValidationText(const char *message) {
  copyText(statValidationText_, sizeof(statValidationText_), message);
  if (seqAsynPort_ && statValidationTextParam_ >= 0) {
    seqAsynPort_->refreshParam(statValidationTextParam_);
  }
}

int ecmcMotionSequence::validateStep(int stepIndex, const ecmcSeqStep &step) {
  if (!validAction(step.action)) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d has invalid action %d.", stepIndex, step.action);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (actionNeedsAxis(step.action)) {
    if (step.axis < 0 || step.axis >= ECMC_MAX_AXES) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d axis index out of range.", stepIndex);
      return setError(ERROR_MAIN_AXIS_INDEX_OUT_OF_RANGE, msg);
    }
    if (!axes[step.axis]) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d axis %d does not exist.", stepIndex, step.axis);
      return setError(ERROR_MAIN_AXIS_OBJECT_NULL, msg);
    }
  }
  if (step.timeoutMs < 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d timeout is negative.", stepIndex);
    return setError(ERROR_MAIN_SAMPLE_RATE_OUT_OF_RANGE, msg);
  }
  return 0;
}

int ecmcMotionSequence::prepareStep(int stepIndex, ecmcSeqStep &step) {
  const int error = validateStep(stepIndex, step);
  if (error) {
    return error;
  }

  switch (step.action) {
  case ECMC_SEQ_ACTION_MC_MOVE_VELOCITY:
    step.waitForDone = 0;
    break;
  case ECMC_SEQ_ACTION_MC_RESET:
  case ECMC_SEQ_ACTION_MC_POWER:
  case ECMC_SEQ_ACTION_MC_HOME:
  case ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE:
  case ECMC_SEQ_ACTION_MC_MOVE_RELATIVE:
  case ECMC_SEQ_ACTION_MC_HALT:
    step.waitForDone = 1;
    break;
  default:
    break;
  }
  const std::string waitText = getArgValue(step.args, "wait");
  if (!waitText.empty()) {
    step.waitForDone = atoi(waitText.c_str()) ? 1 : 0;
  }

  if (step.action == ECMC_SEQ_ACTION_SET_ITEM ||
      step.action == ECMC_SEQ_ACTION_WAIT_ITEM) {
    return prepareItemStep(stepIndex, step);
  }
  if (step.action == ECMC_SEQ_ACTION_ARM_POS_TRIGGER ||
      step.action == ECMC_SEQ_ACTION_ARM_TIME_TRIGGER) {
    return preparePosTriggerStep(stepIndex, step);
  }
  if (step.action == ECMC_SEQ_ACTION_WAIT_TRIGGER_DONE) {
    const std::string idText = getArgValue(step.args, "id");
    if (idText.empty()) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d missing trigger id arg.", stepIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
    char *end = nullptr;
    const long triggerId = strtol(idText.c_str(), &end, 10);
    if (end == idText.c_str() || triggerId < 0) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d trigger id parse failed.", stepIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
    step.cmdData = static_cast<int32_t>(triggerId);
  }
  if (step.action == ECMC_SEQ_ACTION_RUN_SEQUENCE) {
    const int childIndex = step.axis;
    if (childIndex < 0 || childIndex >= ECMC_MAX_MOTION_SEQUENCES) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d child sequence out of range.", stepIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
    if (childIndex == index_) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d cannot run its own sequence.", stepIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
    if (!motionSeqs[childIndex]) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d child sequence %d does not exist.", stepIndex, childIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
    step.childSeq = motionSeqs[childIndex];
  }
  if (step.action == ECMC_SEQ_ACTION_MC_HOME) {
    const std::string seqText = getArgValue(step.args, "seq");
    if (!seqText.empty()) {
      step.cmdData = atoi(seqText.c_str());
    }
    const std::string decText = getArgValue(step.args, "dec");
    if (!decText.empty()) {
      step.itemValue = strtod(decText.c_str(), nullptr);
    } else {
      step.itemValue = step.deceleration;
    }
  }
  if (step.action == ECMC_SEQ_ACTION_MC_POWER) {
    const std::string enableText = getArgValue(step.args, "enable");
    if (!enableText.empty()) {
      step.enable = atoi(enableText.c_str()) ? 1 : 0;
    }
  }
  if (step.action == ECMC_SEQ_ACTION_MC_MOVE_VELOCITY) {
    const std::string toleranceText = getArgValue(step.args, "tol");
    step.itemValue = toleranceText.empty() ? 0.0 : strtod(toleranceText.c_str(), nullptr);
    if (step.itemValue < 0.0) {
      char msg[ECMC_SEQ_TEXT_LEN] = {0};
      snprintf(msg, sizeof(msg), "Step %d velocity tolerance is negative.", stepIndex);
      return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
    }
  }
  return 0;
}

int ecmcMotionSequence::preparePosTriggerStep(int stepIndex, ecmcSeqStep &step) {
  const std::string idText = getArgValue(step.args, "id");
  const std::string itemName = getArgValue(step.args, "item");
  const std::string valueText = getArgValue(step.args, "value");
  const bool softTrigger = itemName == "soft";
  if (idText.empty() || itemName.empty() || valueText.empty()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d missing trigger id/item/value args.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.velocity == 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger period is zero.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.action == ECMC_SEQ_ACTION_ARM_TIME_TRIGGER &&
      step.velocity < 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger period is negative.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.action == ECMC_SEQ_ACTION_ARM_TIME_TRIGGER &&
      step.position < 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger delay is negative.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.acceleration <= 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger count is invalid.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.deceleration < 0.0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger pulse time is negative.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (!softTrigger && !asynPort) {
    return setError(ERROR_MAIN_ASYN_PORT_DRIVER_NULL, "Asyn port unavailable.");
  }

  char *end = nullptr;
  const long triggerId = strtol(idText.c_str(), &end, 10);
  if (end == idText.c_str() || triggerId < 0) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger id parse failed.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }

  end = nullptr;
  const double value = strtod(valueText.c_str(), &end);
  if (end == valueText.c_str()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger value parse failed.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }

  step.cmdData = static_cast<int32_t>(triggerId);
  step.triggerSoft = softTrigger ? 1 : 0;
  step.itemValue = value;

  if (softTrigger) {
    step.item = nullptr;
    return 0;
  }

  ecmcDataItem *item = asynPort->findAvailDataItem(itemName.c_str());
  if (!item || !item->getDataItemInfo() || !item->getDataItemInfo()->dataPointerValid) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger item not found.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (!isScalarNumericType(item->getEcmcDataType()) ||
      item->getEcmcDataSize() < scalarTypeSize(item->getEcmcDataType())) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger item type unsupported.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (!item->getAllowWriteToEcmc()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d trigger item is not writable.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }

  step.item = item;
  return 0;
}

int ecmcMotionSequence::prepareItemStep(int stepIndex, ecmcSeqStep &step) {
  const std::string itemName = getArgValue(step.args, "item");
  const std::string valueText = getArgValue(step.args, "value");
  const std::string opText = getArgValue(step.args, "op");
  if (itemName.empty() || valueText.empty()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d missing item/value args.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (!asynPort) {
    return setError(ERROR_MAIN_ASYN_PORT_DRIVER_NULL, "Asyn port unavailable.");
  }

  ecmcDataItem *item = asynPort->findAvailDataItem(itemName.c_str());
  if (!item || !item->getDataItemInfo() || !item->getDataItemInfo()->dataPointerValid) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d item not found.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (!isScalarNumericType(item->getEcmcDataType())) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d item type unsupported.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (item->getEcmcDataSize() < scalarTypeSize(item->getEcmcDataType())) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d item size unsupported.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  if (step.action == ECMC_SEQ_ACTION_SET_ITEM && !item->getAllowWriteToEcmc()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d item is not writable.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }

  char *end = nullptr;
  const double value = strtod(valueText.c_str(), &end);
  if (end == valueText.c_str()) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d value parse failed.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }

  step.item = item;
  step.itemValue = value;
  if (!parseCompareOp(opText, &step.compareOp)) {
    char msg[ECMC_SEQ_TEXT_LEN] = {0};
    snprintf(msg, sizeof(msg), "Step %d compare op invalid.", stepIndex);
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, msg);
  }
  return 0;
}

int ecmcMotionSequence::applyEditStep() {
  if (editIndex_ < 0 || editIndex_ >= maxSteps_) {
    return setError(ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE, "Edit step index out of range.");
  }

  int error = setStep(editIndex_,
                      edit_.enabled,
                      edit_.action,
                      edit_.axis,
                      edit_.position,
                      edit_.velocity,
                      edit_.acceleration,
                      edit_.deceleration,
                      edit_.timeoutMs);
  if (error) {
    return error;
  }
  return setStepText(editIndex_,
                     edit_.name,
                     edit_.transition,
                     edit_.onError,
                     edit_.args);
}

int ecmcMotionSequence::setStep(int stepIndex,
                                int enabled,
                                int action,
                                int axis,
                                double position,
                                double velocity,
                                double acceleration,
                                double deceleration,
                                double timeoutMs) {
  if (stepIndex < 0 || stepIndex >= maxSteps_) {
    return setError(ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE, "Step index out of range.");
  }

  {
    std::lock_guard<std::mutex> guard(stepsMutex_);
    auto &step = steps_[stepIndex];
    step.enabled = enabled ? 1 : 0;
    step.action = action;
    step.axis = axis;
    step.position = position;
    step.velocity = velocity;
    step.acceleration = acceleration;
    step.deceleration = deceleration;
    step.timeoutMs = timeoutMs;
  }
  statValid_ = 0;
  statArmed_ = 0;
  if (statState_ == ECMC_SEQ_STATE_ARMED) {
    statState_ = ECMC_SEQ_STATE_IDLE;
  }
  statErrorId_ = 0;
  copyText(statErrorText_, sizeof(statErrorText_), "");
  setValidationText("Sequence changed. Compile required.");
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::setStepText(int stepIndex,
                                    const char *name,
                                    const char *transition,
                                    const char *onError,
                                    const char *args) {
  if (stepIndex < 0 || stepIndex >= maxSteps_) {
    return setError(ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE, "Step index out of range.");
  }

  {
    std::lock_guard<std::mutex> guard(stepsMutex_);
    auto &step = steps_[stepIndex];
    copyText(step.name, sizeof(step.name), name);
    copyText(step.transition, sizeof(step.transition), transition);
    copyText(step.onError, sizeof(step.onError), onError);
    copyText(step.args, sizeof(step.args), args);
  }
  statValid_ = 0;
  statArmed_ = 0;
  if (statState_ == ECMC_SEQ_STATE_ARMED) {
    statState_ = ECMC_SEQ_STATE_IDLE;
  }
  setValidationText("Sequence changed. Compile required.");
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::readStep() {
  if (readIndex_ < 0 || readIndex_ >= maxSteps_) {
    return setError(ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE, "Read step index out of range.");
  }

  {
    std::lock_guard<std::mutex> guard(stepsMutex_);
    read_ = steps_[readIndex_];
  }

  if (seqAsynPort_) {
    seqAsynPort_->refreshParam(readIndexParam_);
    seqAsynPort_->refreshParam(readEnabledParam_);
    seqAsynPort_->refreshParam(readActionParam_);
    seqAsynPort_->refreshParam(readAxisParam_);
    seqAsynPort_->refreshParam(readPositionParam_);
    seqAsynPort_->refreshParam(readVelocityParam_);
    seqAsynPort_->refreshParam(readAccelerationParam_);
    seqAsynPort_->refreshParam(readDecelerationParam_);
    seqAsynPort_->refreshParam(readTimeoutMsParam_);
    seqAsynPort_->refreshParam(readNameParam_);
    seqAsynPort_->refreshParam(readTransitionParam_);
    seqAsynPort_->refreshParam(readOnErrorParam_);
    seqAsynPort_->refreshParam(readArgsParam_);
  }
  return 0;
}

int ecmcMotionSequence::readStepOffset(int offset) {
  int newIndex = readIndex_ + offset;
  if (newIndex < 0) {
    newIndex = 0;
  }
  if (newIndex >= maxSteps_) {
    newIndex = maxSteps_ - 1;
  }
  readIndex_ = newIndex;
  return readStep();
}

int ecmcMotionSequence::requestCompile(bool waitForCompletion) {
  if (statRunning_) {
    return setError(ERROR_AXIS_BUSY, "Cannot compile while sequence is running.");
  }
  if (!compileRequestEvent_ || !compileDoneEvent_ || !compileThread_) {
    return runCompile();
  }

  const int requestGeneration =
    compileRequestGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
  compileRequestPending_.store(1, std::memory_order_release);
  epicsEventSignal(compileRequestEvent_);

  if (!waitForCompletion) {
    return 0;
  }

  while (compileCompletedGeneration_.load(std::memory_order_acquire) < requestGeneration) {
    epicsEventWait(compileDoneEvent_);
  }
  return compileResult_.load(std::memory_order_acquire);
}

int ecmcMotionSequence::runCompile() {
  if (statRunning_) {
    return setError(ERROR_AXIS_BUSY, "Cannot compile while sequence is running.");
  }

  std::vector<ecmcSeqStep> stepsSnapshot;
  {
    std::lock_guard<std::mutex> guard(stepsMutex_);
    stepsSnapshot = steps_;
  }

  std::vector<ecmcSeqStep> compiledPlan;
  int enabledCount = 0;
  statValid_ = 0;
  statArmed_ = 0;

  for (int i = 0; i < maxSteps_; ++i) {
    auto step = stepsSnapshot[i];
    if (!step.enabled) {
      continue;
    }
    enabledCount++;
    const int error = prepareStep(i, step);
    if (error) {
      return error;
    }
    compiledPlan.push_back(step);
  }

  {
    std::lock_guard<std::mutex> guard(planMutex_);
    compiledPlan_ = compiledPlan;
  }
  statStepCount_ = enabledCount;
  statValid_ = 1;
  statState_ = ECMC_SEQ_STATE_IDLE;
  statErrorId_ = 0;
  copyText(statErrorText_, sizeof(statErrorText_), "");
  setValidationText(enabledCount ? "Compile OK." : "Compile OK. No enabled steps.");
  refreshStatus();
  return 0;
}

void ecmcMotionSequence::compileLoop() {
  while (!compileStop_.load(std::memory_order_acquire)) {
    epicsEventWait(compileRequestEvent_);
    if (compileStop_.load(std::memory_order_acquire)) {
      break;
    }
    if (!compileRequestPending_.exchange(0, std::memory_order_acq_rel)) {
      continue;
    }

    statCompileBusy_ = 1;
    refreshStatus();
    const int result = runCompile();
    compileResult_.store(result, std::memory_order_release);
    compileCompletedGeneration_.store(
      compileRequestGeneration_.load(std::memory_order_acquire),
      std::memory_order_release);
    statCompileBusy_ = 0;
    refreshStatus();
    epicsEventSignal(compileDoneEvent_);
  }
}

int ecmcMotionSequence::startCompileWorker() {
  compileRequestEvent_ = epicsEventCreate(epicsEventEmpty);
  compileDoneEvent_ = epicsEventCreate(epicsEventEmpty);
  if (!compileRequestEvent_ || !compileDoneEvent_) {
    setValidationText("Compile worker unavailable. Compile runs synchronously.");
    return 0;
  }

  char threadName[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(threadName, sizeof(threadName), "ecmcSeq%dCompile", index_);
  compileThread_ = epicsThreadCreate(threadName,
                                     epicsThreadPriorityLow,
                                     epicsThreadGetStackSize(epicsThreadStackSmall),
                                     compileThreadEntry,
                                     this);
  if (!compileThread_) {
    setValidationText("Compile worker unavailable. Compile runs synchronously.");
    return 0;
  }
  return 0;
}

void ecmcMotionSequence::stopCompileWorker() {
  compileStop_.store(1, std::memory_order_release);
  if (compileRequestEvent_) {
    epicsEventSignal(compileRequestEvent_);
  }
}

int ecmcMotionSequence::arm() {
  if (statRunning_) {
    return setError(ERROR_AXIS_BUSY, "Cannot arm while sequence is running.");
  }
  if (!statValid_) {
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "Cannot arm uncompiled sequence.");
  }
  {
    std::lock_guard<std::mutex> guard(planMutex_);
    activePlan_ = compiledPlan_;
  }
  statArmed_ = 1;
  statState_ = ECMC_SEQ_STATE_ARMED;
  statStepIndex_ = -1;
  statAction_ = ECMC_SEQ_ACTION_NOP;
  statElapsedMs_ = 0.0;
  copyText(statStepName_, sizeof(statStepName_), "");
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::start() {
  if (!statArmed_) {
    return setError(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "Cannot start sequence that is not armed.");
  }
  requestStart_.store(1, std::memory_order_release);
  setValidationText("Start requested.");
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::stop() {
  requestStop_.store(1, std::memory_order_release);
  refreshStatus();
  return 0;
}

int ecmcMotionSequence::reset() {
  requestReset_.store(1, std::memory_order_release);
  if (statRunning_) {
    refreshStatus();
    return 0;
  }
  statRunning_ = 0;
  statArmed_ = 0;
  statStepIndex_ = -1;
  statAction_ = ECMC_SEQ_ACTION_NOP;
  statElapsedMs_ = 0.0;
  copyText(statStepName_, sizeof(statStepName_), "");
  statErrorId_ = 0;
  statState_ = statValid_ ? ECMC_SEQ_STATE_IDLE : ECMC_SEQ_STATE_IDLE;
  copyText(statErrorText_, sizeof(statErrorText_), "");
  refreshStatus();
  return 0;
}

void ecmcMotionSequence::executeRT(double cycleTimeS) {
  if (requestReset_.exchange(0, std::memory_order_acq_rel)) {
    if (rtChildSeq_) {
      rtChildSeq_->stopRT();
      rtChildSeq_ = nullptr;
    }
    clearTriggersRT();
    statRunning_ = 0;
    statArmed_ = 0;
    statStepIndex_ = -1;
    statAction_ = ECMC_SEQ_ACTION_NOP;
    statElapsedMs_ = 0.0;
    copyText(statStepName_, sizeof(statStepName_), "");
    statErrorId_ = 0;
    statState_ = ECMC_SEQ_STATE_IDLE;
    resetStepRuntimeRT();
    copyText(statErrorText_, sizeof(statErrorText_), "");
    return;
  }

  if (requestStop_.exchange(0, std::memory_order_acq_rel)) {
    if (rtChildSeq_) {
      rtChildSeq_->stopRT();
      rtChildSeq_ = nullptr;
    }
    clearTriggersRT();
    statRunning_ = 0;
    statArmed_ = 0;
    statState_ = ECMC_SEQ_STATE_STOPPED;
    resetStepRuntimeRT();
    return;
  }

  if (requestStart_.exchange(0, std::memory_order_acq_rel)) {
    if (statArmed_) {
      statRunning_ = 1;
      statState_ = ECMC_SEQ_STATE_RUNNING;
      statStepIndex_ = 0;
      clearTriggersRT();
      resetStepRuntimeRT();
    }
  }

  if (!statRunning_) {
    return;
  }

  if (statStepIndex_ < 0 ||
      statStepIndex_ >= static_cast<int32_t>(activePlan_.size())) {
    statRunning_ = 0;
    statArmed_ = 0;
    statState_ = ECMC_SEQ_STATE_DONE;
    statStepIndex_ = -1;
    statAction_ = ECMC_SEQ_ACTION_NOP;
    statElapsedMs_ = 0.0;
    copyText(statStepName_, sizeof(statStepName_), "");
    clearTriggersRT();
    resetStepRuntimeRT();
    return;
  }

  evalTriggersRT(cycleTimeS);

  auto &step = activePlan_[statStepIndex_];
  if (!rtStepEntered_) {
    rtStepEntered_ = true;
    rtStepElapsedMs_ = 0.0;
    statAction_ = step.action;
    copyText(statStepName_, sizeof(statStepName_), step.name);
  } else {
    rtStepElapsedMs_ += cycleTimeS * 1000.0;
  }
  statElapsedMs_ = rtStepElapsedMs_;

  if (step.action != ECMC_SEQ_ACTION_WAIT_TIME &&
      step.timeoutMs > 0.0 &&
      rtStepElapsedMs_ > step.timeoutMs) {
    failStepRT(ERROR_AXIS_BUSY, "Sequence step timeout.");
    return;
  }

  ecmcMcAxisRef axisRef;
  axisRef.axisIndex = step.axis;

  switch (step.action) {
  case ECMC_SEQ_ACTION_NOP:
    advanceStepRT();
    break;
  case ECMC_SEQ_ACTION_MC_RESET:
    rtReset_.run(axisRef, true);
    if (rtReset_.Error) {
      failStepRT(static_cast<int>(rtReset_.ErrorID), "MC_Reset failed.");
    } else if (!step.waitForDone || rtReset_.Done) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_POWER:
    rtPower_.run(axisRef, step.enable != 0);
    if (rtPower_.Error) {
      failStepRT(static_cast<int>(rtPower_.ErrorID), "MC_Power failed.");
    } else if (!step.waitForDone || rtPower_.Status == (step.enable != 0)) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_HOME:
    rtHome_.run(axisRef,
                true,
                step.cmdData,
                step.position,
                step.velocity,
                step.acceleration,
                step.deceleration,
                step.itemValue);
    if (rtHome_.Error) {
      failStepRT(static_cast<int>(rtHome_.ErrorID), "MC_Home failed.");
    } else if (rtHome_.CommandAborted) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "MC_Home aborted.");
    } else if (!step.waitForDone || rtHome_.Done) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE:
    rtMoveAbsolute_.run(axisRef,
                        true,
                        step.position,
                        step.velocity,
                        step.acceleration,
                        step.deceleration);
    if (rtMoveAbsolute_.Error) {
      failStepRT(static_cast<int>(rtMoveAbsolute_.ErrorID), "MC_MoveAbsolute failed.");
    } else if (rtMoveAbsolute_.CommandAborted) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "MC_MoveAbsolute aborted.");
    } else if (!step.waitForDone || rtMoveAbsolute_.Done) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_MOVE_RELATIVE:
    rtMoveRelative_.run(axisRef,
                        true,
                        step.position,
                        step.velocity,
                        step.acceleration,
                        step.deceleration);
    if (rtMoveRelative_.Error) {
      failStepRT(static_cast<int>(rtMoveRelative_.ErrorID), "MC_MoveRelative failed.");
    } else if (rtMoveRelative_.CommandAborted) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "MC_MoveRelative aborted.");
    } else if (!step.waitForDone || rtMoveRelative_.Done) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_MOVE_VELOCITY:
    rtMoveVelocity_.run(axisRef,
                        true,
                        step.velocity,
                        step.acceleration,
                        step.deceleration);
    if (rtMoveVelocity_.Error) {
      failStepRT(static_cast<int>(rtMoveVelocity_.ErrorID), "MC_MoveVelocity failed.");
    } else if (step.waitForDone) {
      double actualVelocity = 0.0;
      if (!axes[step.axis] ||
          axes[step.axis]->getVelAct(&actualVelocity)) {
        failStepRT(ERROR_MAIN_AXIS_OBJECT_NULL, "MC_MoveVelocity velocity read failed.");
      } else if (actualVelocity >= step.velocity - step.itemValue &&
                 actualVelocity <= step.velocity + step.itemValue) {
        advanceStepRT();
      }
    } else {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_MC_HALT:
    rtHalt_.run(axisRef, true);
    if (rtHalt_.Error) {
      failStepRT(static_cast<int>(rtHalt_.ErrorID), "MC_Halt failed.");
    } else if (!step.waitForDone || rtHalt_.Done) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_WAIT_IN_POSITION:
    if (axisInPosition(step.axis)) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_SET_ITEM:
    if (writeItemScalarRT(step)) {
      advanceStepRT();
    } else {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "SetItem failed.");
    }
    break;
  case ECMC_SEQ_ACTION_WAIT_ITEM: {
    double value = 0.0;
    if (!readItemScalarRT(step, &value)) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "WaitItem read failed.");
      break;
    }
    if (compareItemValue(value, step)) {
      advanceStepRT();
    }
    break;
  }
  case ECMC_SEQ_ACTION_WAIT_TIME:
    if (rtStepElapsedMs_ >= step.timeoutMs) {
      advanceStepRT();
    }
    break;
  case ECMC_SEQ_ACTION_RUN_SEQUENCE:
    if (!step.childSeq) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "RunSequence child missing.");
      break;
    }
    if (rtStepElapsedMs_ <= 0.0) {
      if (!step.childSeq->startFromActivePlanRT()) {
        failStepRT(ERROR_AXIS_BUSY, "RunSequence child start failed.");
        break;
      }
      rtChildSeq_ = step.childSeq;
    }
    if (step.childSeq->statState_ == ECMC_SEQ_STATE_ERROR) {
      failStepRT(step.childSeq->statErrorId_, "RunSequence child failed.");
    } else if (step.childSeq->statState_ == ECMC_SEQ_STATE_STOPPED) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "RunSequence child stopped.");
    } else if (step.childSeq->statState_ == ECMC_SEQ_STATE_DONE) {
      advanceStepRT();
    } else if (!step.childSeq->statRunning_) {
      failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL, "RunSequence child not running.");
    }
    break;
  case ECMC_SEQ_ACTION_ARM_POS_TRIGGER:
  case ECMC_SEQ_ACTION_ARM_TIME_TRIGGER:
    armPosTriggerRT(step);
    advanceStepRT();
    break;
  case ECMC_SEQ_ACTION_WAIT_TRIGGER_DONE:
    if (waitTriggerDoneRT(step.cmdData)) {
      advanceStepRT();
    }
    break;
  default:
    failStepRT(ERROR_MAIN_SEQUENCE_OBJECT_NULL,
               "Sequence RT runner does not support this action.");
    break;
  }
}

void ecmcMotionSequence::advanceStepRT() {
  rtChildSeq_ = nullptr;
  resetStepRuntimeRT();
  statStepIndex_++;
}

void ecmcMotionSequence::failStepRT(int errorId, const char *message) {
  if (rtChildSeq_) {
    rtChildSeq_->stopRT();
    rtChildSeq_ = nullptr;
  }
  statErrorId_ = errorId;
  statState_ = ECMC_SEQ_STATE_ERROR;
  statRunning_ = 0;
  statArmed_ = 0;
  clearTriggersRT();
  resetStepRuntimeRT();
  copyText(statErrorText_, sizeof(statErrorText_), message);
}

void ecmcMotionSequence::resetStepRuntimeRT() {
  rtStepEntered_ = false;
  rtStepElapsedMs_ = 0.0;
  rtReset_ = ecmcMcReset();
  rtPower_ = ecmcMcPower();
  rtHome_ = ecmcMcHome();
  rtMoveAbsolute_ = ecmcMcMoveAbsolute();
  rtMoveRelative_ = ecmcMcMoveRelative();
  rtMoveVelocity_ = ecmcMcMoveVelocity();
  rtHalt_ = ecmcMcHalt();
}

bool ecmcMotionSequence::axisInPosition(int axisIndex) const {
  if (axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return false;
  }
  auto *axis = axes[axisIndex];
  if (!axis) {
    return false;
  }
  auto *status = axis->getAxisStatusDataPtr();
  return status && status->statusWord_.attarget && !axis->getBusy();
}

bool ecmcMotionSequence::writeItemScalarRT(ecmcSeqStep &step) {
  if (!step.item || !step.item->getDataItemInfo()) {
    return false;
  }

  switch (step.item->getEcmcDataType()) {
  case ECMC_EC_U8: {
    uint8_t value = static_cast<uint8_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_S8: {
    int8_t value = static_cast<int8_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_U16: {
    uint16_t value = static_cast<uint16_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_S16: {
    int16_t value = static_cast<int16_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_U32: {
    uint32_t value = static_cast<uint32_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_S32: {
    int32_t value = static_cast<int32_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_U64: {
    uint64_t value = static_cast<uint64_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_S64: {
    int64_t value = static_cast<int64_t>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_F32: {
    float value = static_cast<float>(step.itemValue);
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  case ECMC_EC_F64: {
    double value = step.itemValue;
    return step.item->write(reinterpret_cast<uint8_t *>(&value), sizeof(value)) == 0;
  }
  default:
    return false;
  }
}

bool ecmcMotionSequence::readItemScalarRT(ecmcSeqStep &step, double *value) {
  if (!value || !step.item || !step.item->getDataItemInfo()) {
    return false;
  }

  switch (step.item->getEcmcDataType()) {
  case ECMC_EC_U8: {
    uint8_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_S8: {
    int8_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_U16: {
    uint16_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_S16: {
    int16_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_U32: {
    uint32_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_S32: {
    int32_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_U64: {
    uint64_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = static_cast<double>(tmp);
    return true;
  }
  case ECMC_EC_S64: {
    int64_t tmp = 0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = static_cast<double>(tmp);
    return true;
  }
  case ECMC_EC_F32: {
    float tmp = 0.0f;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  case ECMC_EC_F64: {
    double tmp = 0.0;
    if (step.item->read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp))) return false;
    *value = tmp;
    return true;
  }
  default:
    return false;
  }
}

bool ecmcMotionSequence::compareItemValue(double actual, const ecmcSeqStep &step) const {
  const double diff = actual - step.itemValue;
  switch (step.compareOp) {
  case ECMC_SEQ_CMP_EQ:
    return diff < 1.0e-9 && diff > -1.0e-9;
  case ECMC_SEQ_CMP_NE:
    return !(diff < 1.0e-9 && diff > -1.0e-9);
  case ECMC_SEQ_CMP_GT:
    return actual > step.itemValue;
  case ECMC_SEQ_CMP_GE:
    return actual >= step.itemValue;
  case ECMC_SEQ_CMP_LT:
    return actual < step.itemValue;
  case ECMC_SEQ_CMP_LE:
    return actual <= step.itemValue;
  default:
    return false;
  }
}

bool ecmcMotionSequence::startFromActivePlanRT() {
  if (statRunning_) {
    return false;
  }
  if (activePlan_.empty()) {
    return false;
  }
  statRunning_ = 1;
  statState_ = ECMC_SEQ_STATE_RUNNING;
  statStepIndex_ = 0;
  statAction_ = ECMC_SEQ_ACTION_NOP;
  statElapsedMs_ = 0.0;
  statErrorId_ = 0;
  copyText(statStepName_, sizeof(statStepName_), "");
  copyText(statErrorText_, sizeof(statErrorText_), "");
  clearTriggersRT();
  resetStepRuntimeRT();
  return true;
}

void ecmcMotionSequence::stopRT() {
  statRunning_ = 0;
  statArmed_ = 0;
  statState_ = ECMC_SEQ_STATE_STOPPED;
  clearTriggersRT();
  resetStepRuntimeRT();
  if (rtChildSeq_) {
    rtChildSeq_->stopRT();
    rtChildSeq_ = nullptr;
  }
}

void ecmcMotionSequence::clearTriggersRT() {
  for (auto &trig : rtTriggers_) {
    if (trig.pulseActive) {
      writeTriggerOutputRT(trig, 0.0, 0);
    }
  }
  rtTriggers_.clear();
}

void ecmcMotionSequence::evalTriggersRT(double cycleTimeS) {
  for (auto &trig : rtTriggers_) {
    if (!trig.active && !trig.pulseActive) {
      continue;
    }

    if (trig.pulseActive) {
      trig.pulseElapsedMs += cycleTimeS * 1000.0;
      if (trig.pulseMs > 0.0 && trig.pulseElapsedMs >= trig.pulseMs) {
        writeTriggerOutputRT(trig, 0.0, 0);
        trig.pulseActive = 0;
      }
    }

    if (!trig.active) {
      continue;
    }

    bool crossed = false;
    double pos = 0.0;
    if (trig.timeBased) {
      trig.elapsedMs += cycleTimeS * 1000.0;
      crossed = trig.elapsedMs >= trig.nextTimeMs;
    } else {
      if (trig.axis < 0 ||
          trig.axis >= ECMC_MAX_AXES ||
          !axes[trig.axis] ||
          axes[trig.axis]->getPosAct(&pos)) {
        continue;
      }

      const bool forward = trig.period > 0.0;
      crossed = forward
        ? (trig.lastPos < trig.nextPos && pos >= trig.nextPos)
        : (trig.lastPos > trig.nextPos && pos <= trig.nextPos);
    }

    if (crossed) {
      writeTriggerOutputRT(trig, trig.value, 1);
      trig.fired++;
      trig.nextPos = trig.startPos + trig.period * trig.fired;
      trig.nextTimeMs = trig.startPos + trig.period * trig.fired;
      if (trig.pulseMs > 0.0) {
        trig.pulseActive = 1;
        trig.pulseElapsedMs = 0.0;
      }
      if (trig.fired >= trig.count) {
        trig.active = 0;
      }
    }
    if (!trig.timeBased) {
      trig.lastPos = pos;
    }
  }
}

void ecmcMotionSequence::armPosTriggerRT(const ecmcSeqStep &step) {
  double pos = 0.0;
  if (step.axis >= 0 && step.axis < ECMC_MAX_AXES && axes[step.axis]) {
    axes[step.axis]->getPosAct(&pos);
  }

  for (auto &trig : rtTriggers_) {
    if (trig.id == step.cmdData) {
      trig = ecmcSeqPosTrigger();
      trig.id = step.cmdData;
      trig.axis = step.axis;
      trig.count = static_cast<int32_t>(step.acceleration);
      trig.startPos = step.position;
      trig.period = step.velocity;
      trig.value = step.itemValue;
      trig.pulseMs = step.deceleration;
      trig.nextPos = step.position;
      trig.nextTimeMs = step.position;
      trig.lastPos = pos;
      trig.timeBased = step.action == ECMC_SEQ_ACTION_ARM_TIME_TRIGGER ? 1 : 0;
      trig.soft = step.triggerSoft;
      trig.item = step.item;
      trig.active = 1;
      return;
    }
  }

  ecmcSeqPosTrigger trig;
  trig.id = step.cmdData;
  trig.axis = step.axis;
  trig.count = static_cast<int32_t>(step.acceleration);
  trig.startPos = step.position;
  trig.period = step.velocity;
  trig.value = step.itemValue;
  trig.pulseMs = step.deceleration;
  trig.nextPos = step.position;
  trig.nextTimeMs = step.position;
  trig.lastPos = pos;
  trig.timeBased = step.action == ECMC_SEQ_ACTION_ARM_TIME_TRIGGER ? 1 : 0;
  trig.soft = step.triggerSoft;
  trig.item = step.item;
  trig.active = 1;
  rtTriggers_.push_back(trig);
}

bool ecmcMotionSequence::waitTriggerDoneRT(int triggerId) const {
  for (const auto &trig : rtTriggers_) {
    if (trig.id == triggerId) {
      return !trig.active && !trig.pulseActive && trig.fired >= trig.count;
    }
  }
  return false;
}

void ecmcMotionSequence::writeTriggerOutputRT(ecmcSeqPosTrigger &trig,
                                              double value,
                                              int pulseActive) {
  if (trig.soft) {
    if (pulseActive) {
      statSoftTriggerId_ = trig.id;
      statSoftTriggerCount_++;
    }
    return;
  }

  ecmcSeqStep pulseStep;
  pulseStep.item = trig.item;
  pulseStep.itemValue = value;
  writeItemScalarRT(pulseStep);
}

int ecmcMotionSequence::report(int stepIndex) {
  printf("Motion sequence %d\n", index_);
  printf("  port           = %s\n", portName_);
  printf("  max_steps      = %d\n", maxSteps_);
  printf("  state          = %d\n", statState_);
  printf("  valid          = %d\n", statValid_);
  printf("  armed          = %d\n", statArmed_);
  printf("  running        = %d\n", statRunning_);
  printf("  compile_busy   = %d\n", statCompileBusy_);
  printf("  current_step   = %d\n", statStepIndex_);
  printf("  current_action = %d\n", statAction_);
  printf("  elapsed_ms     = %lf\n", statElapsedMs_);
  printf("  step_count     = %d\n", statStepCount_);
  printf("  error_id       = 0x%x\n", statErrorId_);
  printf("  error_text     = %s\n", statErrorText_);
  printf("  validation     = %s\n", statValidationText_);
  printf("  soft_trig_id   = %d\n", statSoftTriggerId_);
  printf("  soft_trig_cnt  = %d\n", statSoftTriggerCount_);

  std::vector<ecmcSeqStep> stepsSnapshot;
  {
    std::lock_guard<std::mutex> guard(stepsMutex_);
    stepsSnapshot = steps_;
  }

  const int firstStep = stepIndex >= 0 ? stepIndex : 0;
  const int lastStep = stepIndex >= 0 ? stepIndex : maxSteps_ - 1;
  if (firstStep < 0 || lastStep >= maxSteps_) {
    return ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE;
  }

  for (int i = firstStep; i <= lastStep; ++i) {
    const auto &step = stepsSnapshot[i];
    printf("  step[%d]\n", i);
    printf("    enabled      = %d\n", step.enabled);
    printf("    action       = %d\n", step.action);
    printf("    axis         = %d\n", step.axis);
    printf("    position     = %lf\n", step.position);
    printf("    velocity     = %lf\n", step.velocity);
    printf("    acceleration = %lf\n", step.acceleration);
    printf("    deceleration = %lf\n", step.deceleration);
    printf("    timeout_ms   = %lf\n", step.timeoutMs);
    printf("    name         = %s\n", step.name);
    printf("    transition   = %s\n", step.transition);
    printf("    on_error     = %s\n", step.onError);
    printf("    args         = %s\n", step.args);
  }
  return 0;
}

void ecmcMotionSequence::refreshStatus() {
  if (!seqAsynPort_) {
    return;
  }
  seqAsynPort_->refreshParam(statStateParam_);
  seqAsynPort_->refreshParam(statValidParam_);
  seqAsynPort_->refreshParam(statArmedParam_);
  seqAsynPort_->refreshParam(statRunningParam_);
  seqAsynPort_->refreshParam(statCompileBusyParam_);
  seqAsynPort_->refreshParam(statStepIndexParam_);
  seqAsynPort_->refreshParam(statActionParam_);
  seqAsynPort_->refreshParam(statErrorIdParam_);
  seqAsynPort_->refreshParam(statStepCountParam_);
  seqAsynPort_->refreshParam(statElapsedMsParam_);
  seqAsynPort_->refreshParam(statStepNameParam_);
  seqAsynPort_->refreshParam(statErrorTextParam_);
  seqAsynPort_->refreshParam(statValidationTextParam_);
  seqAsynPort_->refreshParam(statSoftTriggerIdParam_);
  seqAsynPort_->refreshParam(statSoftTriggerCountParam_);
}

asynStatus ecmcMotionSequence::asynWriteApply(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->applyEditStep() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteCompile(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->requestCompile(false) ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteArm(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->arm() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteStart(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->start() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteStop(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->stop() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteReset(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->reset() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteRead(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->readStep() ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteReadNext(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->readStepOffset(1) ? asynError : asynSuccess;
}

asynStatus ecmcMotionSequence::asynWriteReadPrev(void *, size_t, asynParamType, void *userObj) {
  return static_cast<ecmcMotionSequence *>(userObj)->readStepOffset(-1) ? asynError : asynSuccess;
}

void ecmcMotionSequence::compileThreadEntry(void *userObj) {
  static_cast<ecmcMotionSequence *>(userObj)->compileLoop();
}

int createMotionSeq(int index, int maxSteps, const char *portName) {
  if (index < 0 || index >= ECMC_MAX_MOTION_SEQUENCES) {
    LOGERR("%s/%s:%d: ERROR: Motion sequence index %d out of range.\n",
           __FILE__, __FUNCTION__, __LINE__, index);
    return ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE;
  }
  if (!asynPort) {
    return ERROR_MAIN_ASYN_PORT_DRIVER_NULL;
  }
  if (motionSeqs[index]) {
    LOGERR("%s/%s:%d: ERROR: Motion sequence %d already exists.\n",
           __FILE__, __FUNCTION__, __LINE__, index);
    return ERROR_MAIN_AXIS_ALREADY_CREATED;
  }
  if (maxSteps <= 0) {
    maxSteps = kDefaultMaxSteps;
  }
  if (maxSteps > kHardMaxSteps) {
    LOGERR("%s/%s:%d: ERROR: Motion sequence max steps %d out of range.\n",
           __FILE__, __FUNCTION__, __LINE__, maxSteps);
    return ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE;
  }

  char resolvedPortName[ECMC_SEQ_TEXT_LEN] = {0};
  if (portName && portName[0]) {
    snprintf(resolvedPortName, sizeof(resolvedPortName), "%s", portName);
  } else {
    snprintf(resolvedPortName, sizeof(resolvedPortName), "ECMC_SEQ%d", index);
  }

  ecmcMotionSequence *seq = nullptr;
  try {
    seq = new (std::nothrow) ecmcMotionSequence(index, maxSteps, resolvedPortName);
  } catch (...) {
    seq = nullptr;
  }
  if (!seq) {
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }

  const int seqParamCount = 128;
  seq->seqAsynPort_ = new (std::nothrow) ecmcMotionSequencePort(resolvedPortName,
                                                                seqParamCount);
  if (!seq->seqAsynPort_) {
    delete seq;
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }

  const int error = seq->createAsynParams(seq->seqAsynPort_);
  if (error) {
    delete seq;
    return error;
  }
  motionSeqs[index] = seq;
  return 0;
}

namespace {
ecmcMotionSequence *getMotionSeq(int seqIndex, int *errorCode) {
  if (errorCode) {
    *errorCode = 0;
  }
  if (seqIndex < 0 || seqIndex >= ECMC_MAX_MOTION_SEQUENCES) {
    if (errorCode) {
      *errorCode = ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE;
    }
    return nullptr;
  }
  if (!motionSeqs[seqIndex]) {
    if (errorCode) {
      *errorCode = ERROR_MAIN_SEQUENCE_OBJECT_NULL;
    }
    return nullptr;
  }
  return motionSeqs[seqIndex];
}
}

int setMotionSeqStep(int seqIndex,
                     int stepIndex,
                     int enabled,
                     int action,
                     int axis,
                     double position,
                     double velocity,
                     double acceleration,
                     double deceleration,
                     double timeoutMs) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  if (!seq) {
    return error;
  }
  return seq->setStep(stepIndex,
                      enabled,
                      action,
                      axis,
                      position,
                      velocity,
                      acceleration,
                      deceleration,
                      timeoutMs);
}

int setMotionSeqStepText(int seqIndex,
                         int stepIndex,
                         const char *name,
                         const char *transition,
                         const char *onError,
                         const char *args) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  if (!seq) {
    return error;
  }
  return seq->setStepText(stepIndex, name, transition, onError, args);
}

int compileMotionSeq(int seqIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->requestCompile(true) : error;
}

int armMotionSeq(int seqIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->arm() : error;
}

int startMotionSeq(int seqIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->start() : error;
}

int stopMotionSeq(int seqIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->stop() : error;
}

int resetMotionSeq(int seqIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->reset() : error;
}

int reportMotionSeq(int seqIndex, int stepIndex) {
  int error = 0;
  auto *seq = getMotionSeq(seqIndex, &error);
  return seq ? seq->report(stepIndex) : error;
}

namespace {
int setStepAndText(int seqIndex,
                   int stepIndex,
                   int action,
                   int axis,
                   double position,
                   double velocity,
                   double acceleration,
                   double deceleration,
                   double timeoutMs,
                   const char *name,
                   const char *transition,
                   const char *onError,
                   const char *args) {
  int error = setMotionSeqStep(seqIndex,
                               stepIndex,
                               1,
                               action,
                               axis,
                               position,
                               velocity,
                               acceleration,
                               deceleration,
                               timeoutMs);
  if (error) {
    return error;
  }
  return setMotionSeqStepText(seqIndex,
                              stepIndex,
                              name,
                              transition ? transition : "Done",
                              onError ? onError : "Abort",
                              args ? args : "");
}
}

int setMotionSeqNop(int seqIndex, int stepIndex) {
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_NOP,
                        -1,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        "NOP",
                        "Done",
                        "Abort",
                        "");
}

int setMotionSeqWaitTime(int seqIndex, int stepIndex, double waitMs) {
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_WAIT_TIME,
                        -1,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        waitMs,
                        "Wait time",
                        "Done",
                        "Abort",
                        "");
}

int setMotionSeqRunSeq(int seqIndex, int stepIndex, int childSeqIndex, double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "Run sequence %d", childSeqIndex);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_RUN_SEQUENCE,
                        childSeqIndex,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        "Done",
                        "Abort",
                        "");
}

int setMotionSeqArmPosTrigger(int seqIndex,
                              int stepIndex,
                              int triggerId,
                              int axis,
                              const char *item,
                              double startPos,
                              double period,
                              int count,
                              double value,
                              double pulseMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "ArmPosTrigger %d", triggerId);
  snprintf(args,
           sizeof(args),
           "id=%d;item=%s;value=%g",
           triggerId,
           item ? item : "",
           value);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_ARM_POS_TRIGGER,
                        axis,
                        startPos,
                        period,
                        static_cast<double>(count),
                        pulseMs,
                        0.0,
                        name,
                        "Armed",
                        "Abort",
                        args);
}

int setMotionSeqArmTimeTrigger(int seqIndex,
                               int stepIndex,
                               int triggerId,
                               const char *item,
                               double delayMs,
                               double periodMs,
                               int count,
                               double value,
                               double pulseMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "ArmTimeTrigger %d", triggerId);
  snprintf(args,
           sizeof(args),
           "id=%d;item=%s;value=%g",
           triggerId,
           item ? item : "",
           value);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_ARM_TIME_TRIGGER,
                        -1,
                        delayMs,
                        periodMs,
                        static_cast<double>(count),
                        pulseMs,
                        0.0,
                        name,
                        "Armed",
                        "Abort",
                        args);
}

int setMotionSeqWaitTriggerDone(int seqIndex, int stepIndex, int triggerId, double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "WaitTriggerDone %d", triggerId);
  snprintf(args, sizeof(args), "id=%d", triggerId);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_WAIT_TRIGGER_DONE,
                        -1,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        "Done",
                        "Abort",
                        args);
}

int setMotionSeqReset(int seqIndex, int stepIndex, int axis, double timeoutMs) {
  return setMotionSeqResetWait(seqIndex, stepIndex, axis, timeoutMs, 1);
}

int setMotionSeqResetWait(int seqIndex,
                          int stepIndex,
                          int axis,
                          double timeoutMs,
                          int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "Reset axis %d", axis);
  snprintf(args, sizeof(args), "wait=%d", waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_RESET,
                        axis,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        waitForDone ? "Done" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqPower(int seqIndex, int stepIndex, int axis, int enable, double timeoutMs) {
  return setMotionSeqPowerWait(seqIndex, stepIndex, axis, enable, timeoutMs, 1);
}

int setMotionSeqPowerWait(int seqIndex,
                          int stepIndex,
                          int axis,
                          int enable,
                          double timeoutMs,
                          int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "%s axis %d", enable ? "Power" : "Disable", axis);
  snprintf(args,
           sizeof(args),
           "enable=%d;wait=%d",
           enable ? 1 : 0,
           waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_POWER,
                        axis,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        waitForDone ? (enable ? "Enabled" : "Disabled") : "Started",
                        "Abort",
                        args);
}

int setMotionSeqHome(int seqIndex,
                     int stepIndex,
                     int axis,
                     int homeSeq,
                     double homePosition,
                     double velocityTowardsCam,
                     double velocityOffCam,
                     double acceleration,
                     double deceleration,
                     double timeoutMs) {
  return setMotionSeqHomeWait(seqIndex,
                              stepIndex,
                              axis,
                              homeSeq,
                              homePosition,
                              velocityTowardsCam,
                              velocityOffCam,
                              acceleration,
                              deceleration,
                              timeoutMs,
                              1);
}

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
                         int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "Home axis %d", axis);
  snprintf(args,
           sizeof(args),
           "seq=%d;dec=%g;wait=%d",
           homeSeq,
           deceleration,
           waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_HOME,
                        axis,
                        homePosition,
                        velocityTowardsCam,
                        velocityOffCam,
                        acceleration,
                        timeoutMs,
                        name,
                        waitForDone ? "Done" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqMoveAbs(int seqIndex,
                        int stepIndex,
                        int axis,
                        double position,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs) {
  return setMotionSeqMoveAbsWait(seqIndex,
                                 stepIndex,
                                 axis,
                                 position,
                                 velocity,
                                 acceleration,
                                 deceleration,
                                 timeoutMs,
                                 1);
}

int setMotionSeqMoveAbsWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double position,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "MoveAbs axis %d", axis);
  snprintf(args, sizeof(args), "wait=%d", waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_MOVE_ABSOLUTE,
                        axis,
                        position,
                        velocity,
                        acceleration,
                        deceleration,
                        timeoutMs,
                        name,
                        waitForDone ? "Done" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqMoveRel(int seqIndex,
                        int stepIndex,
                        int axis,
                        double distance,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs) {
  return setMotionSeqMoveRelWait(seqIndex,
                                 stepIndex,
                                 axis,
                                 distance,
                                 velocity,
                                 acceleration,
                                 deceleration,
                                 timeoutMs,
                                 1);
}

int setMotionSeqMoveRelWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double distance,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "MoveRel axis %d", axis);
  snprintf(args, sizeof(args), "wait=%d", waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_MOVE_RELATIVE,
                        axis,
                        distance,
                        velocity,
                        acceleration,
                        deceleration,
                        timeoutMs,
                        name,
                        waitForDone ? "Done" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqMoveVel(int seqIndex,
                        int stepIndex,
                        int axis,
                        double velocity,
                        double acceleration,
                        double deceleration,
                        double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "MoveVel axis %d", axis);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_MOVE_VELOCITY,
                        axis,
                        0.0,
                        velocity,
                        acceleration,
                        deceleration,
                        timeoutMs,
                        name,
                        "Started",
                        "Abort",
                        "");
}

int setMotionSeqMoveVelWait(int seqIndex,
                            int stepIndex,
                            int axis,
                            double velocity,
                            double acceleration,
                            double deceleration,
                            double timeoutMs,
                            int waitForVelocity,
                            double tolerance) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "MoveVel axis %d", axis);
  snprintf(args,
           sizeof(args),
           "wait=%d;tol=%g",
           waitForVelocity ? 1 : 0,
           tolerance);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_MOVE_VELOCITY,
                        axis,
                        0.0,
                        velocity,
                        acceleration,
                        deceleration,
                        timeoutMs,
                        name,
                        waitForVelocity ? "InVelocity" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqHalt(int seqIndex, int stepIndex, int axis, double timeoutMs) {
  return setMotionSeqHaltWait(seqIndex, stepIndex, axis, timeoutMs, 1);
}

int setMotionSeqHaltWait(int seqIndex,
                         int stepIndex,
                         int axis,
                         double timeoutMs,
                         int waitForDone) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "Halt axis %d", axis);
  snprintf(args, sizeof(args), "wait=%d", waitForDone ? 1 : 0);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_MC_HALT,
                        axis,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        waitForDone ? "Done" : "Started",
                        "Abort",
                        args);
}

int setMotionSeqWaitInPos(int seqIndex, int stepIndex, int axis, double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "WaitInPos axis %d", axis);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_WAIT_IN_POSITION,
                        axis,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        "Done",
                        "Abort",
                        "");
}

int setMotionSeqSetItem(int seqIndex,
                        int stepIndex,
                        const char *item,
                        double value,
                        double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "SetItem %s", item ? item : "");
  snprintf(args, sizeof(args), "item=%s;value=%g", item ? item : "", value);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_SET_ITEM,
                        -1,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        "Done",
                        "Abort",
                        args);
}

int setMotionSeqWaitItem(int seqIndex,
                         int stepIndex,
                         const char *item,
                         const char *op,
                         double value,
                         double timeoutMs) {
  char name[ECMC_SEQ_TEXT_LEN] = {0};
  char args[ECMC_SEQ_TEXT_LEN] = {0};
  snprintf(name, sizeof(name), "WaitItem %s", item ? item : "");
  snprintf(args,
           sizeof(args),
           "item=%s;op=%s;value=%g",
           item ? item : "",
           op ? op : "==",
           value);
  return setStepAndText(seqIndex,
                        stepIndex,
                        ECMC_SEQ_ACTION_WAIT_ITEM,
                        -1,
                        0.0,
                        0.0,
                        0.0,
                        0.0,
                        timeoutMs,
                        name,
                        "Done",
                        "Abort",
                        args);
}
