// Only logging/publication infrastructure is replaced. Monitor and encoder
// implementations (including their RTTI) are compiled from production source.
#include "ecmcRtLogger.h"
asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;
void ecmcRtLoggerLogError(const char *, ...) {}
void ecmcRtLoggerLogInfoSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogErrorSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogWarningSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogDebugSource(int, int, const char *, ...) {}
