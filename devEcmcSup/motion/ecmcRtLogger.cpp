/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcRtLogger.cpp
*
*  Created on: Apr 10, 2026
*
\*************************************************************************/

#include "ecmcRtLogger.h"

#include <atomic>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "epicsMutex.h"
#include "epicsThread.h"
#include "epicsTime.h"
#include "ecmcOctetIF.h"
#include "ecmcRtLoggerPortDriver.h"

namespace {

constexpr size_t ECMC_RT_LOGGER_QUEUE_SIZE = 256;
constexpr size_t ECMC_RT_LOGGER_MSG_SIZE   = 192;
constexpr size_t ECMC_LOG_BUFFER_MAX_ROWS  = 1024;
constexpr size_t ECMC_LOG_BUFFER_MSG_SIZE  = 384;
constexpr size_t ECMC_LOG_BUFFER_TIME_SIZE = 40;
constexpr double ECMC_RT_LOGGER_SLEEP_S    = 0.05;
const char      *ECMC_RT_LOGGER_THREAD     = "ecmc_rt_log";
const char      *ECMC_LOG_BUFFER_FRAME     =
  "#--------------------------------------------------";

struct ecmcRtLogEvent {
  int  level;
  int  sourceType;
  int  sourceIndex;
  char message[ECMC_RT_LOGGER_MSG_SIZE];
};

struct ecmcLogBufferEntry {
  unsigned int sequence;
  int          level;
  char         timestamp[ECMC_LOG_BUFFER_TIME_SIZE];
  char         message[ECMC_LOG_BUFFER_MSG_SIZE];
};

struct ecmcLogBuffer {
  ecmcLogBufferEntry entries[ECMC_LOG_BUFFER_MAX_ROWS];
  size_t             writeIndex;
  size_t             count;
  unsigned int       sequence;
  const char        *name;
};

std::atomic<size_t> writeIndex_(0);
std::atomic<size_t> readIndex_(0);
std::atomic<unsigned int> droppedCount_(0);
std::atomic<int> enabled_(0);
std::atomic<int> started_(0);
std::atomic<unsigned int> controlWord_(ECMC_RT_LOGGER_CONTROL_DEFAULT);
ecmcRtLogEvent queue_[ECMC_RT_LOGGER_QUEUE_SIZE] = {};
ecmcLogBuffer configLogBuffer_ = {{}, 0, 0, 0, "ecmc configuration log buffer"};
ecmcLogBuffer rtLogBuffer_ = {{}, 0, 0, 0, "ecmc runtime log buffer"};

epicsMutexId getConfigLogBufferMutex() {
  static epicsMutexId mutex = epicsMutexMustCreate();
  return mutex;
}

epicsMutexId getRtLogBufferMutex() {
  static epicsMutexId mutex = epicsMutexMustCreate();
  return mutex;
}

const char *rtLogLevelText(int level) {
  if (level == ECMC_RT_LOG_LEVEL_ERROR) {
    return "ERROR";
  }
  if (level == ECMC_RT_LOG_LEVEL_WARNING) {
    return "WARNING";
  }
  if (level == ECMC_RT_LOG_LEVEL_DEBUG) {
    return "DEBUG";
  }
  return "INFO";
}

int normalizeLogBufferLevel(int level) {
  if (level == ECMC_RT_LOG_LEVEL_ERROR ||
      level == ECMC_RT_LOG_LEVEL_WARNING ||
      level == ECMC_RT_LOG_LEVEL_DEBUG) {
    return level;
  }
  return ECMC_LOG_BUFFER_INFO;
}

int logBufferLevelFromText(const char *level) {
  if (!level) {
    return ECMC_LOG_BUFFER_INFO;
  }
  if (!strcasecmp(level, "ERROR")) {
    return ECMC_LOG_BUFFER_ERROR;
  }
  if (!strcasecmp(level, "WARNING") || !strcasecmp(level, "WARN")) {
    return ECMC_LOG_BUFFER_WARNING;
  }
  return ECMC_LOG_BUFFER_INFO;
}

void formatLogBufferTimestamp(char *buffer, size_t bufferSize) {
  epicsTimeStamp now;

  if (!buffer || bufferSize == 0) {
    return;
  }

  if (epicsTimeGetCurrent(&now)) {
    snprintf(buffer, bufferSize, "unknown-time");
    return;
  }

  char seconds[ECMC_LOG_BUFFER_TIME_SIZE];
  epicsTimeToStrftime(seconds, sizeof(seconds), "%Y-%m-%d %H:%M:%S", &now);
  snprintf(buffer, bufferSize, "%s.%03u", seconds, now.nsec / 1000000);
}

int logBufferWriteEntry(ecmcLogBuffer &buffer,
                        epicsMutexId   mutex,
                        int            level,
                        const char    *message) {
  if (!message) {
    return -1;
  }
  ecmcLogBufferEntry entry = {};
  entry.level = normalizeLogBufferLevel(level);
  formatLogBufferTimestamp(entry.timestamp, sizeof(entry.timestamp));
  snprintf(entry.message, sizeof(entry.message), "%s", message);

  epicsMutexLock(mutex);

  entry.sequence = ++buffer.sequence;
  buffer.entries[buffer.writeIndex] = entry;
  buffer.writeIndex = (buffer.writeIndex + 1) % ECMC_LOG_BUFFER_MAX_ROWS;
  if (buffer.count < ECMC_LOG_BUFFER_MAX_ROWS) {
    buffer.count++;
  }

  epicsMutexUnlock(mutex);
  return 0;
}

int logBufferWriteV(ecmcLogBuffer &buffer,
                    epicsMutexId   mutex,
                    int            level,
                    const char    *fmt,
                    va_list        args) {
  if (!fmt) {
    return -1;
  }

  char message[ECMC_LOG_BUFFER_MSG_SIZE];
  vsnprintf(message, sizeof(message), fmt, args);
  return logBufferWriteEntry(buffer, mutex, level, message);
}

void logBufferPrint(ecmcLogBuffer &buffer, epicsMutexId mutex) {
  epicsMutexLock(mutex);

  printf("%s\n", ECMC_LOG_BUFFER_FRAME);

  if (buffer.count == 0) {
    printf("%s is empty.\n", buffer.name);
    printf("%s\n", ECMC_LOG_BUFFER_FRAME);
    epicsMutexUnlock(mutex);
    return;
  }

  printf("%s (%zu/%zu entries):\n",
         buffer.name,
         buffer.count,
         ECMC_LOG_BUFFER_MAX_ROWS);

  const size_t firstIndex =
    (buffer.writeIndex + ECMC_LOG_BUFFER_MAX_ROWS - buffer.count) %
    ECMC_LOG_BUFFER_MAX_ROWS;

  for (size_t i = 0; i < buffer.count; ++i) {
    const size_t index = (firstIndex + i) % ECMC_LOG_BUFFER_MAX_ROWS;
    const ecmcLogBufferEntry &entry = buffer.entries[index];
    printf("%u %s %s: %s\n",
           entry.sequence,
           entry.timestamp,
           rtLogLevelText(entry.level),
           entry.message);
  }

  printf("%s\n", ECMC_LOG_BUFFER_FRAME);
  epicsMutexUnlock(mutex);
}

void logBufferClear(ecmcLogBuffer &buffer, epicsMutexId mutex) {
  epicsMutexLock(mutex);

  memset(buffer.entries, 0, sizeof(buffer.entries));
  buffer.writeIndex = 0;
  buffer.count = 0;
  buffer.sequence = 0;

  epicsMutexUnlock(mutex);
}

bool levelEnabled(int level) {
  const unsigned int controlWord = controlWord_.load(std::memory_order_acquire);

  if (level == ECMC_RT_LOG_LEVEL_ERROR) {
    return (controlWord & ECMC_RT_LOGGER_CONTROL_ERROR_ENABLE) != 0;
  }
  if (level == ECMC_RT_LOG_LEVEL_WARNING) {
    return (controlWord & ECMC_RT_LOGGER_CONTROL_WARNING_ENABLE) != 0;
  }
  if (level == ECMC_RT_LOG_LEVEL_DEBUG) {
    return (controlWord & ECMC_RT_LOGGER_CONTROL_DEBUG_ENABLE) != 0;
  }

  return (controlWord & ECMC_RT_LOGGER_CONTROL_INFO_ENABLE) != 0;
}

void normalizeSeverityText(char *message,
                           size_t messageSize,
                           int level) {
  if (!message || messageSize == 0) {
    return;
  }

  const char *targetSeverity = rtLogLevelText(level);

  const char *patterns[] = {
    ": INFO: ",
    ": WARNING: ",
    ": ERROR: ",
    ": DEBUG: "
  };

  for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
    const char *match = strstr(message, patterns[i]);
    if (!match) {
      continue;
    }

    const size_t prefixLength = (size_t)(match - message) + 2;
    char normalized[ECMC_RT_LOGGER_MSG_SIZE];
    snprintf(normalized,
             sizeof(normalized),
             "%.*s%s: %s",
             (int)prefixLength,
             message,
             targetSeverity,
             match + strlen(patterns[i]));
    const size_t copyLength = strnlen(normalized, messageSize - 1);
    memcpy(message, normalized, copyLength);
    message[copyLength] = '\0';
    return;
  }
}

void printMessage(int level, const char *message) {
  if (level == ECMC_RT_LOG_LEVEL_ERROR) {
    LOGERR("%s", message);
  } else if (level == ECMC_RT_LOG_LEVEL_WARNING) {
    LOGWARNING("%s", message);
  } else if (level == ECMC_RT_LOG_LEVEL_DEBUG) {
    LOGDEBUG("%s", message);
  } else {
    LOGINFO("%s", message);
  }
}

void reportDroppedEvents() {
  const unsigned int dropped =
    droppedCount_.exchange(0, std::memory_order_acq_rel);

  if (dropped == 0) {
    return;
  }

  ecmcRtLoggerPortDriverPublishDropped(dropped);

  LOGERR("%s/%s:%d: ERROR: RT log queue dropped %u events.\n",
         __FILE__,
         __FUNCTION__,
         __LINE__,
         dropped);
}

void drainQueue() {
  while (true) {
    const size_t readIndex = readIndex_.load(std::memory_order_relaxed);
    const size_t writeIndex = writeIndex_.load(std::memory_order_acquire);

    if (readIndex == writeIndex) {
      break;
    }

    const ecmcRtLogEvent event = queue_[readIndex];
    readIndex_.store((readIndex + 1) % ECMC_RT_LOGGER_QUEUE_SIZE,
                     std::memory_order_release);
    ecmcRtLoggerPortDriverPublishMessage(event.level,
                                         event.sourceType,
                                         event.sourceIndex,
                                         event.message);
    logBufferWriteEntry(rtLogBuffer_,
                        getRtLogBufferMutex(),
                        event.level,
                        event.message);
    printMessage(event.level, event.message);
  }

  reportDroppedEvents();
}

void loggerTask(void *arg) {
  (void)arg;

  while (true) {
    ecmcRtLoggerPortDriverService();
    drainQueue();
    epicsThreadSleep(ECMC_RT_LOGGER_SLEEP_S);
  }
}

void logMessageV(int level,
                 int sourceType,
                 int sourceIndex,
                 const char *fmt,
                 va_list args) {
  if (!levelEnabled(level)) {
    return;
  }

  if (!started_.load(std::memory_order_acquire) ||
      !enabled_.load(std::memory_order_acquire)) {
    char buffer[ECMC_RT_LOGGER_MSG_SIZE];
    va_list argsCopy;
    va_copy(argsCopy, args);
    vsnprintf(buffer, sizeof(buffer), fmt, argsCopy);
    va_end(argsCopy);
    normalizeSeverityText(buffer, sizeof(buffer), level);
    printMessage(level, buffer);
    return;
  }

  const size_t writeIndex = writeIndex_.load(std::memory_order_relaxed);
  const size_t nextWriteIndex = (writeIndex + 1) % ECMC_RT_LOGGER_QUEUE_SIZE;
  const size_t readIndex = readIndex_.load(std::memory_order_acquire);

  if (nextWriteIndex == readIndex) {
    droppedCount_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  queue_[writeIndex].level = level;
  queue_[writeIndex].sourceType = sourceType;
  queue_[writeIndex].sourceIndex = sourceIndex;
  // The consumer cannot see this slot until the release-store below.
  va_list argsCopy;
  va_copy(argsCopy, args);
  vsnprintf(queue_[writeIndex].message, ECMC_RT_LOGGER_MSG_SIZE, fmt, argsCopy);
  va_end(argsCopy);
  normalizeSeverityText(queue_[writeIndex].message, ECMC_RT_LOGGER_MSG_SIZE, level);
  writeIndex_.store(nextWriteIndex, std::memory_order_release);
}

}  // namespace

int ecmcRtLoggerStart() {
  if (started_.load(std::memory_order_acquire)) {
    return 0;
  }

  if (ecmcRtLoggerPortDriverStart()) {
    LOGWARNING("%s/%s:%d: WARNING: Failed to create RT logger asyn port driver. Continuing with IOC log output only.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__);
  }

  epicsThreadId threadId = epicsThreadCreate(
    ECMC_RT_LOGGER_THREAD,
    epicsThreadPriorityLow,
    epicsThreadGetStackSize(epicsThreadStackSmall),
    loggerTask,
    NULL);

  if (!threadId) {
    LOGWARNING("%s/%s:%d: WARNING: Failed to create low priority RT log thread. Using direct RT logging.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__);
    enabled_.store(0, std::memory_order_release);
    return 0;
  }

  started_.store(1, std::memory_order_release);
  return 0;
}

void ecmcRtLoggerSetEnabled(int enabled) {
  enabled_.store(enabled ? 1 : 0, std::memory_order_release);
}

int ecmcRtLoggerIsEnabled() {
  return started_.load(std::memory_order_acquire) &&
         enabled_.load(std::memory_order_acquire);
}

const char *ecmcRtLoggerGetAsynPortName() {
  return ecmcRtLoggerPortDriverGetPortName();
}

void ecmcRtLoggerSetControlWord(unsigned int controlWord) {
  controlWord_.store(controlWord, std::memory_order_release);
}

unsigned int ecmcRtLoggerGetControlWord() {
  return controlWord_.load(std::memory_order_acquire);
}

int ecmcLogBufferWrite(int level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int result = logBufferWriteV(configLogBuffer_,
                                     getConfigLogBufferMutex(),
                                     level,
                                     fmt,
                                     args);
  va_end(args);
  return result;
}

int ecmcLogBufferWriteText(const char *level, const char *message) {
  if (!message) {
    return -1;
  }
  return ecmcLogBufferWrite(logBufferLevelFromText(level), "%s", message);
}

int ecmcRtLogBufferWrite(int level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int result = logBufferWriteV(rtLogBuffer_,
                                     getRtLogBufferMutex(),
                                     level,
                                     fmt,
                                     args);
  va_end(args);
  return result;
}

int ecmcRtLogBufferWriteText(const char *level, const char *message) {
  if (!message) {
    return -1;
  }
  return ecmcRtLogBufferWrite(logBufferLevelFromText(level), "%s", message);
}

void ecmcLogBufferLogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logBufferWriteV(configLogBuffer_,
                  getConfigLogBufferMutex(),
                  ECMC_LOG_BUFFER_INFO,
                  fmt,
                  args);
  va_end(args);
}

void ecmcLogBufferLogWarning(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logBufferWriteV(configLogBuffer_,
                  getConfigLogBufferMutex(),
                  ECMC_LOG_BUFFER_WARNING,
                  fmt,
                  args);
  va_end(args);
}

void ecmcLogBufferLogError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logBufferWriteV(configLogBuffer_,
                  getConfigLogBufferMutex(),
                  ECMC_LOG_BUFFER_ERROR,
                  fmt,
                  args);
  va_end(args);
}

void ecmcLogBufferPrint() {
  logBufferPrint(configLogBuffer_, getConfigLogBufferMutex());
}

void ecmcLogBufferClear() {
  logBufferClear(configLogBuffer_, getConfigLogBufferMutex());
}

void ecmcRtLogBufferPrint() {
  logBufferPrint(rtLogBuffer_, getRtLogBufferMutex());
}

void ecmcRtLogBufferClear() {
  logBufferClear(rtLogBuffer_, getRtLogBufferMutex());
}

void ecmcRtLoggerLogInfoSource(int sourceType,
                               int sourceIndex,
                               const char *fmt,
                               ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_INFO,
              sourceType,
              sourceIndex,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogDebugSource(int sourceType,
                                int sourceIndex,
                                const char *fmt,
                                ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_DEBUG,
              sourceType,
              sourceIndex,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogErrorSource(int sourceType,
                                int sourceIndex,
                                const char *fmt,
                                ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_ERROR,
              sourceType,
              sourceIndex,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogWarningSource(int sourceType,
                                  int sourceIndex,
                                  const char *fmt,
                                  ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_WARNING,
              sourceType,
              sourceIndex,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_INFO,
              ECMC_RT_LOG_SOURCE_UNKNOWN,
              -1,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogDebug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_DEBUG,
              ECMC_RT_LOG_SOURCE_UNKNOWN,
              -1,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_ERROR,
              ECMC_RT_LOG_SOURCE_UNKNOWN,
              -1,
              fmt,
              args);
  va_end(args);
}

void ecmcRtLoggerLogWarning(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logMessageV(ECMC_RT_LOG_LEVEL_WARNING,
              ECMC_RT_LOG_SOURCE_UNKNOWN,
              -1,
              fmt,
              args);
  va_end(args);
}
