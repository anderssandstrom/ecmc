#ifndef ECMC_PLC_DIAGNOSTIC_H
#define ECMC_PLC_DIAGNOSTIC_H
// Metadata is generated during compilation and synchronized by Store::mutex.
#include <array>
#include <algorithm>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <locale>
#include <chrono>
#include <mutex>
#include <string>
#include <utility>
#include "ecmcDefinitions.h"

namespace ecmcPlcDiagnostic {
constexpr size_t capacity = 65536;
constexpr int count = ECMC_MAX_PLCS + ECMC_MAX_AXES;
// Negative commands request a page of up to 32 registered variables.
// Low six bits select the page, remaining bits identify the request.
constexpr unsigned snapshotSize=32;
static_assert(ATOMIC_INT_LOCK_FREE==2,"PLC diagnostics require lock-free int atomics");
struct Snapshot {
  std::atomic<int> state{0}; // 0 idle, 1 requested, 2 copying, 3 ready
  unsigned page=0, size=0;
  double values[snapshotSize]{};
};
inline Snapshot* snapshots() { static Snapshot slots[count]; return slots; }
struct Entry {
  std::string metadata;
  std::string response;
  int status = 0;
  int snapshotToken = 0;
  unsigned variables = 0;
};
struct Store {
  std::mutex mutex;
  std::array<Entry,count> entries;
  std::chrono::steady_clock::time_point nextRequest{};
};
inline Store& store() { static Store value; return value; }
inline bool valid(int index) { return index>=0 && index<count; }

inline void invalidate(int index) {
  if (!valid(index)) return;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  snapshots()[index].state.store(0,std::memory_order_relaxed);
  s.entries[index]=Entry{};
}
inline void publish(int index,std::string metadata,unsigned variables=0) {
  if (!valid(index)) return;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.entries[index]=Entry{};
  snapshots()[index].state.store(0,std::memory_order_relaxed);
  if (metadata.size()+64<=capacity) { s.entries[index].metadata=std::move(metadata); s.entries[index].variables=variables; }
}
inline bool request(int index,int token) {
  if (!valid(index) || token==0 || token==(-2147483647-1)) return false;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  // IOC-wide admission control; never called from cyclic PLC execution.
  const auto now=std::chrono::steady_clock::now();
  auto& slot=snapshots()[index];
  int state=slot.state.load(std::memory_order_acquire);
  if (state==2) return false;
  // Cancel an unserved request only through CAS; never overwrite a copying slot.
  if (now<s.nextRequest) return false; // Preserve the accepted response.
  if (state==1 && !slot.state.compare_exchange_strong(state,0,std::memory_order_acq_rel)) return false;
  if (state==3) slot.state.store(0,std::memory_order_relaxed);
  s.nextRequest=now+std::chrono::seconds(1);
  auto& e=s.entries[index];
  e.response.clear();
  e.snapshotToken=0;
  e.status=token<0 ? 0 : -token;
  if (e.metadata.empty()) return true;
  if (token<0) {
    unsigned page=static_cast<unsigned>(-token)&63u;
    unsigned start=page*snapshotSize;
    if (start>=e.variables) return false;
    slot.page=page;
    slot.size=std::min(snapshotSize,e.variables-start);
    e.snapshotToken=token;
    slot.state.store(1,std::memory_order_release);
    return true;
  }
  try {
    e.response="ECMCPLC1 "+std::to_string(token)+"\n"+e.metadata;
    e.status=token;
  } catch (...) { e.response.clear(); }
  return true;
}
// Called only by the PLC execution thread. No store mutex, formatting or allocation.
template<class Reader> inline void capture(int index,Reader readValue) {
  auto& slot=snapshots()[index];
  if (slot.state.load(std::memory_order_relaxed)!=1) return;
  int expected=1;
  if (!slot.state.compare_exchange_strong(expected,2,std::memory_order_acquire)) return;
  for (unsigned i=0;i<slot.size;++i) slot.values[i]=readValue(slot.page*snapshotSize+i);
  slot.state.store(3,std::memory_order_release);
}
inline void finish(int index,Entry& e) {
  auto& slot=snapshots()[index];
  if (!e.snapshotToken || slot.state.load(std::memory_order_acquire)!=3) return;
  try {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<"ECMCPLCV1 "<<e.snapshotToken<<" "<<slot.page*snapshotSize<<" "<<slot.size<<"\n";
    out<<std::setprecision(17);
    for (unsigned i=0;i<slot.size;++i) out<<slot.values[i]<<"\n";
    e.response=out.str();
    e.status=e.snapshotToken;
  } catch (...) { e.response.clear(); e.status=0; }
  e.snapshotToken=0;
  slot.state.store(0,std::memory_order_relaxed);
}
inline int status(int index) {
  if (!valid(index)) return 0;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  finish(index,s.entries[index]);
  return s.entries[index].status;
}
inline bool read(int index,char* data,size_t max,size_t* actual) {
  if (!valid(index) || !data || !actual) return false;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  auto& e=s.entries[index];
  finish(index,e);
  *actual=0;
  if (e.response.size()>max) return false;
  e.response.copy(data,e.response.size());
  *actual=e.response.size();
  return true;
}
}
#endif
