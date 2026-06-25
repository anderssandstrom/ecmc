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
int         ecmcRtLoggerPortDriverGetCountMotorRecordStopCommands(int axisIndex);
int         ecmcRtLoggerPortDriverGetCountEnableCommands(int axisIndex);
void        ecmcRtLoggerPortDriverService();
const char *ecmcRtLoggerPortDriverGetPortName();

#endif  /* ECMC_RT_LOGGER_PORT_DRIVER_H_ */
