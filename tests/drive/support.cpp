#include "ecmcEncoder.h"
#include "ecmcRtLogger.h"
#include <stdexcept>
asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;
void ecmcRtLoggerLogInfoSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogErrorSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogWarningSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogDebugSource(int, int, const char *, ...) {}
// DriveBase references CSP encoder accessors. These tests exercise CSV only;
// reject accidental CSP access rather than substituting encoder behavior.
[[noreturn]] static void unavailableCsp() {
  throw std::logic_error("CSP unavailable in brake fixture");
}
// UBSan needs encoder RTTI for calls in DriveBase even though this fixture
// never constructs an encoder. Its out-of-line virtuals emit that metadata.
ecmcEncoder::~ecmcEncoder() = default;
void ecmcEncoder::errorReset() { unavailableCsp(); }
int ecmcEncoder::setErrorID(int) { unavailableCsp(); }
int ecmcEncoder::setErrorID(int, ecmcAlarmSeverity) { unavailableCsp(); }
int ecmcEncoder::setErrorID(const char *, const char *, int, int) { unavailableCsp(); }
int ecmcEncoder::setErrorID(const char *, const char *, int, int, ecmcAlarmSeverity) { unavailableCsp(); }
ecmcEcDataType ecmcEncoder::getActPosEntryDataType() { unavailableCsp(); }
double ecmcEncoder::getRawPosRegisterDouble() { unavailableCsp(); }
double ecmcEncoder::getActPos() { unavailableCsp(); }
