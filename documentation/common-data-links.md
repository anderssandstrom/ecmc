# Common scalar data links

The common input layer is implemented in production source. It is not a
simulation driver. Hardware-free tests compile and exercise production common
access, normal encoder/monitor input handling and configuration/lifetime rules.
IOC/hardware and production RT performance validation remain outstanding; no
IOC build is performed for this change.

## Data path

```text
EtherCAT entry buffer / registered native data item / readable axis value
                         |
                 ecmcDataAccess
                         |
                  ecmcDataLink
                         |
           normal encoder / monitor / drive / PLC input
```

`ecmcDataAccess` describes existing scalar storage, datatype, bit width,
validity and write-publication behavior. It owns no signal value. `ecmcDataLink`
keeps compact pointer/bit slots; ownership handles are kept separately and
reference counts are not changed by cyclic reads/writes. Storage is read
directly, without a PLC-to-EtherCAT entry wrapper or a second value buffer.

The normal encoder, monitor, drive, position-compare, PVT and PLC entry-link
consumers now use this neutral class. Axis status and sequencer mode links also
use neutral descriptors. EtherCAT configuration functions export the existing
entry buffer and filtered domain-validity field. Existing whole-value/double
write publication and bit-write behavior are retained by the descriptor.

The old `ecmcEcEntryLink` header and historical method spellings remain as
source-compatibility interfaces. Internal C++ class layouts have changed:
rebuild external C++ clients that use these classes. The legacy plugin C ABI
descriptor and existing configuration commands are unchanged; this is not a
claim of binary compatibility for clients compiled against internal C++ layouts.

## PLC source configuration

The initial command interface supports an existing `static.*` or `global.*`
scalar registered in a named producer PLC:

```iocsh
# Compile the producer and configure axis 2 first.
# Feed the normally configured encoder's raw position input; retain its scaling.
ecmcConfigOrDie "Cfg.LinkPLCDataToObject(0,global.position,ax2.enc.actpos,0)"

# Positive PLC value means switch input 1; zero/negative means input 0.
ecmcConfigOrDie "Cfg.LinkPLCDataToObject(0,static.limit,ax2.mon.lowlim,1)"

# Stop runtime before unlinking. The prior binding is restored if still alive.
ecmcConfigOrDie "Cfg.UnlinkDataFromObject(ax2.mon.lowlim)"
```

The encoder destination is the encoder selected for configuration, consistent
with existing encoder-link commands (`Cfg.SelectAxisEncConfig`). These links
feed normal hardware-input processing, not external trajectory/encoder modes.
The source position is in the destination encoder's raw input units; the link
does not silently apply an engineering-unit conversion.

Conversion argument:

| Value | Meaning |
| --- | --- |
| `0` | Native scalar access. F64 encoder/analog inputs remain floating point. |
| `1` | Positive numeric value maps to Boolean 1, zero/negative to 0. |
| `2` | Exact unsigned integer access, for example a PLC status word. Fractional, negative, nonfinite or out-of-range floating values are invalid. |

Use an explicit conversion when a PLC double supplies an integer/Boolean input.
Encoder, monitor and drive input slots are supported; command/control-output
slots are rejected by the command. Monitoring feature enable flags, switch
polarity, encoder scaling and other consumer configuration remain explicit
existing settings. Linking does not enable motion or enable a monitor feature.

Both application mode command and status must be configuration mode. A
destination cannot be stacked with another PLC link: unlink it first. A failed
source lookup/type binding leaves the previous destination unchanged.

## Axis sources without a PLC program

`Cfg.LinkDataToObject(source,destination,conversion)` resolves any readable axis
variable supported by PLC syntax, directly from the configured axis. A PLC
program need not mention the variable and no producer PLC needs to exist:

```iocsh
# Read axis 1's engineering-unit position into axis 2's raw encoder input.
# Configure axis 2's encoder scaling explicitly to match those units.
ecmcConfigOrDie "Cfg.LinkDataToObject(ax1.enc.actpos,ax2.enc.actpos,0)"

# drv.enable is the requested enable status; drv.enabled is drive feedback.
# Boolean axis sources need no explicit conversion to a switch input.
ecmcConfigOrDie "Cfg.LinkDataToObject(ax1.drv.enable,ax2.mon.lowlim,0)"
ecmcConfigOrDie "Cfg.UnlinkDataFromObject(ax2.mon.lowlim)"
```

Names are resolved and descriptors cached on first use during configuration.
Scalar position, velocity, error and counter fields expose their native storage
and type; raw 64-bit integers retain their precision. Packed status bits and
computed values use a reader resolved at configuration, sharing the axis reader
used by PLCs. This requires no periodically refreshed proxy value. The descriptors
are read-only: linking an enable **source** does not issue an enable command.
This command does not create additional `plcs.*` asyn parameters.

Both link commands accept the same input destinations and configuration-mode
rules. Non-Boolean axis sources still require conversion `1` or `2` for integer
or Boolean destinations. Invalid names leave the destination untouched. An axis
source becomes invalid when its owning axis is destroyed. It does not become
invalid merely because the axis is disabled, stopped or reporting a motion error;
those values remain useful to other consumers. Existing EtherCAT health and
lost-frame tolerances are not changed.

Axes retain their existing execution order. A source axis that has already
executed supplies this cycle's state; a source that executes later supplies its
last state. Different signals within one axis and reciprocal links between axes
are allowed. Only an identical source/destination signal on the same axis is
rejected (for example, `ax1.enc.actpos` to `ax1.enc.actpos`). Axis indices are
compared numerically, so alternative spellings do not bypass the check. This
is a configuration check of canonical signal names, not a general feedback
analysis. Links read existing state without recursively evaluating another axis;
there is no loop solver, extra execution pass or implicit rescheduling. Feedback
therefore follows the existing sample and execution order. Dependencies in PLC
programs are not analysed.

## Lifetime, scheduling and validity

The producer's variable is resolved during configuration. Global storage belongs
to the PLC manager; static storage belongs to the PLC task. A linked producer
is pinned against reload, expression replacement and deletion. Unlink all its
inputs before changing its program. `ERROR_PLC_DATA_SOURCE_BOUND` reports this
condition. Destruction/invalidation also makes retained source descriptors
unreadable; a retained descriptor does not keep the source value alive.

Producer membership is indexed per destination axis during configuration, so
cyclic health checks do not scan unrelated links. Producer health is refreshed
immediately before each destination axis executes,
after that axis's PLC. Same-axis PLC writes are therefore visible in that cycle.
General PLCs run later and their writes are visible at the next axis execution.
Existing initialized variable contents are available during startup; this is
not a requirement that the producer has already executed once.

Disabled, uncompiled or faulted producers and a producer held by its EPICS-start
setting report unhealthy. A configured slower PLC rate and an unchanged value
are valid; no new per-frame timeout or freshness watchdog is added. Global
variables may have multiple writers: the named PLC is a declared producer,
not proof of exclusive ownership or proof that the program assigns the variable.

An unhealthy read leaves the consumer's previous input untouched and reports
the normal data/domain error to existing encoder/monitor/drive processing.
Existing interlock/error/reset behavior remains responsible for motion response;
the binding does not implement a new stop policy or automatic motion restart.
Nonfinite floating values on new native bindings are invalid. Existing EtherCAT
floating-entry behavior and configured lost-frame tolerance remain unchanged.

Metadata/pointer changes must go through the owning data item's invalidation
API, under existing synchronization. Direct mutation of `getDataItemInfo()`
while linked is outside the contract. Runtime entry revalidates configured
links and refreshes their health without a name lookup in the cyclic path.

## Scope and validation

Registered data items can export the neutral descriptor through C++
`getDataAccess()` and be bound with `setDataAtIndex`. The initial command interface
resolves PLC static/global sources and readable axis sources. Identical signal
self-links are rejected; different signals on the same axis and reciprocal
cross-axis links are allowed. Array links, generic command
destinations, analysis of PLC program dependencies and a simulated I/O driver
are not implemented.

Tests cover common scalar access/publication, on-demand axis names, native
position/bit/computed sources, same-axis and reciprocal links, identical signal
self-link rejection and owner invalidation, PLC
producer pins and faults, restore/unlink, runtime-entry validation and real
encoder/monitor inputs.
Logging/asyn and producer endpoints are controlled fixtures, not IOC execution.
See [the expectation inventory](../tests/EXPECTATIONS.md).

There is still one cyclic RT thread. Shared descriptors are prepared before RT;
no new per-signal transport dispatch or value buffer is introduced. Computed
axis sources use a numeric-reader callback; ordinary native scalar sources and
EtherCAT entry-word reads retain direct storage access. Explicit
PLC links add producer-health work and an optional branch in the axis loop.
This does not establish performance parity: measure the production cycle,
jitter, deadlines and memory/cache effects against the baseline before rollout.

A reproducible [access probe](../tools/bench-data-access.py) compares the original
entry-link code at a supplied pre-migration revision with the current common
layer. It compiles standalone test code with a small fake entry provider:

```sh
CXX=clang++ python3 tools/bench-data-access.py --baseline <pre-migration-revision>
```

The hot link slot remains 16 bytes on the tested 64-bit host. One local Clang
`-O2` probe after separating extended checks from the direct-status path measured
median read cost 2.86 ns old / 2.04 ns new and write cost 2.96 ns old / 2.62 ns new.
These figures are a limited microbenchmark, not stable performance guarantees;
the fake provider, compiler, host load and cache state affect them. Custom
`getDomainOK` overrides retain their existing semantics through a compatibility
callback; their cost needs separate evaluation. Ordinary in-tree EtherCAT
providers use direct filtered-status pointers.
