// Standalone PLC expression/library tests. No IOC, realtime thread or EtherCAT.
#include "exprtkWrap.h"
#include "ecmcDataStorage.h"
#include "ecmcPLCTaskErrors.h"
#include "ecmcErrorsList.h"
#include "ecmcLookupTable.h"
#include "ecmcEc.h"
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;
ecmcDataStorage *dataStorages[ECMC_MAX_DATA_STORAGE_OBJECTS] = {};

// The production library reports argument errors through this logging boundary.
void ecmcRtLoggerLogError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}
#include "ecmcPLCTask_libDs.inc"
#include "ecmcPLCTask_libLut.inc"
#include "ecmcPLCTask_libEc.inc"

ecmcLookupTable<double, double> *luts[ECMC_MAX_LUTS] = {};
ecmcEc *ec = nullptr;

// No SDO transport is emulated: requests find no configured SDO objects.
// The production PLC wrappers still perform validation and return conventions.
static int absentSdo(int slave) {
  if (!ec) return ERROR_MAIN_EC_MASTER_NULL;
  return ec->findSlave(slave) ? ERROR_EC_SDO_ASYNC_NOT_FOUND : ERROR_MAIN_EC_SLAVE_NULL;
}
extern "C" {
int ecSdoRead(int s, int) { return absentSdo(s); }
int ecSdoWrite(int s, int) { return absentSdo(s); }
int ecSdoGetBusy(int s, int, int *) { return absentSdo(s); }
int ecSdoGetError(int s, int, int *) { return absentSdo(s); }
int ecSdoGetSize(int s, int, int *) { return absentSdo(s); }
int ecSdoGetCapacity(int s, int, int *) { return absentSdo(s); }
int ecSdoGetValue(int s, int, int, double *) { return absentSdo(s); }
int ecSdoSetValue(int s, int, int, double) { return absentSdo(s); }
}

namespace {
namespace fs = std::filesystem;
struct Line { size_t number; std::string text; };
struct Test { std::string name; std::vector<Line> lines; };

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
std::string readFile(const fs::path &path) {
  std::ifstream file(path);
  if (!file) fail("cannot open " + path.string());
  std::ostringstream text;
  text << file.rdbuf();
  if (file.bad()) fail("cannot read " + path.string());
  return text.str();
}
double number(const std::string &token) {
  size_t used = 0;
  double result = std::stod(token, &used);
  if (used != token.size() || !std::isfinite(result))
    fail("expected finite number: " + token);
  return result;
}
int integer(const std::string &token, int low, int high) {
  double value = number(token);
  if (value < low || value > high || std::floor(value) != value)
    fail("integer out of range: " + token);
  return static_cast<int>(value);
}
int64_t nanoseconds(const std::string &token, bool signedValue = false) {
  double value = number(token);
  // Keep setup values exactly representable by the PLC double interface.
  if (std::floor(value) != value || value > 9007199254740991.0 ||
      value < (signedValue ? -9007199254740991.0 : 0)) fail("invalid nanoseconds: " + token);
  return static_cast<int64_t>(value);
}
std::vector<std::string> words(const std::string &line) {
  std::istringstream input(line);
  std::vector<std::string> result;
  std::string word;
  while (input >> std::quoted(word)) result.push_back(word);
  if (!input.eof()) fail("invalid quoted argument");
  return result;
}
void count(const std::vector<std::string> &args, size_t size) {
  if (args.size() != size) fail("wrong number of arguments to " + args[0]);
}
std::vector<Test> parse(const fs::path &path) {
  std::istringstream input(readFile(path));
  std::vector<Test> tests;
  bool active = false;
  std::string text;
  size_t line = 0;
  while (std::getline(input, text)) {
    ++line;
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos || text[first] == '#') continue;
    auto args = words(text);
    if (args[0] == "case") {
      count(args, 2);
      if (active) fail("nested case at line " + std::to_string(line));
      for (const auto &test : tests)
        if (test.name == args[1]) fail("duplicate case: " + args[1]);
      tests.push_back({args[1], {}});
      active = true;
    } else if (args[0] == "end") {
      count(args, 1);
      if (!active) fail("end outside case");
      active = false;
    } else {
      if (!active) fail("command outside case at line " + std::to_string(line));
      tests.back().lines.push_back({line, text});
    }
  }
  if (active) fail("missing end");
  if (tests.empty()) fail("suite contains no cases");
  return tests;
}

class Context {
  ecmcAsynPortDriver publication_;
  std::map<int, std::unique_ptr<ecmcDataStorage>> storage_;
  std::map<int, std::unique_ptr<ecmcLookupTable<double, double>>> tables_;
  std::unique_ptr<ecmcEc> backend_;
  std::map<std::string, double> variables_;
  // Destroy expression before the variable references it holds.
  exprtkWrap expression_;
  bool compiled_ = false;
 public:
  Context() {
    ds_errorCode = 0;
    ec_errorCode = 0;
    lut_errorCode = 0;
    // All storage functions except EPICS publication are bound below.
    if (ds_cmd_count != 15) fail("storage library changed; update offline bindings");
#define BIND(function) if (expression_.addFunction(#function, function)) \
    fail("cannot register " #function)
    BIND(ds_append_data); BIND(ds_clear_data); BIND(ds_get_data);
    BIND(ds_set_data); BIND(ds_get_buff_id); BIND(ds_set_buff_id);
    BIND(ds_get_err); BIND(ds_is_full); BIND(ds_get_size);
    BIND(ds_get_avg); BIND(ds_get_min); BIND(ds_get_max);
    BIND(ds_err_rst); BIND(ds_append_to_ds);
    BIND(lut_get_value);
    BIND(ec_set_bit); BIND(ec_clr_bit); BIND(ec_flp_bit); BIND(ec_chk_bit);
    BIND(ec_wrt_bit); BIND(ec_wrt_bits); BIND(ec_chk_bits);
    BIND(ec_print_hex); BIND(ec_print_bin); BIND(ec_get_err); BIND(ec_err_rst);
    BIND(ec_get_time); BIND(ec_get_time_l32); BIND(ec_get_time_u32);
    BIND(ec_get_last_receive_time); BIND(ec_get_last_send_time);
    BIND(ec_get_time_offset_mono); BIND(ec_get_slave_input_event_time);
    BIND(ec_get_time_local_nsec); BIND(ec_get_time_local_sec);
    BIND(ec_get_time_local_min); BIND(ec_get_time_local_hour);
    BIND(ec_mm_cp); BIND(ec_get_mm_type); BIND(ec_get_mm_data);
    BIND(ec_set_mm_data); BIND(ec_get_mm_size); BIND(ec_get_dom_state);
    BIND(ec_mm_append_to_ds); BIND(ec_mm_append_to_ds_scale_offset);
    BIND(ec_sdo_read); BIND(ec_sdo_write); BIND(ec_sdo_get_busy);
    BIND(ec_sdo_get_error); BIND(ec_sdo_get_size); BIND(ec_sdo_get_capacity);
    BIND(ec_sdo_get_value); BIND(ec_sdo_set_value);
#undef BIND
  }
  ~Context() {
    for (const auto &item : storage_) dataStorages[item.first] = nullptr;
    for (const auto &item : tables_) luts[item.first] = nullptr;
    ec = nullptr;
    ds_errorCode = 0;
    ec_errorCode = 0;
    lut_errorCode = 0;
  }
  void execute(const Test &test, const fs::path &base) {
    size_t runs = 0, checks = 0;
    for (const auto &line : test.lines) {
      try {
        auto a = words(line.text);
        const auto &command = a[0];
        if (command == "var") {
          count(a, 3);
          if (compiled_) fail("declare variables before plc");
          if (a[1].rfind("global.", 0) != 0 && a[1].rfind("static.", 0) != 0)
            fail("variable must use global. or static. prefix");
          if (!variables_.emplace(a[1], number(a[2])).second)
            fail("duplicate variable: " + a[1]);
          if (expression_.addVariable(a[1], variables_.at(a[1])))
            fail("cannot register variable: " + a[1]);
        } else if (command == "storage") {
          count(a, 4);
          int index = integer(a[1], 0, ECMC_MAX_DATA_STORAGE_OBJECTS - 1);
          int size = integer(a[2], 1, 1000000);
          int mode = integer(a[3], 0, 2);
          if (storage_.count(index)) fail("duplicate storage index");
          auto object = std::make_unique<ecmcDataStorage>(
              &publication_, index, size, static_cast<ecmcDSBufferType>(mode));
          if (object->getErrorID()) fail("storage initialization failed");
          if (object->clearBuffer()) fail("storage clear failed");
          dataStorages[index] = object.get();
          storage_.emplace(index, std::move(object));
        } else if (command == "lut") {
          count(a, 3);
          int index = integer(a[1], 0, ECMC_MAX_LUTS - 1);
          // Construct first: a failed reload keeps the old table intact.
          std::unique_ptr<ecmcLookupTable<double, double>> table;
          try {
            table = std::make_unique<ecmcLookupTable<double, double>>((base / a[2]).string());
          } catch (int error) {
            fail("LUT load failed: " + std::to_string(error));
          }
          luts[index] = table.get();
          tables_[index] = std::move(table);
        } else if (command == "ec") {
          count(a, 1);
          if (backend_) fail("offline EtherCAT backend already created");
          backend_ = std::make_unique<ecmcEc>();
          ec = backend_.get();
        } else if (command == "ec_memmap") {
          count(a, 3);
          if (!backend_) fail("ec_memmap requires ec");
          int index = integer(a[1], 0, EC_MAX_MEM_MAPS - 1);
          int size = integer(a[2], 1, 1000000);
          if (ec->maps.count(index)) fail("duplicate memory map");
          ec->maps[index] = std::make_unique<ecmcEcMemMap>(size);
        } else if (command == "ec_time") {
          count(a, 6);
          if (!backend_) fail("ec_time requires ec");
          ec->cycle.applicationTimeNs = nanoseconds(a[1]);
          ec->cycle.receiveTimeNs = nanoseconds(a[2]);
          ec->sendTime = nanoseconds(a[3]);
          ec->cycle.nominalPeriodNs = integer(a[4], 1, 1000000000);
          ec->offset = nanoseconds(a[5], true);
        } else if (command == "ec_domain") {
          count(a, 3);
          if (!backend_) fail("ec_domain requires ec");
          ec->domains[integer(a[1], 0, 65535)] = integer(a[2], 0, 1);
        } else if (command == "ec_slave") {
          count(a, 4);
          if (!backend_) fail("ec_slave requires ec");
          auto &slave = ec->slaves[integer(a[1], 0, 65535)];
          slave.timing.valid = true;
          slave.timing.cycleOffsetKnown = true;
          slave.timing.eventOffsetKnown = true;
          slave.timing.cycleOffset = integer(a[2], -1000000, 1000000);
          slave.timing.eventOffsetNs = integer(a[3], -1000000000, 1000000000);
        } else if (command == "plc") {
          count(a, 2);
          if (compiled_) fail("only one plc per case is supported");
          if (expression_.compile(readFile(base / a[1])))
            fail("PLC compile failed: " + expression_.getParserError());
          compiled_ = true;
        } else if (command == "set") {
          count(a, 3);
          if (!variables_.count(a[1])) fail("unknown variable: " + a[1]);
          variables_.at(a[1]) = number(a[2]);
        } else if (command == "run") {
          count(a, 2);
          if (!compiled_) fail("run requires a compiled plc");
          int cycles = integer(a[1], 1, 1000000);
          for (int i = 0; i < cycles; ++i) expression_.refresh();
          runs += cycles;
        } else if (command == "expect") {
          if (a.size() != 3 && a.size() != 4) fail("expect variable value [absolute_tolerance]");
          if (!runs) fail("expect requires at least one evaluation");
          if (!variables_.count(a[1])) fail("unknown variable: " + a[1]);
          double expected = number(a[2]);
          double tolerance = a.size() == 4 ? number(a[3]) : 0;
          if (tolerance < 0) fail("negative tolerance");
          double actual = variables_.at(a[1]);
          if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
            std::ostringstream message;
            message << std::setprecision(17) << a[1] << ": expected " << expected
                    << ", actual " << actual << ", tolerance " << tolerance;
            fail(message.str());
          }
          ++checks;
        } else {
          fail("unsupported command: " + command);
        }
      } catch (const std::exception &error) {
        fail("line " + std::to_string(line.number) + ": " + error.what());
      }
    }
    if (!runs || !checks) fail("case requires run and expect");
  }
};

void timeout(int) {
  const char message[] = "FAIL: PLC test timeout\n";
  (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
  _exit(124);
}
}  // namespace

int main(int argc, char **argv) {
  try {
    fs::path suite;
    int seconds = 30;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "Usage: ecmcPlcTest --suite FILE [--timeout SECONDS]\n";
        return 0;
      }
      if (arg == "--suite" && i + 1 < argc && suite.empty()) suite = argv[++i];
      else if (arg == "--timeout" && i + 1 < argc) seconds = integer(argv[++i], 1, 3600);
      else fail("unknown or incomplete argument: " + arg);
    }
    if (suite.empty()) fail("usage: ecmcPlcTest --suite FILE [--timeout SECONDS]");
    std::signal(SIGALRM, timeout);
    alarm(seconds);
    auto tests = parse(suite);
    size_t passed = 0;
    for (const auto &test : tests) {
      try {
        Context context;
        context.execute(test, suite.parent_path());
        std::cout << "[PASS] " << test.name << std::endl;
        ++passed;
      } catch (const std::exception &error) {
        std::cout << "[FAIL] " << test.name << ": " << error.what() << std::endl;
      }
    }
    std::cout << passed << '/' << tests.size() << " passed\n";
    return passed == tests.size() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}
