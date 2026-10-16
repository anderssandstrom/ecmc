# C++ logic concurrency

The cyclic C++ logic runs on ECMC's single realtime thread. EPICS port handlers
and the callback worker are separate threads.

## Lock order

- CPP port handlers: CPP port lock, then main ECMC port lock, then callback queue
  mutex if publishing a change.
- RT: the existing main ECMC port lock, then a single `try_lock` attempt on the
  callback queue mutex when handing off staged updates. A busy queue is skipped.
- Callback worker: queue mutex to detach pending snapshots, release it, then CPP
  port lock to update the parameter cache and deliver callbacks. It never takes
  the main ECMC port lock.

No path may acquire the CPP port lock while holding the main ECMC lock. C++ logic
must use the provided host services, not synchronous asyn calls to CPP ports from
its realtime callback. The worker must never hold the queue mutex while waiting
for another lock or calling EPICS.

Live scalar/array reads and writes, runtime rate changes, and alias metadata
changes are serialized with RT by the main port lock. Runtime scalar reads use
the published asyn cache, protected by the CPP port lock. Before IOC startup,
scalar reads populate that cache under the main lock so initialization and
restore reads do not depend on the callback worker.

## Callback snapshots

Each parameter has staging, pending, and delivery buffers allocated at
registration. Staging replaces the previous change-detection buffer, so this
refinement does not add a full-sized buffer per exported waveform. A producer
copies a complete value into staging under the main lock, records it in a
producer-owned dirty list, and then attempts to flush that list to the queue.

RT uses `try_lock` once per flush attempt. If it fails, both the staged values
and any required startup wakeup remain pending. At the next invocation of that
module's RT function, a retry runs before execution-rate and enable checks.
Thus an unchanged final value is retried even when execution is disabled or
runs only every N cycles. Repeated updates coalesce to the latest staged value;
this is a monitoring interface, not a lossless sample stream.

A successful flush copies dirty staged values into pending buffers. The worker
swaps pending/delivery buffers and detaches the pending list under the mutex.
It then releases the mutex and uses only its delivery buffers. The worker does
not access staging or the dirty list. Non-RT writes and lifecycle calls use a
blocking flush, including a final drain when RT stops, so delivery does not
depend on another RT cycle after shutdown.

RT never updates the CPP asyn parameter cache or invokes its callbacks. Scalar,
string, and array publication all use the worker. The main lock is not held by
the worker during conversion, allocation, or EPICS callback delivery. Ordinary
export writes publish only the changed parameter rather than scanning every
exported waveform.

The queue handoff allocates no memory in RT. The worker's queue critical section
swaps pointers rather than copying waveform data. Producer copies remain
proportional to parameter size; large waveform exports still require timing
measurement on the target system. Snapshots are coherent per parameter, not a
transaction across several parameters. RT does not wait directly for the queue
mutex, but the shared main lock can still delay RT behind non-RT reads/writes;
those writes may themselves briefly wait for the queue mutex.

## Multiple modules

Every loaded C++ logic module retains its existing worker and private queue.
A failed queue acquisition for one module does not prevent processing the next
module. Idle retry checks are O(1) per module; flushes walk only dirty parameters,
not every export. Regular change detection and waveform copies still scale with
the total exported data and publication frequency. Many workers also add CPU
scheduling and EPICS callback load. Use each module's `update_rate_ms` to control
publication frequency independently of its execution rate. No hard latency
bound is implied; measure worst-case RT timing on the target IOC.

## Lifetime

`LoadCppLogic` is a startup command and must run before `iocInit`. Loading or
replacing a module after IOC startup is rejected; restart the IOC instead.
Joining a live port's worker while holding the main command lock could otherwise
wait for a CPP handler that is itself waiting for that main lock. Direct C++
object destruction likewise requires all clients to be quiescent; it is not a
hot-unload API.

## Validation

`python3 tests/test_cpp_logic_locking.py` compiles the production guard, snapshot
queue, and worker against fake asyn endpoints. It checks main-lock exclusion,
recursive locking, immutable delivery during concurrent publication, coalescing,
producer progress during a slow callback, forced queue contention, final-value
retry without a new change, startup-wakeup retry, independent queues for multiple
modules, and shutdown without acquiring the main lock. `CXXFLAGS=-fsanitize=thread` additionally enables ThreadSanitizer where
supported. The harness does not replace integration testing with actual asyn.

On the target IOC, exercise scalar and array PV writes while RT is active,
callback delivery under load, execution enable/disable, rate changes, startup
restore, runtime stop/start, and rejection of post-startup loading. Compare RT
latency with and without large waveform publication.
