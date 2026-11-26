#ifndef ECMC_PLC_DIAGNOSTIC_H
#define ECMC_PLC_DIAGNOSTIC_H
// Metadata is generated during compilation and synchronized by Store::mutex.
#include <array>
#include <mutex>
#include <string>
#include <utility>
#include "ecmcDefinitions.h"

namespace ecmcPlcDiagnostic {
constexpr size_t capacity = 65536;
constexpr int count = ECMC_MAX_PLCS + ECMC_MAX_AXES;
struct Entry {
  std::string metadata;
  std::string response;
  int status = 0;
};
struct Store {
  std::mutex mutex;
  std::array<Entry,count> entries;
};
inline Store& store() { static Store value; return value; }
inline bool valid(int index) { return index>=0 && index<count; }

inline void invalidate(int index) {
  if (!valid(index)) return;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.entries[index]=Entry{};
}
inline void publish(int index,std::string metadata) {
  if (!valid(index)) return;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.entries[index]=Entry{};
  if (metadata.size()+64<=capacity) s.entries[index].metadata=std::move(metadata);
}
inline bool request(int index,int token) {
  if (!valid(index) || token<=0) return false;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  auto& e=s.entries[index];
  e.response.clear();
  e.status=-token;
  if (e.metadata.empty()) return true;
  try {
    e.response="ECMCPLC1 "+std::to_string(token)+"\n"+e.metadata;
    e.status=token;
  } catch (...) { e.response.clear(); }
  return true;
}
inline int status(int index) {
  if (!valid(index)) return 0;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.entries[index].status;
}
inline bool read(int index,char* data,size_t max,size_t* actual) {
  if (!valid(index) || !data || !actual) return false;
  auto& s=store();
  std::lock_guard<std::mutex> lock(s.mutex);
  const auto& e=s.entries[index];
  *actual=0;
  if (e.response.size()>max) return false;
  e.response.copy(data,e.response.size());
  *actual=e.response.size();
  return true;
}
}
#endif
