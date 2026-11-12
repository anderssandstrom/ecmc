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
// Raw YAML state is captured under the RT mutex, then formatted/written without
// it. Capture remains proportional to the configured state (including PVT and
// filter arrays). Allocation/capture/output failure returns nonzero; incomplete
// raw snapshots are never serialized. Level-2 JSON and YAML retain their
// existing separate capture times.
int  ecmcMotionDiagWriteDumpFile(const char *fileName, int level);
int  ecmcMotionStateWriteYaml(const char *fileName);
void ecmcMotionDiagBuildAxisReport(int axisIndex, char *buffer, size_t bufferSize);

#endif  /* ECMC_MOTION_DIAG_H_ */
