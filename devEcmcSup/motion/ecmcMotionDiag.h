/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMotionDiag.h
*
\*************************************************************************/

#ifndef ECMC_MOTION_DIAG_H_
#define ECMC_MOTION_DIAG_H_

#include <stddef.h>

void ecmcMotionDiagMakeDumpFileName(char *buffer, size_t bufferSize);
int  ecmcMotionDiagWriteDumpFile(const char *fileName, int level);
void ecmcMotionDiagBuildAxisReport(int axisIndex, char *buffer, size_t bufferSize);

#endif  /* ECMC_MOTION_DIAG_H_ */
