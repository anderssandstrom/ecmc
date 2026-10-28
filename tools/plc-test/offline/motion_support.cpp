// Test-only implementations of unavailable infrastructure. The real axis,
// sequencer, trapezoidal trajectory, encoder and monitor are compiled separately.
#include "ecmcAxisBase.h"
#include "ecmcTrajectoryS.h"
#include "ecmcRtLogger.h"
#include "ecmcRtLoggerPortDriver.h"
#include <stdexcept>

ecmcAxisBase *axes[ECMC_MAX_AXES] = {};
app_mode_type appModeStat = ECMC_MODE_RUNTIME;
int ecmcStartupMotionHeld() { return 0; }
int ecmcStartupCommandsHeld() { return 0; }
int ecmcRtLoggerIsEnabled() { return 0; }
void ecmcRtLoggerLogInfo(const char *, ...) {}
void ecmcRtLoggerLogDebug(const char *, ...) {}
void ecmcRtLoggerLogInfoSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogErrorSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogDebugSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogWarningSource(int, int, const char *, ...) {}
int ecmcLogBufferWrite(int, const char *, ...) { return 0; }
void ecmcRtLoggerPortDriverSetAxisCommandCounters(int, unsigned int, unsigned int, unsigned int) {}
void ecmcRtLoggerPortDriverSetAxisMasterSlaveBlock(int, int, int) {}
int ecmcRtLoggerPortDriverGetCountEnableCommands(int) { return 0; }
extern "C" int setAxisPLCEnable(int, int) {
  throw std::logic_error("axis PLCs unavailable in standalone motion tests");
}
extern "C" int getAxisPLCEnable(int, int *enable) { *enable = 0; return 0; }
void ecmcDriveBase::setCspRef(double, double, double) {
  throw std::logic_error("physical drives unavailable in virtual-axis tests");
}

// AxisBase contains an S-curve construction branch. Reject it explicitly rather
// than shipping a substitute S-curve algorithm or adding a Ruckig dependency.
static void unavailableSCurve() { throw std::logic_error("only trapezoidal trajectories supported offline"); }
ecmcTrajectoryS::ecmcTrajectoryS(ecmcAxisData &data, double period)
  : ecmcTrajectoryBase(data, period) { unavailableSCurve(); }
ecmcTrajectoryS::~ecmcTrajectoryS() = default;
void ecmcTrajectoryS::setTargetVel(double) { unavailableSCurve(); }
void ecmcTrajectoryS::setTargetPosLocal(double) { unavailableSCurve(); }
void ecmcTrajectoryS::setCurrentPosSet(double) { unavailableSCurve(); }
double ecmcTrajectoryS::distToStop(double) { unavailableSCurve(); return 0; }
int ecmcTrajectoryS::initStopRamp(double, double, double) { unavailableSCurve(); return 0; }
int ecmcTrajectoryS::setExecute(bool) { unavailableSCurve(); return 0; }
int ecmcTrajectoryS::validate() { unavailableSCurve(); return 0; }
double ecmcTrajectoryS::internalTraj(double *, double *, bool *) { unavailableSCurve(); return 0; }
