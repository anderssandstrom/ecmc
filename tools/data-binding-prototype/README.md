# Common data binding prototype

This is an experimental read-only scalar binding over the existing
`ecmcDataItem` metadata. It is not installed, registered with IOC shell, or used
by the cyclic RT path. No production class layout, plugin ABI or existing
`ecmcDataItem::read/write` behavior changes.

Run the pilot from the ecmc repository root:

```sh
python3 tests/test_data_binding.py
```

The test compiles the real `ecmcDataItem.cpp`, this binding and extracted,
unmodified `ecmcPLCDataIF::getDataRef/setData` and
`ecmcAxisBase::setExtSetPos/getExtSetPos` methods. Their enclosing objects have
only the fields needed by these methods. It demonstrates transfer from a PLC
double reference to the existing axis external-setpoint setter, not a running
ExprTk PLC, full axis, simulator, IOC command or motion-safety validation.

## Initial contract

- Bind one native scalar before cyclic use. Validate pointer, pointer-valid
  metadata, exact datatype/size, element size, bit width and declared direction.
  Zero element size/bit width means unspecified in the existing metadata;
  explicit incompatible values are rejected. Arrays, packed bits, converted
  integer encodings and implicit conversions are outside this first prototype.
- Preserve integer values without a `double` intermediate. Read using a fixed
  size `memcpy`, which also supports unaligned byte storage.
- Borrow storage and an optional source-valid flag. Keep both alive and at
  stable addresses; freeze item pointer/type/size metadata until unbound.
  Binding does not impose that lifetime on the source or detect its destruction.
- Return failure without changing the destination when unbound or unhealthy.
  A failed bind discards the old binding. The optional health flag is distinct
  from pointer-valid metadata; no flag means health is unobserved. Omitting it
  is not appropriate when communication failure detection is required.
- Sample only at the consumer's execution point. Writes made before that point
  are visible; writes afterward become visible at a subsequent sample. This
  does not change existing scheduler order or resolve multiple-writer conflicts.
- Source update and sampling must run on the same cyclic thread or use existing
  external synchronization. No threads, locks, allocations, name lookups,
  callbacks or driver dispatch are introduced by reading the binding.
- Reads do not enforce freshness, reject nonfinite floats, stop motion or latch
  errors. Those require explicit consumer policy before production integration.

`getAllowWriteToEcmc` describes writes into a registered item, not whether it can
be observed. Both existing read and write directions allow observation here.
There is deliberately no generic binding write: commands must use their existing
handlers, and raw value writes need defined ownership/publication semantics.

## Existing data-item fit

`ecmcDataItemInfo` already supplies name, storage pointer, byte/element sizes,
bit width and native datatype. `ecmcAsynDataItem` derives from it, so those
descriptors are already used for publication. Reuse these descriptors rather
than allocate a second signal value. The current datatype enum is named
`ecmcEcDataType`; its naming/dependency cleanup is a separate compatibility task.

`ecmcDataItem::read/write` currently perform unchecked byte copies. Direction,
range and pointer-valid metadata are not enforced by those functions. Their
existing behavior remains untouched. Data-item callbacks and generic writes
also do not establish command semantics or communication validity.

| Connection | Existing metadata can supply | Still needed before full integration |
| --- | --- | --- |
| EtherCAT entry to encoder | Native entry buffer metadata published through its asyn data item | Preserve entry conversion/bit access and domain-health/error behavior; expose binding without depending on asyn lookup in RT. |
| Simulation signal to encoder | A data item over simulator-owned native storage | Explicit validity/timing, production encoder binding and model phase ordering. |
| PLC variable to axis external setpoint | F64 data item over `getDataRef()` | Register/resolve the source at configuration time; permission, source lifetime/reload policy, startup gating and sampling at the existing producer/consumer position. |

Only the third connection is exercised here, using explicit sampling and the
existing setter. Leaving a destination unchanged on read failure is a transfer
result, not an implementation of motion fault handling. The requested motion
policy is to retain the sample and stop on declared source loss. EtherCAT's
existing filtered domain-health decision and axis interlock/disable response
must be preserved; the binding must not trip on each lost frame. PLC producer
health and reset/restart rules still require a concrete source adapter. No
enable/execute command is sent by this pilot.

## Access cost and next integration gate

Compared with a bare `ecmcDataItem::read`, this accessor performs a binding-null
check and an optional source-valid check in addition to the native-sized copy.
These checks are not claimed to be free. The compiler can inline the copy;
inspect optimized code before assuming its generated instruction sequence.
There is no new per-signal virtual/function-pointer dispatch.

Before selecting a production consumer, define who owns/rebinds the source,
which checks replace existing checks rather than duplicate them, publication
effects, write conflicts and failure policy. Then measure the integrated path
against the same production baseline, including worst-case cycle behavior.
This standalone pilot cannot establish unchanged IOC/RT performance.
