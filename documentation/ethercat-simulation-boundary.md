# EtherCAT dependencies and a simulation boundary

The common scalar layer now has a [production implementation](common-data-links.md).
The initial cyclic I/O interface and EtherCAT adapter are now implemented.
A model-free [memory simulation backend](simulation-io.md) is also implemented.
There is no production performance result yet.
Preserve existing installations, one cyclic RT thread,
execution order and production performance. Simulation should use the existing
input/output phases rather than add backend dispatch to every signal access.

## Architectural direction

Provide separate EtherCAT and simulation drivers implementing the same ECMC I/O
interface. Keep simulation behavior in its own driver and device models, rather
than adding simulation branches to EtherCAT, encoder, drive or monitor logic.
The common interface is an ECMC contract; no external framework is selected.
The [ecmcIoDriver](../devEcmcSup/com/ecmcIoDriver.h) interface and
[ecmcEthercatDriver](../devEcmcSup/ethercat/ecmcEthercatDriver.h) adapter are
implemented as ordinary classes, without templates or a driver factory.
`ecmcSimulationDriver` implements memory-backed scalar I/O. The common vocabulary is inputs, outputs,
signals, validity and timing; EtherCAT masters, slaves, PDOs and domains remain
implementation-specific concepts.

```text
                   ECMC control logic
                           |
                     ecmcIoDriver
                           |
                 +---------+----------+
                 |                    |
          EtherCAT driver      Simulation driver
          EtherLab / PDOs      Typed memory / external models
```

| Responsibility | Common contract | Driver-specific implementation |
| --- | --- | --- |
| Configuration | Resolve signals, validate type/direction and establish stable bindings before RT | EtherCAT mapping or simulation signal/model configuration |
| Input phase | Update bound inputs and cycle health/timing before control execution | EtherLab receive/domain processing or simulated feedback generation |
| Signal access | Bound buffers and existing conversion/bit semantics | Storage is prepared before RT; no new driver dispatch per signal |
| Output phase | Consume bound outputs at the existing send position | EtherLab output processing/send or simulation command consumption |
| Health and timing | Expose validity and timing quality, including failures | Physical communication/DC state or explicit simulated status/time |

Select the implementation before runtime. Both phases execute synchronously on
the existing single cyclic RT thread; the simulator adds no worker thread.
Preserve the current ordering of checks, publication and control producers
during migration. The table describes responsibilities, not new call placement.
EtherCAT-specific setup and mailbox services remain separate from the cyclic
contract. Existing configuration commands and plugins retain compatibility
through adapters as needed.

## Implemented cyclic interface

Configuration, cyclic I/O and device simulation remain separate responsibilities.
`cyclic_task` constructs a non-owning concrete EtherCAT adapter once on RT entry.
Its methods forward to the existing master; it owns no buffers, copied I/O image,
thread, lock or backend-selection state. Configuration and activation stay in
the existing APIs. Simulated instances are selected explicitly during configuration
and validated before runtime; see [simulation-io.md](simulation-io.md).

| Operation | Existing EtherCAT operation | Placement/meaning |
| --- | --- | --- |
| `isActive()` | `getInitDone()` | Sample once at the start of the cycle. Transport initialization is separate from communication health. |
| `readInputs(inputTime,timeOffset)` | `receive(inputTime,timeOffset)` | Existing receive position, before control logic. |
| `updateIoStatus()` | `checkDomainsState()`, then `statusOK()` | Existing post-input position; retains domain filtering, offline policy and integer status. |
| `writeOutputs(timeOffset)` | `send(timeOffset)` | Existing send position, after safety and publication. |
| `setTimeOffset(timeOffset)` | `setTimeOffest(timeOffset)` | Existing `MASTER_ID=-1` fallback; no bus I/O. |
| `timingReady()` | `timingStartupReady()` | Existing startup-gate checks, independently of communication status. |
| `resetStartupErrors()` | `resetStartupCommunicationErrors()` | Existing one-time startup reset. |
| `updateDiagnostics()` | `checkState()` | Existing slow-status position/divider. |
| `slowExecute()` | `slowExecute()` | Existing slow-work position, after axis slow work. |

The input timestamp is the existing `CLOCK_MONOTONIC` sample. The offset is
`CLOCK_REALTIME - CLOCK_MONOTONIC`, sampled at activation. Both keep their
existing `timespec` representation. The adapter adds no clock reads or unit
conversions; application-time sampling and clock synchronization remain inside
`ecmcEc::send` at their original positions.

The caller keeps the same conditions and order. An initialized but unhealthy
bus still receives/processes/sends data. An uninitialized real master performs
neither bus I/O nor the masterless offset fallback. With `MASTER_ID=-1`, receive,
health/timing checks and bus slow work are skipped and the offset is updated.
There is still only one cyclic RT thread.

`ecmcIoDriver` defines the internal common contract. Production calls use the
concrete `final` EtherCAT type, allowing the forwarding methods to inline and
devirtualize. Backend selection happens outside the
cyclic signal-access path. There is no new per-signal driver dispatch.

Validation, EtherCAT activation, plugin enter/exit hooks, shared-memory master
identity and cleanup remain on their existing paths. The adapter has no new
activation/stop behavior. Existing configuration commands and the C plugin ABI
are unchanged. Signal lookup uses the common data-link configuration, separately
from this cyclic driver interface.

The memory simulation driver implements these operations without a device model.
It exposes shared typed storage and explicit health. The input/output phases
record timing without synthesizing feedback. A future device model may consume
control/setpoint values and supply status/position through these bindings.
External PV clients are asynchronous and do not supply deterministic RT feedback.
No simulation worker thread, event queue or extra image copy is introduced.

### Stable bindings and shared state

A signal binding needs the following information, resolved before runtime:

- Stable value storage, type/width, direction and bit-field metadata.
- A stable association with its I/O validity group, not just one global
  driver-healthy flag. An EtherCAT domain supplies this state in that driver.
- Endpoint timing metadata where required: source/reference, update divisor,
  offset, uncertainty and validity. Backend-specific DC discovery is separate.
- Existing publication integration and error behavior. A bare pointer is not
  a complete replacement for `ecmcEcEntry` because writes can trigger asyn
  updates and must preserve the read/write failure asymmetry described below.

Bindings and shared health/timing storage remain alive and at stable addresses
throughout an RT invocation. Values and health may change; membership does not.
The cyclic thread updates state and control code reads it without adding a
driver call for each signal. Existing EPICS/command-thread access continues to
use its current synchronization; stable addresses alone do not provide thread
safety.

Keep separate observations for communication health, timing-startup readiness,
controller error, cycle timing and endpoint timing quality. Healthy PDO exchange
does not imply completed timing discovery or accurate endpoint timestamps.
Observation must not initiate hardware access or implicitly reset errors.

### Shared signals cover PLCs as well as motion

The current entry type is `ecmcEcEntry`. It is a shared signal object, not just
a motion implementation detail. `ecmcPLCDataIF` now uses `ecmcDataLink` and
resolves PLC variable paths to neutral descriptors with the same bound read/write
operations as motion consumers. PLC library functions additionally use
EtherCAT services and memory maps through separate paths.

There are consequently two contracts to separate: `ecmcIoDriver` handles cyclic
I/O, while common bound signals serve PLCs, drives, encoders, monitors and other
consumers. Do not create a separate signal abstraction just for motion. A
possible later neutral name is `ecmcIoSignal`, but its storage/access design
must first preserve all entry semantics. Do not globally rename `ecmcEcEntry`
or change its public layout/ABI as part of naming the driver interface.

Migration must retain PLC path syntax, alias/bit binding, numeric conversion,
read/write error behavior, publication and runtime binding lifetime. Moving
the RT loop behind `ecmcIoDriver` alone does not complete this migration.

### All configured data links use the common layer

The intended scope is ordinary axis I/O as well as external encoder/trajectory
modes. A PLC variable must be usable as the source of an encoder-position input,
a limit-switch input or another compatible configured input slot. Restricting
this to `setExtSetPos` would not achieve the requested separation.

```text
EtherCAT entries / simulation / PLC global or static variables / axis outputs
                                   |
                   common typed data descriptors and bindings
                                   |
              encoder / monitor / drive / PLC / diagnostic input slots
```

All configured data links should resolve through that shared contract. It is
not a requirement to replace every internal member access with a registry call.
The registry resolves names, capabilities and types before runtime; cyclic use
accesses the established binding. Keep driver transport separate from this
source-to-consumer data graph.

A destination is a specific supported input, not arbitrary writable axis state.
For example, a position source feeds the encoder's normal input path, retaining
scaling, rollover, filtering and diagnostics; a limit source feeds the monitor's
normal input path, retaining polarity and interlock behavior. Published axis
actual position can be a source, but writing its displayed status field is not
an equivalent encoder connection. Commands retain their handlers.

`global.*` and producer-owned `static.*` PLC variables are both in scope. Static
storage is destroyed by PLC reload, so a live link requires lifetime pinning or
explicit invalidation before destruction. Do not cache a static address across
reload without that protection. Producer health, declared update rate and
source validity remain separate from pointer lifetime.

Validate native types, bit fields, sizes, direction and units at binding time.
A PLC scalar is a double; it must not silently stand in for every integer/raw
encoder format. Any allowed conversion must be explicit and bound before RT.
Define multiple-writer ownership and cycle visibility. Reject identical signal
self-links, without treating an entire axis as a single graph node. Different
signals on the same axis and reciprocal cross-axis links are allowed and read
existing state in the normal execution order. General feedback analysis is not
implemented; a deliberately specified delay would need a separate contract.

Normal encoder and monitor inputs now use the shared contract and have dedicated
production-code checks. The earlier PLC-to-external-setpoint pilot only checks
scalar transfer and is not the live input implementation. The unfinished axis-specific live-link experiment was
removed before wiring it into the RT path.

### Common data binding implementation

`ecmcDataAccess` and `ecmcDataLink` now provide the neutral scalar contract.
Normal encoder/monitor/drive/PLC entry consumers use it; EtherCAT exports its
existing buffer directly. Registered data items can export native storage, and
`Cfg.LinkDataToObject` resolves EtherCAT entry/bit paths, producer-qualified PLC
`static.*`/`global.*` sources and lazily resolved readable axis values into the
same neutral bindings for ordinary axis input slots. Whole EtherCAT entries
retain direct descriptor access and the existing filtered domain-health checks.
Bit selection and explicit conversions are resolved before cyclic execution.
PLC link names can be declared with motion configuration before their programs
exist. Final validation compiles PLCs and resolves pending links before validating
axis inputs; missing sources prevent RT entry without partially installing the
pending batch. Axis sources do not require
a PLC expression to register them. Native scalars retain direct storage access;
bit fields and computed sources share the motion reader used by PLCs. Only
identical signal self-links are rejected during configuration; different signals
on the same axis and reciprocal links between axes are allowed. No PLC-to-EtherCAT entry subclass or second value buffer is used.

See [Common scalar data links](common-data-links.md) for commands, source pins,
validity, remaining scope and the explicit build/performance validation status.
The earlier standalone read-binding prototype remains a limited experiment,
not the implementation used by the live input layer.

### Clean consumer interface and migration scope

Consumers now use the neutral data binding directly. Do not introduce a new
PLC/simulation entry subclass of `ecmcEcEntry` as the common interface: that
would keep EtherCAT in the consumer contract and add a parallel buffer/adapter
path. An unfinished experiment with that shape was removed before integration.

Use existing `ecmcDataItem` metadata as the starting point for the shared data
contract. EtherCAT owns its transport/process-image conversion and exposes its
existing native entry storage through that contract. PLCs and simulation expose
their own native storage through the same contract. Encoder/monitor/drive/PLC
links bind to the neutral contract rather than to an EtherCAT wrapper.

A binding is a resolved access handle, not a second value buffer or transport.
Preserve native types, validity associations and the existing read/write and
publication semantics. Command destinations retain their handlers. Work out
these details before changing production access; direct pointer access alone
is insufficient to preserve all existing behavior.

Configuration compatibility adapters may translate existing entry paths into
neutral bindings. They must not impose extra buffer copies or new per-signal
backend dispatch in the cyclic path. Existing EtherCAT entry APIs can remain
available to old callers during migration, but new consumers must not depend
on those APIs merely to use PLC or simulated data.

Initially keep EtherCAT-specific configuration commands, scan/SDO/SoE operations,
wiring diagnostics and legacy plugin access on the existing objects. They do
not belong in a mandatory interface that a simulator must pretend to support.
Inventory/adapt these remaining consumers before declaring full decoupling.

`MASTER_ID=-1` continues to select the current masterless behavior. Selection of
the memory simulation driver is explicit: `Cfg.SetIoBackend(id,1)` before
`Cfg.EcSetMaster(id)`. Neither driver may report successful timing discovery,
hardware mailbox transfers or device feedback that it cannot actually supply.

The cyclic seam now uses an internal virtual contract with a concrete `final`
EtherCAT adapter at the production call site. Keep forwarding calls inlinable;
any future runtime backend choice must preserve that fast path. No per-signal
transport dispatch is added. The compiler probe below checks representative
phase code, while full IOC performance still requires hardware measurement.
The simulation loops also add empty-list checks in hardware-only operation;
the forwarding probe does not measure that complete cyclic path.

## Current cyclic data flow

```text
cyclic_task (one cyclic RT thread)
  io.readInputs -> ecmcEc::receive
    EtherLab receive -> domain process -> input entries and memory maps
  io.updateIoStatus -> domain/status checks; startup gates
  sequences / axis PLC + axis / PVT / master-slave / plugins / logic / PLCs
  repeated stop/disable enforcement, slow work, safety and publication
  io.writeOutputs -> ecmcEc::send
    cyclic entry copies -> output entries and memory maps -> domain queue
    application time / clock synchronization -> EtherLab send
```

There is one receive phase and one send phase per normal bus-enabled cycle.
Individual domains can run at configured cycle divisors/offsets; an entry can
also have cyclic updating disabled. This does not mean each consumer performs
a hardware transfer. Consumers read and write entry buffers between phases.
Nor are all buffers immutable: producers can update entries during control
execution, making their current order significant.

Sources: [cyclic_task](../devEcmcSup/main/ecmcMainThread.cpp),
[ecmcEc::receive/send and process-image updates](../devEcmcSup/ethercat/ecmcEc.cpp),
[ecmcEcDomain::process/queue](../devEcmcSup/ethercat/ecmcEcDomain.cpp),
[ecmcEcSlave::updateInputProcessImage/updateOutProcessImage](../devEcmcSup/ethercat/ecmcEcSlave.cpp).
The slave input phase also executes async SDOs and timing discovery/publication;
it is not just a PDO memory copy.

## Dependency map

| Consumer or layer | Configuration binding | Cyclic dependency | Consequence for simulation |
| --- | --- | --- | --- |
| Drive base, Stepper and DS402 | `linkEcEntryToAxisDrv` resolves a slave/entry and calls `setEntryAtIndex` | Status, warnings and alarms in; control word, position/velocity, brake and reset out | Preserve real drive state machines and entry buffer access. |
| Encoder | `linkEcEntryToAxisEnc` binds entry pointers and bit indices | Position, readiness, alarms, latch/probe data in; latch/probe controls and reset out; domain checks | Simulate inputs and status, not only ideal axis position. |
| Monitor | `linkEcEntryToAxisMon` binds entries | Limit switches, home sensor, external interlock and analog input | Keep polarity, error behavior and existing monitor timing. |
| Axis | Status-output entry binding plus encoder/drive objects | Master health; status output; direct `ec`/slave access for sample timing, touch probe and position compare | Entry values alone do not remove all EtherCAT dependencies. |
| Position compare, PVT and sequencer | Entry links or stored entry pointers | Scheduled output fields, trigger output and automatic-mode handshake | Preserve timing quality and write order, not just scalar values. |
| PLC data variables | `ecmcPLCDataIF` resolves paths and binds an entry | Entry reads/writes via `ecmcEcEntryLink` | Reuse existing variable paths and conversion behavior. |
| PLC EtherCAT functions | Global `ec`, object indices and library registration | Memory-map operations, timing/status and SDO services | A separate service surface remains beyond entry bindings. |
| C++ logic | Item bindings and host services | Bound-item copies around `runCycle`; asyn publication | Preserve item storage addresses and binding lifetime; it does not share only the motion entry-link path. |
| Memory maps and derived entry data | Domain, start-entry offset, size/type and mapping | Block process-image access, conversion and publication | A scalar-entry simulator alone does not cover these paths. |

Binding sources: [ecmcMotion.cpp](../devEcmcSup/motion/ecmcMotion.cpp),
[ecmcPLCDataIF.cpp](../devEcmcSup/plc/ecmcPLCDataIF.cpp).
Consumer sources: [drive](../devEcmcSup/motion/ecmcDriveBase.cpp),
[encoder](../devEcmcSup/motion/ecmcEncoder.cpp),
[monitor](../devEcmcSup/motion/ecmcMonitor.cpp),
[axis timing](../devEcmcSup/motion/ecmcAxisBase.cpp),
[position compare](../devEcmcSup/motion/ecmcPositionCompare.cpp),
[PVT](../devEcmcSup/motion/ecmcPVTController.cpp),
[sequencer](../devEcmcSup/motion/ecmcAxisSequencer.cpp),
[PLC services](../devEcmcSup/plc/ecmcPLCTask_libEc.inc),
[C++ logic](../devEcmcSup/logic/ecmcCppLogicLib.cpp),
[memory maps](../devEcmcSup/ethercat/ecmcEcMemMap.cpp) and
[derived entry data](../devEcmcSup/ethercat/ecmcEcData.cpp).

## Existing access boundary and compatibility details

Before the common-layer migration, `ecmcEcEntryLink` stored an entry pointer
and bit number in a fixed array. The neutral layer retains a compact resolved
pointer/bit slot. Runtime accesses use these bindings rather than resolving names for each
read/write. `ecmcEcEntry::readValue` reads `buffer_`; `writeValue` updates it and
calls `updateAsyn`. Whole-value/double writes therefore have publication effects,
not just memory effects. Bit operations have their own behavior.

Process-image conversion handlers are already selected by
`bindProcessImageHandlers`; updates call those member-function pointers.
`getDomainOK` and process-image updates are already virtual. Avoid introducing
another interface dispatch at every signal access or replacing existing calls
without measuring the result.

Important asymmetric behavior: link reads check domain validity before returning
a value; link writes update the buffer first, then report a domain error. A
simulator must preserve this distinction. Domain status also includes failure
counters/offline policy, not just master availability.

Although access is buffered, the headers are coupled: `ecmcEcEntry.h` includes
EtherLab, domain, EPICS/asyn and alarm headers; motion headers inherit that
dependency. Separating these includes is a distinct build-dependency task and
does not itself create a simulated runtime.

Sources: [entry links](../devEcmcSup/ethercat/ecmcEcEntryLink.cpp),
[entries](../devEcmcSup/ethercat/ecmcEcEntry.cpp),
[entry header](../devEcmcSup/ethercat/ecmcEcEntry.h) and
[domain health](../devEcmcSup/ethercat/ecmcEcDomain.cpp).

## Communication tolerance and motion response to preserve

Source inspection confirms that `ecmcEcDomain::checkState` filters incomplete
working-counter states. After a complete transfer has reset the counter, a
configured limit N allows N consecutive incomplete checks; check N+1 marks the
domain unhealthy. A complete check resets the counter. This counts domain-state
checks, not individual Ethernet packets or every possible communication fault.
The C++ defaults in both `ecmcEc` and `ecmcEcDomain` are zero; installation
configuration can set a different limit with `Cfg.EcSetDomainFailedCyclesLimit`.

The setter primes `notOKCounter_` to the limit. Thus the first incomplete check
after setting the limit is unhealthy until a complete sample has been seen.
Retain this startup behavior, the exact boundary and recovery policy.

`ecmcEc::checkDomainsState` excludes domains allowed offline from the aggregate
health, but their own `getOK` remains false. `ecmcEc::statusOK` also incorporates
slave/master health and startup readiness, with existing diagnostics/offline
opt-outs. A signal's local validity and the controller's bus summary are
therefore distinct. Do not derive either from a new one-frame-missed flag.

In `ecmcAxisBase::preExecute`, failed overall health sets
`etherCatMasterInterlock` and disables an enabled axis. `ecmcAxisData` maps this
interlock to its existing emergency stop mode. `ecmcAxisReal::execute` also
resets the controller, requests zero velocity output and sets
`ERROR_AXIS_HARDWARE_STATUS_NOT_OK`. This software path does not prove physical
braking, and must not be replaced by a newly assumed normal controlled stop.
The encoder's `readHwActPos` skips its position update on a summary hardware
alarm; entry-link writes retain their existing buffer/error asymmetry.

The requested binding policy is to keep the last valid sample and stop motion
when the source is declared invalid, rather than accept recovery as a new motion
command. For EtherCAT, preserve the above configured tolerance and existing
fault/interlock/reset behavior. Do not add an independent binding timeout or
change existing error-latching semantics. For a PLC source, producer health,
expected execution rate and invalidation must be defined separately; an
unchanged value or a scheduled skipped cycle is not itself a fault.

[The domain-health regression](../tests/test_domain_health.py) steps production
state-check, limit-setting and summary code to protect these boundaries. It does
not exercise EtherLab transport, a running axis or physical stopping. See also
[the existing timeout API comment](../devEcmcSup/ethercat/ecmcEthercat.h),
[real-axis fault handling](../devEcmcSup/motion/ecmcAxisReal.cpp) and
[interlock stop modes](../devEcmcSup/motion/ecmcAxisData.cpp).

## What simulation already exists

- `ecmcEc::init` accepts a negative master ID without requesting an EtherLab
  master. `MASTER_ID=-1` keeps the cyclic control thread but skips receive/send
  in `cyclic_task`; its fallback calls `setTimeOffest`, which only stores the
  offset. It does not reproduce the receive/send cycle timing updates.
- A special simulation slave and simulation-entry constructor exist. Slaves
  create `ZERO` and `ONE` entries (`ONE` is `0xFFFFFFFF`). The simulation entry
  constructor marks entries as outputs and still initializes asyn resources.
  Its process-image address setup must be reviewed before using process-image
  handlers as a general memory-backed PDO backend.
- `ecmcEcEntry::getDomainOK` returns healthy when there is no domain. Existing
  simulation entries therefore do not provide configurable communication loss.
- The [standalone PLC tool](../tools/plc-test/README.md) already tests real
  expressions, virtual motion and controlled EtherCAT services. Its
  [offline entry class](../tools/plc-test/offline/ecmcEcEntry.h) throws for
  hardware reads/writes. This is useful coverage, but not an end-to-end test of
  production entry conversion, real drive I/O and simulated PDO transport.

Keep legacy masterless behavior and a richer simulated backend distinguishable.
Do not silently make `MASTER_ID=-1` invent healthy timing or physical feedback.

## Implemented memory simulation boundary

The separate simulation driver owns typed scalar buffers, stored SDO objects,
configuration metadata and explicit health. Existing slave/entry declarations
and common data links select it through configuration routing; they retain
`ecN.sM.entry` names. Motion and PLC consumers access the same neutral descriptors.
Real and simulated instances may coexist with distinct IDs.

Allocation, name resolution and publication registration occur during
configuration. The driver freezes its publication list before runtime. Input
and output phases execute on the existing cyclic RT thread. Device behavior
remains separate: there is no built-in drive, encoder, loopback or DS402 model.

This is buffer-level simulation. It does not emulate EtherLab process-image
packing, endianness conversion, distributed clocks or physical timing. Several
advanced configuration services remain unsupported. The supported commands,
limitations and examples are listed in [simulation-io.md](simulation-io.md).

## Performance acceptance before production integration

A [compiler probe](../tools/check-io-driver-codegen.py) compares direct EtherCAT
calls with the concrete adapter for input/status/control/output phases:

```sh
CXX=g++ python3 tools/check-io-driver-codegen.py --optimization=-O3
CXX=clang++ python3 tools/check-io-driver-codegen.py --optimization=-O3
```

Both compilers produced identical optimized instruction sequences for the two
probe functions in local checks. This checks forwarding/inlining, not a whole
IOC build, scheduler, hardware transport, cycle time or jitter. The functional
suite exercises the real interface/adapter with a controlled provider and the
existing cycle/startup traces, including masterless and uninitialized-master
cases. The existing domain-health tests protect filtering semantics.

A standalone access probe is described in [common-data-links.md](common-data-links.md).
No IOC/hardware RT performance measurements have been made. The existing
receive/send structure offers a boundary; it does not prove a particular
abstraction will be free.

1. Record a baseline with the same hardware, build flags, CPU scheduling,
   cycle frequency, configured entries, domains and publication settings.
2. Compare the production backend before/after: execution-time distribution
   and maximum, wakeup latency/jitter, send period, missed deadlines/frames,
   and CPU load. Include representative and high-load configurations.
3. Check the placement/meaning of existing timing diagnostics before relying
   on them; they are sampled across loop iterations. Use bounded measurement
   storage, with formatting/reporting outside RT, if additional probes are needed.
4. Require no new per-signal dispatch, allocation, locking, name lookup, logging
   or extra process-image copies attributable to the new boundary. Inspect the
   production call path and optimized code as well as timings.
5. Agree measurement noise and acceptable limits before integration. A passing
   functional suite or a fast simulator cannot establish production RT parity.

Preserve existing entry paths, IDs, types, bit semantics, alarms, startup gates,
error priority, plugin ABI and cycle order throughout migration. The
[expectation inventory](../tests/EXPECTATIONS.md) distinguishes compatibility
checks from unresolved semantic questions. IOC/hardware validation remains
necessary; the adapter is intended to preserve existing production behavior.
