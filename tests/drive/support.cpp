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
ecmcEcDataType ecmcEncoder::getActPosEntryDataType() { throw std::logic_error("CSP unavailable in brake fixture"); }
double ecmcEncoder::getRawPosRegisterDouble() { throw std::logic_error("CSP unavailable in brake fixture"); }
double ecmcEncoder::getActPos() { throw std::logic_error("CSP unavailable in brake fixture"); }
