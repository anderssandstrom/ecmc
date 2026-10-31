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
descriptor and pre-existing EtherCAT configuration commands are unchanged; this is not a
claim of binary compatibility for clients compiled against internal C++ layouts.

## One link command

`Cfg.LinkDataToObject(source,destination,conversion)` resolves all currently
supported input source families through `ecmcDataAccess` and `ecmcDataLink`:

| Source | Example |
| --- | --- |
| EtherCAT entry or selected bit | `ec0.s3.Position`, `ec0.s3.Inputs.2` |
| General PLC static/global variable | `plcs.plc0.static.limit`, `plcs.plc0.global.position` |
| Axis PLC static/global variable | `plcs.ax1.static.limit` |
| Readable axis value | `ax1.enc.actpos`, `ax1.drv.enable` |

Source lookup and view creation happen before cyclic runtime. PLC source names
are deferred until final configuration validation. There is one
resolved binding at the consumer; no second value buffer or cyclic name lookup.
The original EtherCAT configuration commands remain accepted with their existing
behavior and use the same neutral consumer access layer.

## EtherCAT source configuration

```iocsh
# Native word access and the normal encoder scaling/processing path.
ecmcConfigOrDie "Cfg.LinkDataToObject(ec0.s3.Position,ax2.enc.actpos,0)"

# Select bit 2. Both integer and numeric reads see 0 or 1.
ecmcConfigOrDie "Cfg.LinkDataToObject(ec0.s3.Inputs.2,ax2.mon.lowlim,0)"
ecmcConfigOrDie "Cfg.UnlinkDataFromObject(ax2.mon.lowlim)"
```

The master must match the configured master. Nonnegative slave numbers identify
bus positions; `s-1` retains the existing simulated-slave convention. Entry aliases
and optional bit indices use the existing EtherCAT path parser. Missing entries,
wrong masters, invalid bit ranges and bit selection on floating entries are
rejected before replacing a binding.

Conversion `0` for a whole entry binds the provider's existing descriptor
directly. It retains the entry's filtered domain-health pointer, native datatype
and raw word precision. Bit selection and explicit numeric conversions prepare
a read view during configuration, retaining the original descriptor for lifetime
and health. Bit selection produces a Boolean value; conversions `1` and `2` do
not change its 0/1 result. No receive/send calls are added to signal access.

Bus-health errors leave the previous consumer input untouched. Existing domain
failure filtering and lost-frame tolerances remain authoritative; configuration
and runtime-entry lifetime checks do not demand an already healthy bus. Entry
invalidation makes retained native and converted/bit bindings unreadable.
Native whole-entry floating reads retain their existing semantics; explicit
conversions validate their numeric inputs. Floating entries supplying integer
inputs require conversion `1` or `2`.

## PLC source configuration

PLC sources use `static.*` or `global.*` scalars in a named producer PLC, using
the same `LinkDataToObject` command as EtherCAT and axis sources. Declare the
links while configuring motion; load the PLC programs last:

```iocsh
# Hardware and axis 2 are configured first; PLC 0 need not exist yet.
# Feed the normally configured encoder's raw position input; retain its scaling.
ecmcConfigOrDie "Cfg.LinkDataToObject(plcs.plc0.global.position,ax2.enc.actpos,0)"

# Positive PLC value means switch input 1; zero/negative means input 0.
ecmcConfigOrDie "Cfg.LinkDataToObject(plcs.plc0.static.limit,ax2.mon.lowlim,1)"

# Configure PLC programs later, after hardware/motion.
ecmcConfigOrDie "Cfg.CreatePLC(0)"
ecmcConfigOrDie "Cfg.AppendPLCExpr(0)=static.limit:=1;global.position:=0;"
ecmcConfigOrDie "Cfg.SetPLCEnable(0,1)"

# Optional explicit final check; entering runtime also performs this validation.
ecmcConfigOrDie "Cfg.ValidateConfig()"

# In configuration mode, unlink restores the prior binding if still alive.
ecmcConfigOrDie "Cfg.UnlinkDataFromObject(ax2.mon.lowlim)"
```

A PLC link command checks destination, path syntax and requested conversion,
then reserves the input slot without replacing its previous binding. The named
PLC and variable may be created later. Final validation parses/compiles PLCs,
checks every pending source and its scalar metadata, then installs the prepared
bindings before axis input validation. It does not create missing variables.
Missing PLCs, failed compilation and absent variables prevent entry into RT.
A missing variable reports `ERROR_PLC_VARIABLE_NOT_FOUND` at validation time.

No pending binding is installed or producer pinned unless all pending sources
pass preflight. A failed validation can therefore be corrected by loading or
editing the PLCs and validating again. Previously resolved links retain their
existing pins and must be unlinked before editing those producers. Pending links
can also be cancelled with
`UnlinkDataFromObject`; the original binding stays untouched. A destination
changed by another command after declaration is reported as a conflict rather
than silently overwritten. EtherCAT and readable-axis sources still resolve
immediately from existing configured objects.

PLC sources use `plcs.plcN.static.<name>` or `plcs.plcN.global.<name>`.
For an axis PLC, use `plcs.axN.static.<name>` or `plcs.axN.global.<name>`.
The prefix identifies the producer for lifetime pinning and health checks;
global variable storage remains shared. A `plcs.plcN.global.*` link does not
make a separate copy or establish exclusive ownership. These are source paths
for the link command, not additional asyn parameter names.

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
Encoder, monitor and drive input slots are supported. I/O descriptors with
`writable` capability also support encoder control/reset and drive output slots,
for both EtherCAT and simulation. Output links require conversion `0`; floating
outputs cannot select a bit. PLC and readable-axis sources remain read-only.
Legacy EtherCAT descriptors retain their existing writable-buffer semantics;
this capability does not change PDO direction or make a physical input writable. Monitoring feature enable flags, switch
polarity, encoder scaling and other consumer configuration remain explicit
existing settings. Linking does not enable motion or enable a monitor feature.

Both application mode command and status must be configuration mode. A
destination cannot be stacked with another data link: unlink it first. A failed
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

All source families accept the same input destinations and configuration-mode
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

The producer's variable is resolved during final pre-RT validation. Global
storage belongs to the PLC manager; static storage belongs to the PLC task.
After resolution, the producer is pinned against reload, expression replacement
and deletion. Pending declarations do not pin a producer. Unlink all its
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
`getDataAccess()` and be bound with `setDataAtIndex`. The command resolves
EtherCAT entries/bits, PLC static/global sources and readable axis sources.
Identical signal
self-links are rejected; different signals on the same axis and reciprocal
cross-axis links are allowed. Array links, generic command destinations and
analysis of PLC program dependencies are not implemented. The memory-backed
simulation driver uses the same resolved I/O descriptors.

Tests cover common scalar access/publication, EtherCAT lookup/bit selection,
filtered/custom health and entry invalidation, on-demand axis names, native
position/bit/computed sources, same-axis and reciprocal links, identical signal
self-link rejection and owner invalidation, deferred PLC resolution/atomic preflight, producer pins and faults, restore/unlink,
runtime-entry validation and real
encoder/monitor inputs.
Logging/asyn and producer endpoints are controlled fixtures, not IOC execution.
See [the expectation inventory](../tests/EXPECTATIONS.md).

There is still one cyclic RT thread. Shared descriptors are prepared before RT;
no new per-signal transport dispatch or value buffer is introduced. Computed
axis sources use a numeric-reader callback; ordinary native scalar sources and
EtherCAT entry-word reads retain direct storage access. Explicit
PLC links add producer-health work and an optional branch in the axis loop.
EtherCAT and axis source links do not enter the PLC producer-health list; when
there are no linked PLC producers, the cyclic health calls are skipped.
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

## Shared resolution and optional capabilities

`ecmcResolveIoEntry(master, slave, name, access)` is the configuration-time
resolver for EtherCAT and memory I/O. The selected-backend overload retains
legacy numeric command behavior. PLC I/O variables, `LinkDataToObject`, and
motion links use this resolver. Position compare, PVT, status output and
sequencer mode commands now accept either configured backend as well; their
historical command names and C++ entry setters remain compatibility adapters.
Resolving an explicit master never changes the selected configuration context.

Native writable scalar descriptors support width-correct integer, floating and
bit writes. Numeric writes reject nonfinite and out-of-range values before
modifying storage. Raw integer writes retain the low bits of the destination
width. Whole-value writes publish through the optional callback; bit writes
retain the existing no-publication behavior. Existing word-storage descriptors
keep their historical conversion and publication semantics. Exported PLC/axis
input views are not made writable by this change.

A descriptor may expose `ecmcSignalTiming`, returning sample/controller times in
one provider clock, timing quality and uncertainty. EtherCAT resolves the device
once during binding and retains its cycle/DC calculation in its own timing
adapter. Axis code reads the capability without consulting a global master or
looking up slaves. Invalid health or owner invalidation prevents timing reads.
Providers without this capability report unavailable timing; asynchronous
simulation values do not borrow the EtherCAT clock or claim a physical sample
time. Binding a simulation entry to position compare therefore does not by
itself provide the timing required to schedule physical output events.
