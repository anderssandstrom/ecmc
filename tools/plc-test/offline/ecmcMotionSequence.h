// Declaration-only boundary for unregistered sequence PLC functions.
#pragma once
#define ECMC_MAX_MOTION_SEQUENCES 16
class ecmcMotionSequence;
extern ecmcMotionSequence *motionSeqs[ECMC_MAX_MOTION_SEQUENCES];
int requestMotionSeqArmRTSafe(int);
int requestMotionSeqCurrentStepRTSafe(int, int);
int getMotionSeqState(int); int getMotionSeqValid(int); int getMotionSeqArmed(int);
int getMotionSeqRunning(int); int getMotionSeqCompileBusy(int); int getMotionSeqStepId(int);
int getMotionSeqAction(int); int getMotionSeqErrorId(int); int getMotionSeqStepCount(int);
double getMotionSeqElapsedMs(int);
