// Only logging/publication infrastructure is replaced. Monitor and encoder
// implementations (including their RTTI) are compiled from production source.
#include "ecmcRtLogger.h"
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
std::vector<std::string> monitorErrorMessages;
asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;
void ecmcRtLoggerLogError(const char *, ...) {}
void ecmcRtLoggerLogInfoSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogErrorSource(int, int, const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  monitorErrorMessages.emplace_back(message);
}
void ecmcRtLoggerLogWarningSource(int, int, const char *, ...) {}
void ecmcRtLoggerLogDebugSource(int, int, const char *, ...) {}

#include "ecmcDefinitions.h"
app_mode_type appModeStat = ECMC_MODE_RUNTIME;
