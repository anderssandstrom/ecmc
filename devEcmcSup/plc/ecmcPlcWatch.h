#ifndef ECMC_PLC_WATCH_H
#define ECMC_PLC_WATCH_H
#include <atomic>
#include <array>
#include <mutex>
#include <cstddef>
#include <limits>
#include "ecmcDefinitions.h"

namespace ecmcPlcWatch {
constexpr unsigned limit=16;
constexpr int count=ECMC_MAX_PLCS+ECMC_MAX_AXES;
static_assert(ATOMIC_INT_LOCK_FREE==2,"PLC watch requires lock-free integers");
#if defined(__GNUC__)
static_assert(__atomic_always_lock_free(sizeof(double),nullptr),"PLC watch requires lock-free doubles");
#endif
struct Slot {
  std::atomic<unsigned> request{0}, done{0};
  std::array<std::atomic<unsigned>,limit> ids{};
  std::array<std::atomic<double>,limit> values{};
  std::atomic<unsigned> length{0};
  std::mutex mutex;
  unsigned generation=0, available=0, selected=0;
  int token=0;
  std::array<unsigned,limit> selection{};
};
inline Slot* slot(int i) {
  static std::array<Slot,count> slots{};
  return i>=0 && i<count ? &slots[i] : nullptr;
}
inline void configure(int i,unsigned n) {
  auto* s=slot(i);
  if (!s) return;
  std::lock_guard<std::mutex> lock(s->mutex);
  s->request.store(0,std::memory_order_release);
  s->available=n;
  s->selected=0;
  s->token=0;
}
inline bool select(int i,const int* ids,size_t n) {
  auto* s=slot(i);
  if (!s || !ids || !n || n>limit) return false;
  std::lock_guard<std::mutex> lock(s->mutex);
  for(size_t j=0;j<n;++j)
    if(ids[j]<0 || static_cast<unsigned>(ids[j])>=s->available) return false;
  for(size_t j=0;j<n;++j) s->selection[j]=ids[j];
  s->selected=n;
  return true;
}
inline bool request(int i,int token) {
  auto* s=slot(i);
  if(!s || token<0) return false;
  std::lock_guard<std::mutex> lock(s->mutex);
  s->request.store(0,std::memory_order_release);
  s->token=token;
  if(!token || !s->selected || s->generation==std::numeric_limits<unsigned>::max()) return true;
  for(unsigned j=0;j<s->selected;++j) s->ids[j].store(s->selection[j],std::memory_order_relaxed);
  s->length.store(s->selected,std::memory_order_relaxed);
  s->request.store(++s->generation,std::memory_order_release);
  return true;
}
inline int status(int i) {
  auto* s=slot(i);
  if(!s) return 0;
  std::lock_guard<std::mutex> lock(s->mutex);
  unsigned gen=s->request.load(std::memory_order_acquire);
  if(!gen) return -s->token;
  return s->done.load(std::memory_order_acquire)==gen ? s->token : 0;
}
inline bool read(int i,double* data,size_t max,size_t* actual) {
  auto* s=slot(i);
  if(!s || !data || !actual) return false;
  *actual=0;
  std::lock_guard<std::mutex> lock(s->mutex);
  unsigned gen=s->request.load(std::memory_order_acquire);
  if(!gen || s->done.load(std::memory_order_acquire)!=gen) return false;
  unsigned n=s->length.load(std::memory_order_relaxed);
  if(n>limit || max<1+2*n) return false;
  data[0]=s->token;
  for(unsigned j=0;j<n;++j) {
    data[1+2*j]=s->ids[j].load(std::memory_order_relaxed);
    data[2+2*j]=s->values[j].load(std::memory_order_relaxed);
  }
  *actual=1+2*n;
  return true;
}
}
#endif
