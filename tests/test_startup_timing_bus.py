#!/usr/bin/env python3
"""Exercise the startup timing gate and timeout report with fake EtherLab state."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/ethercat/ecmcEc.cpp').read_text()
gate = source[source.index('void ecmcEc::updateTimingDiscoveryGate('):
              source.index('\nint ecmcEc::updateInputProcessImage()')]
report = source[source.index('bool ecmcEc::timingStartupReady()'):
                source.index('\nvoid ecmcEc::resetStartupCommunicationErrors()')]

harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

constexpr unsigned EC_AL_STATE_INIT = 1, EC_AL_STATE_PREOP = 2,
                   EC_AL_STATE_SAFEOP = 4, EC_AL_STATE_OP = 8, EC_WC_COMPLETE = 2;
double mcuPeriod = 1e6;
struct ec_slave_config_state_t { unsigned online, operational, al_state; };
struct ec_master_state_t { unsigned link_up = 1, slaves_responding = 2, al_states = 12; };
struct ec_master_info_t { bool scan_busy = false; };
struct ec_slave_info_t {
  char name[20] = "Test terminal";
  unsigned al_state = EC_AL_STATE_SAFEOP;
  unsigned error_flag = 1;
};
struct ec_domain_state_t { unsigned working_counter = 0, wc_state = 0; };
int offRtCalls = 0;
void ecrt_master_state(ec_master_state_t *master, ec_master_state_t *state) { *state = *master; }
int ecrt_master(ec_master_state_t *, ec_master_info_t *) { ++offRtCalls; return 0; }
int ecrt_master_get_slave(ec_master_state_t *, int, ec_slave_info_t *info) {
  ++offRtCalls;
  *info = {};
  return 0;
}
void ecrt_domain_state(ec_domain_state_t *domain, ec_domain_state_t *state) { *state = *domain; }
std::string diagnostics;
void record(const char *format, ...) {
  char message[192];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  diagnostics += message;
}
#define LOGERR(...) record(__VA_ARGS__)
#define ecmcRtLoggerLogError(...) record(__VA_ARGS__)

struct Slave {
  int position;
  bool optional, timingReady;
  ec_slave_config_state_t state;
  int polls = 0;
  bool getAllowOffline() const { return optional; }
  bool timingStartupReady() const { return timingReady; }
  int getSlaveBusPosition() const { return position; }
  ec_slave_config_state_t readStartupState() { ++polls; return state; }
};
struct Domain {
  ec_domain_state_t state;
  bool hasCompleteWorkingCounter() const { return state.wc_state == EC_WC_COMPLETE; }
  ec_domain_state_t *getDomain() { return &state; }
};
struct ecmcEc {
  ec_master_state_t master;
  ec_master_state_t *master_ = &master;
  int masterIndex_ = 0;
  Slave first{1, false, true, {1, 1, EC_AL_STATE_OP}};
  Slave late{7, false, false, {1, 0, EC_AL_STATE_SAFEOP}};
  Slave offline{9, true, false, {0, 0, 0}};
  Slave *slaveArray_[3] = {&first, &late, &offline};
  int slaveCounter_ = 3;
  Domain required{{2, EC_WC_COMPLETE}};
  Domain optional{{0, 0}};
  Domain *domains_[2] = {&required, &optional};
  std::vector<int> timingRequiredDomains_{0};
  std::atomic<bool> timingReadsReleased_{false};
  uint64_t timingStableSinceNs_ = 0, timingLastHealthyRxNs_ = 0, timingNextOpCheckNs_ = 0;
  bool timingAllRequiredOp_ = false;
  void updateTimingDiscoveryGate(uint64_t receiveTimeNs);
  bool timingStartupReady() const;
  void reportStartupBlockers(bool realtime = false) const;
};
'''

checks = r'''
int main() {
  ecmcEc ec;
  auto cycle = [&](int ms) {
    ec.updateTimingDiscoveryGate(static_cast<uint64_t>(ms) * 1000000);
  };
  auto cycles = [&](int first, int last) {
    for (int ms = first; ms <= last; ++ms) cycle(ms);
  };

  cycles(1, 129); // A required peer remains in SAFEOP.
  assert(!ec.timingReadsReleased_ && !ec.timingStartupReady());
  assert(ec.first.polls < 20 && ec.offline.polls == 0);
  ec.required.state.wc_state = 1;
  ec.reportStartupBlockers();
  assert(diagnostics.find("ec0.s7: configured slave not in OP, AL=0x4 (SAFEOP)") != std::string::npos);
  assert(diagnostics.find("Test terminal") != std::string::npos);
  assert(diagnostics.find("error flag=1") != std::string::npos);
  assert(diagnostics.find("domain 0: PDO working counter=2") != std::string::npos);
  assert(diagnostics.find("s9") == std::string::npos);
  diagnostics.clear();
  const int offRtBefore = offRtCalls;
  ec.reportStartupBlockers(true);
  assert(offRtCalls == offRtBefore && diagnostics.find("ec0.s7") != std::string::npos);

  ec.late.state = {1, 1, EC_AL_STATE_OP};
  cycles(130, 200); // OP alone is insufficient while PDOs are incomplete.
  assert(!ec.timingReadsReleased_);
  ec.required.state.wc_state = EC_WC_COMPLETE;
  cycles(201, 250);
  cycle(280); // A missed interval discards the partial window.
  cycles(281, 350);
  assert(!ec.timingReadsReleased_);
  ec.required.state.wc_state = 1;
  cycle(351); // A single invalid required-domain sample also resets the window.
  ec.required.state.wc_state = EC_WC_COMPLETE;
  cycles(352, 451);
  assert(!ec.timingReadsReleased_);
  cycles(452, 461);
  assert(ec.timingReadsReleased_);
  assert(!ec.timingStartupReady());
  diagnostics.clear();
  ec.reportStartupBlockers(true);
  assert(diagnostics.find("ec0.s7: internal timing reads") != std::string::npos);
  ec.late.timingReady = true;
  assert(ec.timingStartupReady()); // An offline optional device never holds startup.
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-bus-timing-startup-') as directory:
    cpp = Path(directory) / 'timing.cpp'
    exe = Path(directory) / 'timing'
    cpp.write_text(harness + gate + report + checks)
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        str(cpp), '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
print('Startup timing: required OP, stable PDOs, optional devices and blocker diagnostics passed.')
