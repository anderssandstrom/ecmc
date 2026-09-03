/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcRtLoggerPortDriver.h
*
*  Created on: Apr 11, 2026
*
\*************************************************************************/

#ifndef ECMC_RT_LOGGER_PORT_DRIVER_H_
#define ECMC_RT_LOGGER_PORT_DRIVER_H_

#include <stdint.h>

struct ecmcEcTimingDiag {
  int32_t status;
  int32_t source;
  int32_t reference;
  int32_t syncType;
  int32_t cycleOffset;
  int32_t timestampBits;
  int64_t cycleTimeNs;
  int64_t shiftTimeNs;
  int64_t calculationCopyTimeNs;
  int64_t eventOffsetNs;
  int64_t uncertaintyNs;
  int64_t timestampCorrectionNs;
};

int         ecmcRtLoggerPortDriverStart();
void        ecmcRtLoggerPortDriverPublishMessage(int level,
                                                 int sourceType,
                                                 int sourceIndex,
                                                 const char *message);
void        ecmcRtLoggerPortDriverPublishDropped(unsigned int dropped);
void        ecmcRtLoggerPortDriverSetAxisCommandCounters(int axisIndex,
                                                         unsigned int motorRecordRequestCounter,
                                                         unsigned int requestCounter,
                                                         unsigned int executeCounter);
void        ecmcRtLoggerPortDriverSetAxisMotorRecordCommandResult(int axisIndex,
                                                                  int command,
                                                                  int result,
                                                                  int reason,
                                                                  int errorCode,
                                                                  int cycleCounter);
void        ecmcRtLoggerPortDriverSetAxisMasterSlaveBlock(int axisIndex,
                                                          int blocked,
                                                          int cycleCounter);
void        ecmcRtLoggerPortDriverSetEcTiming(int slavePosition,
                                              int direction,
                                              const ecmcEcTimingDiag *timing);
int         ecmcRtLoggerPortDriverGetCountMotorRecordStopCommands(int axisIndex);
int         ecmcRtLoggerPortDriverGetCountEnableCommands(int axisIndex);
void        ecmcRtLoggerPortDriverService();
const char *ecmcRtLoggerPortDriverGetPortName();

#endif  /* ECMC_RT_LOGGER_PORT_DRIVER_H_ */
