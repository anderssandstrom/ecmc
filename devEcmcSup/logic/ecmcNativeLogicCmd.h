/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeLogicCmd.h
*
\*************************************************************************/

#ifndef ECMC_NATIVE_LOGIC_CMD_H_
#define ECMC_NATIVE_LOGIC_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

int loadNativeLogic(int logicId, const char* filenameWP, const char* configStr);
int reportNativeLogic(int logicId);

#ifdef __cplusplus
}
#endif

#endif
