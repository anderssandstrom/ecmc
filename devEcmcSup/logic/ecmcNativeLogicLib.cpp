/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeLogicLib.cpp
*
\*************************************************************************/

#include "ecmcNativeLogicLib.h"

#include "asynPortDriver.h"
#include "ecmcNativeLogic.h"
#include "ecmcPluginClient.h"
#include "ecmcDataItem.h"
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#include "ecmcGlobalsExtern.h"
#include "ecmcOctetIF.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>
#if defined(__linux__)
#  include <sys/syscall.h>
#endif
#include <pthread.h>
#include <unistd.h>

#ifndef VERSION_INT
#  define VERSION_INT(V, R, M, P) (((V) << 24) | ((R) << 16) | ((M) << 8) | (P))
#endif
#define VERSION_INT_4_37 VERSION_INT(4, 37, 0, 0)
#define ECMC_ASYN_VERSION_INT VERSION_INT(ASYN_VERSION, ASYN_REVISION, ASYN_MODIFICATION, 0)
#if ECMC_ASYN_VERSION_INT >= VERSION_INT_4_37
#  define ECMC_NATIVE_ASYN_PARAM_INT64
#endif

namespace {

constexpr const char* kDriverName = "ecmcNativeLogicLib";
constexpr const char* kNativeLogicGetApiSymbol = "ecmc_native_logic_get_api";
constexpr const char* kDefaultAsynPortBase = "NATIVE.LOGIC";

constexpr const char* kBuiltinControlWordName = "logic.ctrl.word";
constexpr const char* kBuiltinRequestedRateMsName = "logic.ctrl.rate_ms";
constexpr const char* kBuiltinActualRateMsName = "logic.stat.rate_ms";
constexpr const char* kBuiltinExecMsName = "logic.stat.exec_ms";
constexpr const char* kBuiltinInputMsName = "logic.stat.input_ms";
constexpr const char* kBuiltinOutputMsName = "logic.stat.output_ms";
constexpr const char* kBuiltinTotalMsName = "logic.stat.total_ms";
constexpr const char* kBuiltinDividerName = "logic.stat.div";
constexpr const char* kBuiltinExecuteCountName = "logic.stat.count";
constexpr const char* kBuiltinDebugTextName = "logic.stat.dbg_txt";

constexpr uint32_t kControlWordEnableExecutionBit = 0u;
constexpr uint32_t kControlWordEnableTimingBit = 1u;
constexpr uint32_t kControlWordEnableDebugPrintsBit = 2u;
constexpr size_t kBuiltinDebugTextMaxChars = 39u;

struct NativeLogicConfig {
  std::string asynPortName;
  double sampleRateMs {0.0};
};

struct ResolvedItemBinding {
  std::string itemName;
  ecmcNativeLogicItemBinding binding {};
  ecmcDataItem* item {nullptr};
  ecmcDataItemInfo info {};
};

struct ExportedParamBinding {
  std::string name;
  int paramId {-1};
  uint32_t type {0u};
  uint32_t writable {0u};
  void* data {nullptr};
  size_t bytes {0u};
  bool isArray {false};
  asynParamType asynType {asynParamNotDefined};
  std::vector<uint8_t> lastValue;
  bool initialized {false};
};

class NativeLogicAsynPort;

inline bool isEpicsStarted() {
  return allowCallbackEpicsState != 0;
}

double currentCycleTimeS();
void publishCurrentDebugText(const char* message);

uint32_t nativeTypeSize(uint32_t type) {
  switch (type) {
  case ECMC_NATIVE_TYPE_BOOL:
  case ECMC_NATIVE_TYPE_S8:
  case ECMC_NATIVE_TYPE_U8:
    return 1u;
  case ECMC_NATIVE_TYPE_S16:
  case ECMC_NATIVE_TYPE_U16:
    return 2u;
  case ECMC_NATIVE_TYPE_S32:
  case ECMC_NATIVE_TYPE_U32:
  case ECMC_NATIVE_TYPE_F32:
    return 4u;
  case ECMC_NATIVE_TYPE_S64:
  case ECMC_NATIVE_TYPE_U64:
  case ECMC_NATIVE_TYPE_F64:
    return 8u;
  default:
    return 0u;
  }
}

bool nativeTypeMatchesItemType(uint32_t nativeType, ecmcEcDataType itemType) {
  switch (nativeType) {
  case ECMC_NATIVE_TYPE_BOOL:
    return itemType == ECMC_EC_B1 || itemType == ECMC_EC_B2 || itemType == ECMC_EC_B3 ||
           itemType == ECMC_EC_B4 || itemType == ECMC_EC_U8 || itemType == ECMC_EC_S8;
  case ECMC_NATIVE_TYPE_S8:
    return itemType == ECMC_EC_S8 || itemType == ECMC_EC_U8;
  case ECMC_NATIVE_TYPE_U8:
    return itemType == ECMC_EC_U8 || itemType == ECMC_EC_S8 || itemType == ECMC_EC_S8_TO_U8;
  case ECMC_NATIVE_TYPE_S16:
    return itemType == ECMC_EC_S16 || itemType == ECMC_EC_U16;
  case ECMC_NATIVE_TYPE_U16:
    return itemType == ECMC_EC_U16 || itemType == ECMC_EC_S16 || itemType == ECMC_EC_S16_TO_U16;
  case ECMC_NATIVE_TYPE_S32:
    return itemType == ECMC_EC_S32 || itemType == ECMC_EC_U32;
  case ECMC_NATIVE_TYPE_U32:
    return itemType == ECMC_EC_U32 || itemType == ECMC_EC_S32 || itemType == ECMC_EC_S32_TO_U32;
  case ECMC_NATIVE_TYPE_S64:
    return itemType == ECMC_EC_S64 || itemType == ECMC_EC_U64;
  case ECMC_NATIVE_TYPE_U64:
    return itemType == ECMC_EC_U64 || itemType == ECMC_EC_S64 || itemType == ECMC_EC_S64_TO_U64;
  case ECMC_NATIVE_TYPE_F32:
    return itemType == ECMC_EC_F32;
  case ECMC_NATIVE_TYPE_F64:
    return itemType == ECMC_EC_F64;
  default:
    return false;
  }
}

asynParamType exportParamType(uint32_t type, size_t bytes) {
  const uint32_t scalarBytes = nativeTypeSize(type);
  const bool isArray = scalarBytes > 0u && bytes > scalarBytes;

  if (!isArray) {
    switch (type) {
    case ECMC_NATIVE_TYPE_BOOL:
    case ECMC_NATIVE_TYPE_S8:
    case ECMC_NATIVE_TYPE_U8:
    case ECMC_NATIVE_TYPE_S16:
    case ECMC_NATIVE_TYPE_U16:
    case ECMC_NATIVE_TYPE_S32:
    case ECMC_NATIVE_TYPE_U32:
      return asynParamInt32;
    default:
      return asynParamFloat64;
    }
  }

  switch (type) {
  case ECMC_NATIVE_TYPE_BOOL:
  case ECMC_NATIVE_TYPE_S8:
  case ECMC_NATIVE_TYPE_U8:
    return asynParamInt8Array;
  case ECMC_NATIVE_TYPE_S16:
  case ECMC_NATIVE_TYPE_U16:
    return asynParamInt16Array;
  case ECMC_NATIVE_TYPE_S32:
  case ECMC_NATIVE_TYPE_U32:
    return asynParamInt32Array;
  case ECMC_NATIVE_TYPE_F32:
    return asynParamFloat32Array;
  default:
    return asynParamFloat64Array;
  }
}

void trimDebugText(std::string* text) {
  if (!text) {
    return;
  }
  if (text->size() > kBuiltinDebugTextMaxChars) {
    text->resize(kBuiltinDebugTextMaxChars);
  }
}

bool parseConfigString(const char* configStr, NativeLogicConfig* config, std::string* errorOut) {
  if (!config) {
    return false;
  }

  *config = {};
  if (!configStr || !configStr[0]) {
    return true;
  }

  std::string configText(configStr);
  size_t cursor = 0u;
  while (cursor <= configText.size()) {
    const size_t next = configText.find(';', cursor);
    const size_t tokenEnd = next == std::string::npos ? configText.size() : next;
    const std::string token = configText.substr(cursor, tokenEnd - cursor);

    if (!token.empty()) {
      const size_t equals = token.find('=');
      if (equals == std::string::npos) {
        if (errorOut) {
          *errorOut = "Invalid native logic config token: " + token;
        }
        return false;
      }

      const std::string key = token.substr(0, equals);
      const std::string value = token.substr(equals + 1u);

      if (key == "asyn_port") {
        config->asynPortName = value;
      } else if (key == "sample_rate_ms") {
        if (value.empty()) {
          config->sampleRateMs = 0.0;
        } else {
          char* endPtr = nullptr;
          const double parsed = std::strtod(value.c_str(), &endPtr);
          if (!endPtr || *endPtr != '\0' || !std::isfinite(parsed) || parsed < 0.0) {
            if (errorOut) {
              *errorOut = "Invalid sample_rate_ms value: '" + value + "'";
            }
            return false;
          }
          config->sampleRateMs = parsed;
        }
      } else if (errorOut) {
        *errorOut = "Unknown native logic config key: " + key;
        return false;
      } else {
        return false;
      }
    }

    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1u;
  }

  return true;
}

double monotonicTimeMs() {
  struct timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1.0e6;
}

class NativeLogicAsynPort : public asynPortDriver {
 public:
  explicit NativeLogicAsynPort(ecmcNativeLogicLib::Impl* impl);
  ~NativeLogicAsynPort() override;

  bool syncExportedParams(std::vector<ExportedParamBinding>* bindings, bool force, bool deferCallbacks);
  bool syncOctetParam(int paramId,
                      const std::string& value,
                      std::string* lastValue,
                      bool* initialized,
                      bool force,
                      bool deferCallbacks);

  asynStatus writeInt32(asynUser* pasynUser, epicsInt32 value) override;
  asynStatus writeFloat64(asynUser* pasynUser, epicsFloat64 value) override;
  asynStatus writeInt8Array(asynUser* pasynUser, epicsInt8* value, size_t nElements) override;
  asynStatus readInt8Array(asynUser* pasynUser, epicsInt8* value, size_t nElements, size_t* nIn) override;
  asynStatus writeInt16Array(asynUser* pasynUser, epicsInt16* value, size_t nElements) override;
  asynStatus readInt16Array(asynUser* pasynUser, epicsInt16* value, size_t nElements, size_t* nIn) override;
  asynStatus writeInt32Array(asynUser* pasynUser, epicsInt32* value, size_t nElements) override;
  asynStatus readInt32Array(asynUser* pasynUser, epicsInt32* value, size_t nElements, size_t* nIn) override;
  asynStatus writeFloat32Array(asynUser* pasynUser, epicsFloat32* value, size_t nElements) override;
  asynStatus readFloat32Array(asynUser* pasynUser, epicsFloat32* value, size_t nElements, size_t* nIn) override;
  asynStatus writeFloat64Array(asynUser* pasynUser, epicsFloat64* value, size_t nElements) override;
  asynStatus readFloat64Array(asynUser* pasynUser, epicsFloat64* value, size_t nElements, size_t* nIn) override;

 private:
  ExportedParamBinding* paramBindingForReason(int reason);
  bool syncOneParam(ExportedParamBinding* binding, bool force);
  bool pushArrayCallback(const ExportedParamBinding* binding);
  bool writeScalarFromInt32(ExportedParamBinding* binding, epicsInt32 value);
  bool writeScalarFromFloat64(ExportedParamBinding* binding, epicsFloat64 value);
  void scheduleCallbacks(bool withScalarCallbacks);
  void callbackWorker();
  void fireArrayCallbacks(const std::vector<int>& paramIds);
  template <typename SrcT, typename DstT>
  asynStatus writeTypedArray(asynUser* pasynUser, SrcT* value, size_t nElements);
  template <typename SrcT, typename DstT>
  asynStatus readTypedArray(asynUser* pasynUser, SrcT* value, size_t nElements, size_t* nIn);
  template <typename IntT>
  asynStatus readInt64BackedAsFloat64Array(asynUser* pasynUser,
                                           epicsFloat64* value,
                                           size_t nElements,
                                           size_t* nIn);
  template <typename IntT>
  asynStatus writeFloat64AsInt64BackedArray(asynUser* pasynUser,
                                            epicsFloat64* value,
                                            size_t nElements);
  static void lowerCurrentThreadPriority();

  ecmcNativeLogicLib::Impl* impl_;
  std::mutex callbackMutex_;
  std::condition_variable callbackCv_;
  std::thread callbackThread_;
  bool callbackPending_ {false};
  bool scalarCallbackPending_ {false};
  bool stopCallbackThread_ {false};
  std::vector<int> pendingArrayParamIds_;
};

}  // namespace

struct ecmcNativeLogicLib::Impl {
  explicit Impl(ecmcNativeLogicLib* ownerIn, int indexIn)
    : owner(ownerIn), index(indexIn) {}

  ecmcNativeLogicLib* owner {nullptr};
  int index {0};
  std::string libFilename;
  std::string configString;
  std::string logicName;
  NativeLogicConfig config {};
  void* dlHandle {nullptr};
  const ecmcNativeLogicApi* api {nullptr};
  void* instance {nullptr};
  NativeLogicAsynPort* asynPort {nullptr};
  std::vector<ResolvedItemBinding> itemBindings;
  std::vector<ExportedParamBinding> builtinParams;
  std::vector<ExportedParamBinding> exportedParams;
  ecmcNativeLogicHostServices hostServices {};
  bool loaded {false};
  bool enteredRt {false};
  uint32_t controlWord {1u};
  uint8_t executionEnabled {1u};
  uint8_t timingEnabled {0u};
  uint8_t debugEnabled {0u};
  double requestedSampleRateMs {0.0};
  double actualSampleRateMs {0.0};
  size_t runtimeExecuteDivider {1u};
  size_t executeDividerCounter {0u};
  double lastExecTimeMs {0.0};
  double lastInputTimeMs {0.0};
  double lastOutputTimeMs {0.0};
  double lastTotalTimeMs {0.0};
  int32_t executeDividerPv {1};
  int32_t executeCount {0};
  size_t executeCountPublishDivider {1u};
  size_t executeCountPublishCounter {0u};
  bool executeCountPublishDue {false};
  int debugTextParamId {-1};
  std::string debugText;
  std::string debugTextLastValue;
  bool debugTextInitialized {false};
};

namespace {

thread_local ecmcNativeLogicLib::Impl* g_activeNativeLogic = nullptr;

double currentCycleTimeS() {
  if (g_activeNativeLogic) {
    return g_activeNativeLogic->actualSampleRateMs / 1000.0;
  }
  return getEcmcSampleTimeMS() * 1e-3;
}

void publishCurrentDebugText(const char* message) {
  if (!g_activeNativeLogic) {
    if (message && message[0]) {
      LOGINFO("[ecmc native] %s\n", message);
    }
    return;
  }

  ecmcNativeLogicLib::Impl* impl = g_activeNativeLogic;
  if ((impl->controlWord & (1u << kControlWordEnableDebugPrintsBit)) == 0u) {
    return;
  }

  impl->debugText.assign(message ? message : "");
  trimDebugText(&impl->debugText);
  if (!impl->debugText.empty()) {
    LOGINFO("[ecmc native %s] %s\n",
            impl->logicName.empty() ? "logic" : impl->logicName.c_str(),
            impl->debugText.c_str());
  }
}

bool applyRuntimeSampleRateMs(ecmcNativeLogicLib::Impl* impl,
                              double requestedMs,
                              std::string* errorOut) {
  if (!impl) {
    return false;
  }

  const double baseSampleMs = getEcmcSampleTimeMS();
  if (!std::isfinite(baseSampleMs) || baseSampleMs <= 0.0) {
    if (errorOut) {
      *errorOut = "Invalid ECMC base cycle time.";
    }
    return false;
  }

  if (!std::isfinite(requestedMs) || requestedMs < 0.0) {
    if (errorOut) {
      *errorOut = "Invalid sample rate.";
    }
    return false;
  }

  size_t divider = 1u;
  if (requestedMs > 0.0) {
    divider = static_cast<size_t>(std::llround(requestedMs / baseSampleMs));
    if (divider < 1u) {
      divider = 1u;
    }
  }

  impl->requestedSampleRateMs = requestedMs;
  impl->runtimeExecuteDivider = divider;
  impl->actualSampleRateMs = baseSampleMs * static_cast<double>(divider);
  impl->executeDividerPv = static_cast<int32_t>(divider);

  size_t publishDivider =
    static_cast<size_t>(std::llround(std::max(1.0, 100.0 / impl->actualSampleRateMs)));
  if (publishDivider < 1u) {
    publishDivider = 1u;
  }
  impl->executeCountPublishDivider = publishDivider;
  impl->executeCountPublishCounter = 0u;
  impl->executeCountPublishDue = true;
  return true;
}

bool addBoundParam(NativeLogicAsynPort* port,
                   const char* name,
                   uint32_t type,
                   uint32_t writable,
                   void* data,
                   size_t bytes,
                   std::vector<ExportedParamBinding>* out,
                   std::string* errorOut) {
  if (!port || !name || !data || bytes == 0u || !out) {
    return false;
  }

  ExportedParamBinding binding {};
  binding.name = name;
  binding.type = type;
  binding.writable = writable;
  binding.data = data;
  binding.bytes = bytes;
  binding.isArray = bytes > nativeTypeSize(type);
  binding.asynType = exportParamType(type, bytes);

  if (port->createParam(binding.name.c_str(), binding.asynType, &binding.paramId) != asynSuccess) {
    if (errorOut) {
      *errorOut = "Failed to create asyn parameter: " + binding.name;
    }
    return false;
  }

  out->push_back(binding);
  return true;
}

bool copyItemToBinding(const ResolvedItemBinding& binding) {
  if (!binding.binding.data || !binding.info.data) {
    return false;
  }
  std::memcpy(binding.binding.data, binding.info.data, binding.binding.bytes);
  return true;
}

bool copyBindingToItem(const ResolvedItemBinding& binding) {
  if (!binding.binding.data || !binding.info.data) {
    return false;
  }
  std::memcpy(binding.info.data, binding.binding.data, binding.binding.bytes);
  return true;
}

}  // namespace

NativeLogicAsynPort::NativeLogicAsynPort(ecmcNativeLogicLib::Impl* impl)
  : asynPortDriver(impl && !impl->config.asynPortName.empty() ? impl->config.asynPortName.c_str()
                                                              : "NATIVE.LOGIC",
                   1,
                   asynInt32Mask | asynFloat64Mask | asynOctetMask | asynDrvUserMask |
                     asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask |
                     asynFloat32ArrayMask | asynFloat64ArrayMask,
                   asynInt32Mask | asynFloat64Mask | asynOctetMask | asynDrvUserMask |
                     asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask |
                     asynFloat32ArrayMask | asynFloat64ArrayMask,
                   0,
                   1,
                   0,
                   0),
    impl_(impl) {
  callbackThread_ = std::thread([this]() { callbackWorker(); });
}

NativeLogicAsynPort::~NativeLogicAsynPort() {
  {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stopCallbackThread_ = true;
    callbackPending_ = true;
  }
  callbackCv_.notify_one();
  if (callbackThread_.joinable()) {
    callbackThread_.join();
  }
}

void NativeLogicAsynPort::lowerCurrentThreadPriority() {
#if defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#elif defined(__linux__)
  sched_param sched {};
  pthread_setschedparam(pthread_self(), SCHED_OTHER, &sched);
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

ExportedParamBinding* NativeLogicAsynPort::paramBindingForReason(int reason) {
  if (!impl_) {
    return nullptr;
  }

  for (auto& binding : impl_->builtinParams) {
    if (binding.paramId == reason) {
      return &binding;
    }
  }
  for (auto& binding : impl_->exportedParams) {
    if (binding.paramId == reason) {
      return &binding;
    }
  }
  return nullptr;
}

bool NativeLogicAsynPort::syncOctetParam(int paramId,
                                         const std::string& value,
                                         std::string* lastValue,
                                         bool* initialized,
                                         bool force,
                                         bool deferCallbacks) {
  if (paramId < 0 || !lastValue || !initialized) {
    return false;
  }

  if (!force && *initialized && *lastValue == value) {
    return true;
  }

  setStringParam(paramId, value.c_str());
  *lastValue = value;
  *initialized = true;

  if (deferCallbacks) {
    scheduleCallbacks(true);
  } else if (isEpicsStarted()) {
    callParamCallbacks();
  }

  return true;
}

bool NativeLogicAsynPort::pushArrayCallback(const ExportedParamBinding* binding) {
  if (!binding || !binding->isArray) {
    return false;
  }
  std::lock_guard<std::mutex> lock(callbackMutex_);
  if (std::find(pendingArrayParamIds_.begin(), pendingArrayParamIds_.end(), binding->paramId) ==
      pendingArrayParamIds_.end()) {
    pendingArrayParamIds_.push_back(binding->paramId);
  }
  return true;
}

void NativeLogicAsynPort::scheduleCallbacks(bool withScalarCallbacks) {
  if (!isEpicsStarted()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackPending_ = true;
    scalarCallbackPending_ = scalarCallbackPending_ || withScalarCallbacks;
  }
  callbackCv_.notify_one();
}

void NativeLogicAsynPort::fireArrayCallbacks(const std::vector<int>& paramIds) {
  if (!impl_) {
    return;
  }

  for (int paramId : paramIds) {
    ExportedParamBinding* binding = paramBindingForReason(paramId);
    if (!binding || !binding->data || !binding->isArray) {
      continue;
    }

    const size_t elementSize = std::max<size_t>(1u, nativeTypeSize(binding->type));
    const size_t elementCount = binding->bytes / elementSize;

    switch (binding->type) {
    case ECMC_NATIVE_TYPE_BOOL:
    case ECMC_NATIVE_TYPE_S8:
    case ECMC_NATIVE_TYPE_U8:
      doCallbacksInt8Array(static_cast<epicsInt8*>(binding->data), elementCount, binding->paramId, 0);
      break;
    case ECMC_NATIVE_TYPE_S16:
    case ECMC_NATIVE_TYPE_U16:
      doCallbacksInt16Array(static_cast<epicsInt16*>(binding->data), elementCount, binding->paramId, 0);
      break;
    case ECMC_NATIVE_TYPE_S32:
    case ECMC_NATIVE_TYPE_U32:
      doCallbacksInt32Array(static_cast<epicsInt32*>(binding->data), elementCount, binding->paramId, 0);
      break;
    case ECMC_NATIVE_TYPE_F32:
      doCallbacksFloat32Array(static_cast<epicsFloat32*>(binding->data), elementCount, binding->paramId, 0);
      break;
    case ECMC_NATIVE_TYPE_F64:
      doCallbacksFloat64Array(static_cast<epicsFloat64*>(binding->data), elementCount, binding->paramId, 0);
      break;
    case ECMC_NATIVE_TYPE_S64: {
      std::vector<epicsFloat64> values(elementCount);
      const auto* source = static_cast<const int64_t*>(binding->data);
      for (size_t i = 0; i < elementCount; ++i) {
        values[i] = static_cast<epicsFloat64>(source[i]);
      }
      doCallbacksFloat64Array(values.data(), elementCount, binding->paramId, 0);
      break;
    }
    case ECMC_NATIVE_TYPE_U64: {
      std::vector<epicsFloat64> values(elementCount);
      const auto* source = static_cast<const uint64_t*>(binding->data);
      for (size_t i = 0; i < elementCount; ++i) {
        values[i] = static_cast<epicsFloat64>(source[i]);
      }
      doCallbacksFloat64Array(values.data(), elementCount, binding->paramId, 0);
      break;
    }
    default:
      break;
    }
  }
}

void NativeLogicAsynPort::callbackWorker() {
  lowerCurrentThreadPriority();
  std::unique_lock<std::mutex> lock(callbackMutex_);
  for (;;) {
    callbackCv_.wait(lock, [this]() { return callbackPending_ || stopCallbackThread_; });
    if (stopCallbackThread_) {
      return;
    }

    const bool doScalarCallbacks = scalarCallbackPending_;
    std::vector<int> paramIds;
    paramIds.swap(pendingArrayParamIds_);
    callbackPending_ = false;
    scalarCallbackPending_ = false;

    lock.unlock();
    if (doScalarCallbacks) {
      callParamCallbacks();
    }
    if (!paramIds.empty()) {
      fireArrayCallbacks(paramIds);
    }
    lock.lock();
  }
}

bool NativeLogicAsynPort::syncOneParam(ExportedParamBinding* binding, bool force) {
  if (!binding || binding->paramId < 0 || !binding->data || binding->bytes == 0u) {
    return false;
  }

  if (!force && binding->name == kBuiltinExecuteCountName && !impl_->executeCountPublishDue) {
    return false;
  }

  const auto* current = static_cast<const uint8_t*>(binding->data);
  if (!force && binding->initialized && binding->lastValue.size() == binding->bytes &&
      std::memcmp(binding->lastValue.data(), current, binding->bytes) == 0) {
    return false;
  }

  binding->lastValue.assign(current, current + binding->bytes);
  binding->initialized = true;

  if (binding->isArray) {
    pushArrayCallback(binding);
    return true;
  }

  switch (binding->type) {
  case ECMC_NATIVE_TYPE_BOOL:
    setIntegerParam(binding->paramId, current[0] != 0u ? 1 : 0);
    return true;
  case ECMC_NATIVE_TYPE_S8: {
    int8_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_U8: {
    uint8_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_S16: {
    int16_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_U16: {
    uint16_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_S32: {
    int32_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_U32: {
    uint32_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setIntegerParam(binding->paramId, static_cast<epicsInt32>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_F32: {
    float value = 0.0f;
    std::memcpy(&value, current, sizeof(value));
    setDoubleParam(binding->paramId, static_cast<epicsFloat64>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_F64: {
    double value = 0.0;
    std::memcpy(&value, current, sizeof(value));
    setDoubleParam(binding->paramId, static_cast<epicsFloat64>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_S64: {
    int64_t value = 0;
    std::memcpy(&value, current, sizeof(value));
    setDoubleParam(binding->paramId, static_cast<epicsFloat64>(value));
    return true;
  }
  case ECMC_NATIVE_TYPE_U64: {
    uint64_t value = 0u;
    std::memcpy(&value, current, sizeof(value));
    setDoubleParam(binding->paramId, static_cast<epicsFloat64>(value));
    return true;
  }
  default:
    return false;
  }
}

bool NativeLogicAsynPort::syncExportedParams(std::vector<ExportedParamBinding>* bindings,
                                             bool force,
                                             bool deferCallbacks) {
  if (!bindings) {
    return false;
  }

  bool anyScalarChange = false;
  bool anyArrayChange = false;
  for (auto& binding : *bindings) {
    if (syncOneParam(&binding, force)) {
      anyScalarChange = anyScalarChange || !binding.isArray;
      anyArrayChange = anyArrayChange || binding.isArray;
    }
  }

  if (anyScalarChange || anyArrayChange) {
    if (deferCallbacks) {
      scheduleCallbacks(anyScalarChange);
    } else if (isEpicsStarted()) {
      if (anyScalarChange) {
        callParamCallbacks();
      }
      if (anyArrayChange) {
        std::vector<int> paramIds;
        for (const auto& binding : *bindings) {
          if (binding.isArray) {
            paramIds.push_back(binding.paramId);
          }
        }
        fireArrayCallbacks(paramIds);
      }
    }
  }

  return true;
}

bool NativeLogicAsynPort::writeScalarFromInt32(ExportedParamBinding* binding, epicsInt32 value) {
  if (!binding || !binding->data || binding->bytes == 0u) {
    return false;
  }

  switch (binding->type) {
  case ECMC_NATIVE_TYPE_BOOL: {
    const uint8_t typed = value != 0 ? 1u : 0u;
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_S8: {
    const int8_t typed = static_cast<int8_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_U8: {
    const uint8_t typed = static_cast<uint8_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_S16: {
    const int16_t typed = static_cast<int16_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_U16: {
    const uint16_t typed = static_cast<uint16_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_S32: {
    const int32_t typed = static_cast<int32_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_U32: {
    const uint32_t typed = static_cast<uint32_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  default:
    return false;
  }

  binding->initialized = false;
  return true;
}

bool NativeLogicAsynPort::writeScalarFromFloat64(ExportedParamBinding* binding, epicsFloat64 value) {
  if (!binding || !binding->data || binding->bytes == 0u) {
    return false;
  }

  switch (binding->type) {
  case ECMC_NATIVE_TYPE_F32: {
    const float typed = static_cast<float>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_F64: {
    const double typed = static_cast<double>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_S64: {
    const int64_t typed = static_cast<int64_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  case ECMC_NATIVE_TYPE_U64: {
    const uint64_t typed = static_cast<uint64_t>(value);
    std::memcpy(binding->data, &typed, sizeof(typed));
    break;
  }
  default:
    return false;
  }

  binding->initialized = false;
  return true;
}

asynStatus NativeLogicAsynPort::writeInt32(asynUser* pasynUser, epicsInt32 value) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || binding->writable == 0u) {
    return asynError;
  }

  if (binding->name == kBuiltinControlWordName) {
    impl_->controlWord = static_cast<uint32_t>(value);
    impl_->executionEnabled =
      ((impl_->controlWord >> kControlWordEnableExecutionBit) & 0x1u) ? 1u : 0u;
    impl_->timingEnabled =
      ((impl_->controlWord >> kControlWordEnableTimingBit) & 0x1u) ? 1u : 0u;
    impl_->debugEnabled =
      ((impl_->controlWord >> kControlWordEnableDebugPrintsBit) & 0x1u) ? 1u : 0u;
    if (!impl_->timingEnabled) {
      impl_->lastExecTimeMs = 0.0;
      impl_->lastInputTimeMs = 0.0;
      impl_->lastOutputTimeMs = 0.0;
      impl_->lastTotalTimeMs = 0.0;
    }
    binding->initialized = false;
    syncExportedParams(&impl_->builtinParams, false, false);
    if (impl_->debugTextParamId >= 0) {
      syncOctetParam(impl_->debugTextParamId,
                     impl_->debugEnabled ? impl_->debugText : std::string(),
                     &impl_->debugTextLastValue,
                     &impl_->debugTextInitialized,
                     false,
                     false);
    }
    return asynSuccess;
  }

  if (!writeScalarFromInt32(binding, value)) {
    return asynError;
  }
  syncExportedParams(&impl_->exportedParams, false, false);
  return asynSuccess;
}

asynStatus NativeLogicAsynPort::writeFloat64(asynUser* pasynUser, epicsFloat64 value) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || binding->writable == 0u) {
    return asynError;
  }

  if (binding->name == kBuiltinRequestedRateMsName) {
    std::string error;
    if (!applyRuntimeSampleRateMs(impl_, static_cast<double>(value), &error)) {
      LOGERR("%s/%s:%d: %s\n", __FILE__, __FUNCTION__, __LINE__, error.c_str());
      return asynError;
    }
    for (auto& builtin : impl_->builtinParams) {
      builtin.initialized = false;
    }
    syncExportedParams(&impl_->builtinParams, false, false);
    return asynSuccess;
  }

  if (!writeScalarFromFloat64(binding, value)) {
    return asynError;
  }
  syncExportedParams(&impl_->exportedParams, false, false);
  return asynSuccess;
}

template <typename SrcT, typename DstT>
asynStatus NativeLogicAsynPort::writeTypedArray(asynUser* pasynUser, SrcT* value, size_t nElements) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || binding->writable == 0u || !binding->data || !binding->isArray) {
    return asynError;
  }

  const size_t elementCount = binding->bytes / sizeof(DstT);
  const size_t toCopy = std::min(elementCount, nElements);
  auto* dest = static_cast<DstT*>(binding->data);
  for (size_t i = 0; i < toCopy; ++i) {
    dest[i] = static_cast<DstT>(value[i]);
  }
  binding->initialized = false;
  syncExportedParams(&impl_->exportedParams, false, false);
  return asynSuccess;
}

template <typename SrcT, typename DstT>
asynStatus NativeLogicAsynPort::readTypedArray(asynUser* pasynUser,
                                               SrcT* value,
                                               size_t nElements,
                                               size_t* nIn) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || !binding->data || !binding->isArray) {
    return asynError;
  }

  const size_t elementCount = binding->bytes / sizeof(DstT);
  const size_t toCopy = std::min(elementCount, nElements);
  const auto* source = static_cast<const DstT*>(binding->data);
  for (size_t i = 0; i < toCopy; ++i) {
    value[i] = static_cast<SrcT>(source[i]);
  }
  if (nIn) {
    *nIn = toCopy;
  }
  return asynSuccess;
}

template <typename IntT>
asynStatus NativeLogicAsynPort::readInt64BackedAsFloat64Array(asynUser* pasynUser,
                                                              epicsFloat64* value,
                                                              size_t nElements,
                                                              size_t* nIn) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || !binding->data || !binding->isArray) {
    return asynError;
  }

  const size_t elementCount = binding->bytes / sizeof(IntT);
  const size_t toCopy = std::min(elementCount, nElements);
  const auto* source = static_cast<const IntT*>(binding->data);
  for (size_t i = 0; i < toCopy; ++i) {
    value[i] = static_cast<epicsFloat64>(source[i]);
  }
  if (nIn) {
    *nIn = toCopy;
  }
  return asynSuccess;
}

template <typename IntT>
asynStatus NativeLogicAsynPort::writeFloat64AsInt64BackedArray(asynUser* pasynUser,
                                                               epicsFloat64* value,
                                                               size_t nElements) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding || binding->writable == 0u || !binding->data || !binding->isArray) {
    return asynError;
  }

  const size_t elementCount = binding->bytes / sizeof(IntT);
  const size_t toCopy = std::min(elementCount, nElements);
  auto* dest = static_cast<IntT*>(binding->data);
  for (size_t i = 0; i < toCopy; ++i) {
    dest[i] = static_cast<IntT>(value[i]);
  }
  binding->initialized = false;
  syncExportedParams(&impl_->exportedParams, false, false);
  return asynSuccess;
}

asynStatus NativeLogicAsynPort::writeInt8Array(asynUser* pasynUser, epicsInt8* value, size_t nElements) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding) {
    return asynError;
  }
  switch (binding->type) {
  case ECMC_NATIVE_TYPE_BOOL:
  case ECMC_NATIVE_TYPE_U8:
    return writeTypedArray<epicsInt8, uint8_t>(pasynUser, value, nElements);
  case ECMC_NATIVE_TYPE_S8:
    return writeTypedArray<epicsInt8, int8_t>(pasynUser, value, nElements);
  default:
    return asynError;
  }
}

asynStatus NativeLogicAsynPort::readInt8Array(asynUser* pasynUser,
                                              epicsInt8* value,
                                              size_t nElements,
                                              size_t* nIn) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding) {
    return asynError;
  }
  switch (binding->type) {
  case ECMC_NATIVE_TYPE_BOOL:
  case ECMC_NATIVE_TYPE_U8:
    return readTypedArray<epicsInt8, uint8_t>(pasynUser, value, nElements, nIn);
  case ECMC_NATIVE_TYPE_S8:
    return readTypedArray<epicsInt8, int8_t>(pasynUser, value, nElements, nIn);
  default:
    return asynError;
  }
}

asynStatus NativeLogicAsynPort::writeInt16Array(asynUser* pasynUser, epicsInt16* value, size_t nElements) {
  return writeTypedArray<epicsInt16, epicsInt16>(pasynUser, value, nElements);
}

asynStatus NativeLogicAsynPort::readInt16Array(asynUser* pasynUser,
                                               epicsInt16* value,
                                               size_t nElements,
                                               size_t* nIn) {
  return readTypedArray<epicsInt16, epicsInt16>(pasynUser, value, nElements, nIn);
}

asynStatus NativeLogicAsynPort::writeInt32Array(asynUser* pasynUser, epicsInt32* value, size_t nElements) {
  return writeTypedArray<epicsInt32, epicsInt32>(pasynUser, value, nElements);
}

asynStatus NativeLogicAsynPort::readInt32Array(asynUser* pasynUser,
                                               epicsInt32* value,
                                               size_t nElements,
                                               size_t* nIn) {
  return readTypedArray<epicsInt32, epicsInt32>(pasynUser, value, nElements, nIn);
}

asynStatus NativeLogicAsynPort::writeFloat32Array(asynUser* pasynUser,
                                                  epicsFloat32* value,
                                                  size_t nElements) {
  return writeTypedArray<epicsFloat32, epicsFloat32>(pasynUser, value, nElements);
}

asynStatus NativeLogicAsynPort::readFloat32Array(asynUser* pasynUser,
                                                 epicsFloat32* value,
                                                 size_t nElements,
                                                 size_t* nIn) {
  return readTypedArray<epicsFloat32, epicsFloat32>(pasynUser, value, nElements, nIn);
}

asynStatus NativeLogicAsynPort::writeFloat64Array(asynUser* pasynUser,
                                                  epicsFloat64* value,
                                                  size_t nElements) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding) {
    return asynError;
  }

  switch (binding->type) {
  case ECMC_NATIVE_TYPE_F64:
    return writeTypedArray<epicsFloat64, epicsFloat64>(pasynUser, value, nElements);
  case ECMC_NATIVE_TYPE_F32:
    return writeTypedArray<epicsFloat64, epicsFloat32>(pasynUser, value, nElements);
  case ECMC_NATIVE_TYPE_S64:
    return writeFloat64AsInt64BackedArray<int64_t>(pasynUser, value, nElements);
  case ECMC_NATIVE_TYPE_U64:
    return writeFloat64AsInt64BackedArray<uint64_t>(pasynUser, value, nElements);
  default:
    return asynError;
  }
}

asynStatus NativeLogicAsynPort::readFloat64Array(asynUser* pasynUser,
                                                 epicsFloat64* value,
                                                 size_t nElements,
                                                 size_t* nIn) {
  ExportedParamBinding* binding = paramBindingForReason(pasynUser ? pasynUser->reason : -1);
  if (!binding) {
    return asynError;
  }

  switch (binding->type) {
  case ECMC_NATIVE_TYPE_F64:
    return readTypedArray<epicsFloat64, epicsFloat64>(pasynUser, value, nElements, nIn);
  case ECMC_NATIVE_TYPE_F32:
    return readTypedArray<epicsFloat64, epicsFloat32>(pasynUser, value, nElements, nIn);
  case ECMC_NATIVE_TYPE_S64:
    return readInt64BackedAsFloat64Array<int64_t>(pasynUser, value, nElements, nIn);
  case ECMC_NATIVE_TYPE_U64:
    return readInt64BackedAsFloat64Array<uint64_t>(pasynUser, value, nElements, nIn);
  default:
    return asynError;
  }
}

ecmcNativeLogicLib::ecmcNativeLogicLib(int index)
  : impl_(new Impl(this, index)) {}

ecmcNativeLogicLib::~ecmcNativeLogicLib() {
  unload();
  delete impl_;
  impl_ = nullptr;
}

const char* ecmcNativeLogicLib::getPortName() const {
  return impl_ ? impl_->config.asynPortName.c_str() : "";
}

int ecmcNativeLogicLib::load(const char* libFilenameWP, const char* configStr) {
  if (!impl_ || !libFilenameWP || !libFilenameWP[0]) {
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_FILENAME_EMPTY);
  }

  errorReset();
  unload();

  impl_->libFilename = libFilenameWP;
  impl_->configString = configStr ? configStr : "";

  std::string error;
  if (!parseConfigString(configStr, &impl_->config, &error)) {
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_CONFIG_INVALID);
  }

  if (impl_->config.asynPortName.empty()) {
    char portName[64];
    std::snprintf(portName, sizeof(portName), "%s%d", kDefaultAsynPortBase, impl_->index);
    impl_->config.asynPortName = portName;
  }

  impl_->dlHandle = dlopen(impl_->libFilename.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!impl_->dlHandle) {
    LOGERR("%s/%s:%d: Native logic %s open failed: %s\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           impl_->libFilename.c_str(),
           dlerror());
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_OPEN_FAIL);
  }

  auto* getApi =
    reinterpret_cast<const ecmcNativeLogicApi* (*)()>(dlsym(impl_->dlHandle, kNativeLogicGetApiSymbol));
  if (!getApi) {
    LOGERR("%s/%s:%d: Native logic %s missing %s.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           impl_->libFilename.c_str(),
           kNativeLogicGetApiSymbol);
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_GET_API_FAIL);
  }

  impl_->api = getApi();
  if (!impl_->api) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_API_NULL);
  }
  if (impl_->api->abiVersion != ECMC_NATIVE_LOGIC_ABI_VERSION) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_ABI_MISMATCH);
  }

  getEcmcNativeLogicHostServices(&impl_->hostServices);
  impl_->hostServices.version = ECMC_NATIVE_LOGIC_ABI_VERSION;
  impl_->hostServices.get_cycle_time_s = &currentCycleTimeS;
  impl_->hostServices.publish_debug_text = &publishCurrentDebugText;
  if (impl_->api->setHostServices) {
    impl_->api->setHostServices(&impl_->hostServices);
  }

  if (!impl_->api->createInstance || !impl_->api->runCycle || !impl_->api->destroyInstance) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_API_NULL);
  }

  impl_->instance = impl_->api->createInstance();
  if (!impl_->instance) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_CREATE_INSTANCE_FAIL);
  }

  const ecmcNativeLogicItemBinding* itemBindings =
    impl_->api->getItemBindings ? impl_->api->getItemBindings(impl_->instance) : nullptr;
  const uint32_t itemBindingCount =
    impl_->api->getItemBindingCount ? impl_->api->getItemBindingCount(impl_->instance) : 0u;
  impl_->itemBindings.clear();
  impl_->itemBindings.reserve(itemBindingCount);

  for (uint32_t i = 0u; i < itemBindingCount; ++i) {
    ResolvedItemBinding resolved {};
    resolved.itemName = itemBindings[i].itemName ? itemBindings[i].itemName : "";
    resolved.binding = itemBindings[i];
    resolved.item = asynPort ? asynPort->findAvailDataItem(resolved.itemName.c_str()) : nullptr;
    if (!resolved.item) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_ITEM_MISSING);
    }

    ecmcDataItemInfo* info = resolved.item->getDataItemInfo();
    if (!info || !info->dataPointerValid || !info->data) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_ITEM_INVALID);
    }
    resolved.info = *info;

    if ((resolved.binding.flags & ECMC_NATIVE_BIND_FLAG_AUTO_SIZE) != 0u) {
      if (!resolved.binding.prepare ||
          resolved.binding.prepare(&resolved.binding, static_cast<uint32_t>(resolved.info.dataSize)) != 0) {
        unload();
        return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_PREPARE_FAIL);
      }
    }

    const uint32_t elementSize = nativeTypeSize(resolved.binding.type);
    if (!resolved.binding.data || resolved.binding.bytes == 0u || elementSize == 0u) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_ITEM_INVALID);
    }
    if (resolved.binding.bytes != resolved.info.dataSize ||
        resolved.info.dataElementSize != elementSize ||
        !nativeTypeMatchesItemType(resolved.binding.type, resolved.info.dataType)) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_ITEM_TYPE_MISMATCH);
    }
    if (resolved.binding.writable != 0u && resolved.info.dataDirection != ECMC_DIR_WRITE) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_DIRECTION_MISMATCH);
    }
    if (resolved.binding.writable == 0u && resolved.info.dataDirection != ECMC_DIR_READ) {
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_BIND_DIRECTION_MISMATCH);
    }

    impl_->itemBindings.push_back(resolved);
  }

  impl_->logicName = (impl_->api->name && impl_->api->name[0]) ? impl_->api->name : impl_->libFilename;
  impl_->asynPort = new NativeLogicAsynPort(impl_);
  if (!impl_->asynPort) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_ASYN_CREATE_FAIL);
  }

  impl_->builtinParams.clear();
  impl_->exportedParams.clear();

  if (!addBoundParam(impl_->asynPort,
                     kBuiltinControlWordName,
                     ECMC_NATIVE_TYPE_U32,
                     1u,
                     &impl_->controlWord,
                     sizeof(impl_->controlWord),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinRequestedRateMsName,
                     ECMC_NATIVE_TYPE_F64,
                     1u,
                     &impl_->requestedSampleRateMs,
                     sizeof(impl_->requestedSampleRateMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinActualRateMsName,
                     ECMC_NATIVE_TYPE_F64,
                     0u,
                     &impl_->actualSampleRateMs,
                     sizeof(impl_->actualSampleRateMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinExecMsName,
                     ECMC_NATIVE_TYPE_F64,
                     0u,
                     &impl_->lastExecTimeMs,
                     sizeof(impl_->lastExecTimeMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinInputMsName,
                     ECMC_NATIVE_TYPE_F64,
                     0u,
                     &impl_->lastInputTimeMs,
                     sizeof(impl_->lastInputTimeMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinOutputMsName,
                     ECMC_NATIVE_TYPE_F64,
                     0u,
                     &impl_->lastOutputTimeMs,
                     sizeof(impl_->lastOutputTimeMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinTotalMsName,
                     ECMC_NATIVE_TYPE_F64,
                     0u,
                     &impl_->lastTotalTimeMs,
                     sizeof(impl_->lastTotalTimeMs),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinDividerName,
                     ECMC_NATIVE_TYPE_S32,
                     0u,
                     &impl_->executeDividerPv,
                     sizeof(impl_->executeDividerPv),
                     &impl_->builtinParams,
                     &error) ||
      !addBoundParam(impl_->asynPort,
                     kBuiltinExecuteCountName,
                     ECMC_NATIVE_TYPE_S32,
                     0u,
                     &impl_->executeCount,
                     sizeof(impl_->executeCount),
                     &impl_->builtinParams,
                     &error)) {
    LOGERR("%s/%s:%d: %s\n", __FILE__, __FUNCTION__, __LINE__, error.c_str());
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_ASYN_CREATE_FAIL);
  }

  if (impl_->asynPort->createParam(kBuiltinDebugTextName, asynParamOctet, &impl_->debugTextParamId) !=
      asynSuccess) {
    unload();
    return setErrorID(ERROR_MAIN_NATIVE_LOGIC_ASYN_CREATE_FAIL);
  }

  const ecmcNativeLogicExportedVar* exportedVars =
    impl_->api->getExportedVars ? impl_->api->getExportedVars(impl_->instance) : nullptr;
  const uint32_t exportedVarCount =
    impl_->api->getExportedVarCount ? impl_->api->getExportedVarCount(impl_->instance) : 0u;
  impl_->exportedParams.reserve(exportedVarCount);
  for (uint32_t i = 0u; i < exportedVarCount; ++i) {
    if (!addBoundParam(impl_->asynPort,
                       exportedVars[i].name,
                       exportedVars[i].type,
                       exportedVars[i].writable,
                       exportedVars[i].data,
                       exportedVars[i].bytes,
                       &impl_->exportedParams,
                       &error)) {
      LOGERR("%s/%s:%d: %s\n", __FILE__, __FUNCTION__, __LINE__, error.c_str());
      unload();
      return setErrorID(ERROR_MAIN_NATIVE_LOGIC_ASYN_CREATE_FAIL);
    }
  }

  applyRuntimeSampleRateMs(impl_, impl_->config.sampleRateMs, nullptr);
  impl_->loaded = true;
  impl_->enteredRt = false;
  impl_->debugText.clear();
  impl_->debugTextLastValue.clear();
  impl_->debugTextInitialized = false;

  impl_->asynPort->syncExportedParams(&impl_->builtinParams, true, false);
  impl_->asynPort->syncExportedParams(&impl_->exportedParams, true, false);
  impl_->asynPort->syncOctetParam(impl_->debugTextParamId,
                                  std::string(),
                                  &impl_->debugTextLastValue,
                                  &impl_->debugTextInitialized,
                                  true,
                                  false);
  return 0;
}

void ecmcNativeLogicLib::unload() {
  if (!impl_) {
    return;
  }

  impl_->enteredRt = false;
  impl_->loaded = false;

  if (impl_->asynPort) {
    delete impl_->asynPort;
    impl_->asynPort = nullptr;
  }

  impl_->builtinParams.clear();
  impl_->exportedParams.clear();
  impl_->itemBindings.clear();

  if (impl_->api && impl_->instance && impl_->api->destroyInstance) {
    impl_->api->destroyInstance(impl_->instance);
  }
  impl_->instance = nullptr;
  impl_->api = nullptr;

  if (impl_->dlHandle) {
    dlclose(impl_->dlHandle);
    impl_->dlHandle = nullptr;
  }

  impl_->logicName.clear();
  impl_->debugText.clear();
  impl_->debugTextLastValue.clear();
  impl_->debugTextInitialized = false;
}

void ecmcNativeLogicLib::report() {
  if (!impl_) {
    return;
  }

  printf("Native Logic %d\n", impl_->index);
  printf("  Loaded             = %s\n", impl_->loaded ? "yes" : "no");
  printf("  Logic name         = %s\n", impl_->logicName.c_str());
  printf("  Shared library     = %s\n", impl_->libFilename.c_str());
  printf("  Config             = %s\n", impl_->configString.c_str());
  printf("  Asyn port          = %s\n", impl_->config.asynPortName.c_str());
  printf("  Requested rate [ms]= %.3f\n", impl_->requestedSampleRateMs);
  printf("  Actual rate [ms]   = %.3f\n", impl_->actualSampleRateMs);
  printf("  Execute divider    = %d\n", impl_->executeDividerPv);
  printf("  Item bindings      = %zu\n", impl_->itemBindings.size());
  printf("  EPICS exports      = %zu\n", impl_->exportedParams.size());
}

int ecmcNativeLogicLib::exeEnterRTFunc() {
  if (!impl_ || !impl_->loaded) {
    return 0;
  }

  impl_->enteredRt = true;
  impl_->executeDividerCounter = 0u;
  impl_->executeCount = 0;
  impl_->executeCountPublishCounter = 0u;
  impl_->executeCountPublishDue = true;
  impl_->lastExecTimeMs = 0.0;
  impl_->lastInputTimeMs = 0.0;
  impl_->lastOutputTimeMs = 0.0;
  impl_->lastTotalTimeMs = 0.0;
  impl_->debugText.clear();
  impl_->asynPort->syncExportedParams(&impl_->builtinParams, true, false);
  impl_->asynPort->syncExportedParams(&impl_->exportedParams, true, false);
  impl_->asynPort->syncOctetParam(impl_->debugTextParamId,
                                  std::string(),
                                  &impl_->debugTextLastValue,
                                  &impl_->debugTextInitialized,
                                  true,
                                  false);
  return 0;
}

int ecmcNativeLogicLib::exeExitRTFunc() {
  if (!impl_) {
    return 0;
  }
  impl_->enteredRt = false;
  impl_->lastExecTimeMs = 0.0;
  impl_->lastInputTimeMs = 0.0;
  impl_->lastOutputTimeMs = 0.0;
  impl_->lastTotalTimeMs = 0.0;
  impl_->asynPort->syncExportedParams(&impl_->builtinParams, false, false);
  return 0;
}

int ecmcNativeLogicLib::exeRTFunc(int controllerErrorCode) {
  (void)controllerErrorCode;

  if (!impl_ || !impl_->loaded || !impl_->enteredRt) {
    return 0;
  }

  impl_->executeDividerCounter++;
  if (impl_->executeDividerCounter < impl_->runtimeExecuteDivider) {
    return 0;
  }
  impl_->executeDividerCounter = 0u;

  if (impl_->executionEnabled == 0u) {
    if (impl_->lastExecTimeMs != 0.0 || impl_->lastInputTimeMs != 0.0 ||
        impl_->lastOutputTimeMs != 0.0 || impl_->lastTotalTimeMs != 0.0) {
      impl_->lastExecTimeMs = 0.0;
      impl_->lastInputTimeMs = 0.0;
      impl_->lastOutputTimeMs = 0.0;
      impl_->lastTotalTimeMs = 0.0;
      impl_->asynPort->syncExportedParams(&impl_->builtinParams, false, true);
    }
    return 0;
  }

  const bool measureTiming = impl_->timingEnabled != 0u;
  const double totalStartMs = measureTiming ? monotonicTimeMs() : 0.0;
  const double inputStartMs = measureTiming ? totalStartMs : 0.0;

  for (const auto& binding : impl_->itemBindings) {
    if (binding.binding.writable == 0u) {
      copyItemToBinding(binding);
    }
  }

  if (measureTiming) {
    impl_->lastInputTimeMs = monotonicTimeMs() - inputStartMs;
  } else {
    impl_->lastInputTimeMs = 0.0;
  }

  const double execStartMs = measureTiming ? monotonicTimeMs() : 0.0;
  try {
    g_activeNativeLogic = impl_;
    impl_->api->runCycle(impl_->instance);
    g_activeNativeLogic = nullptr;
  } catch (...) {
    g_activeNativeLogic = nullptr;
    return setErrorID(ERROR_MAIN_EXCEPTION);
  }
  if (measureTiming) {
    impl_->lastExecTimeMs = monotonicTimeMs() - execStartMs;
  } else {
    impl_->lastExecTimeMs = 0.0;
  }

  const double outputStartMs = measureTiming ? monotonicTimeMs() : 0.0;
  for (const auto& binding : impl_->itemBindings) {
    if (binding.binding.writable != 0u) {
      copyBindingToItem(binding);
    }
  }

  impl_->executeCount++;
  impl_->executeCountPublishCounter++;
  if (impl_->executeCountPublishCounter >= impl_->executeCountPublishDivider) {
    impl_->executeCountPublishCounter = 0u;
    impl_->executeCountPublishDue = true;
  }

  impl_->asynPort->syncExportedParams(&impl_->exportedParams, false, true);

  if (measureTiming) {
    impl_->lastOutputTimeMs = monotonicTimeMs() - outputStartMs;
    impl_->lastTotalTimeMs = monotonicTimeMs() - totalStartMs;
  } else {
    impl_->lastOutputTimeMs = 0.0;
    impl_->lastTotalTimeMs = 0.0;
  }

  impl_->asynPort->syncExportedParams(&impl_->builtinParams, false, true);
  impl_->asynPort->syncOctetParam(impl_->debugTextParamId,
                                  impl_->debugEnabled ? impl_->debugText : std::string(),
                                  &impl_->debugTextLastValue,
                                  &impl_->debugTextInitialized,
                                  false,
                                  true);
  impl_->executeCountPublishDue = false;
  return 0;
}
