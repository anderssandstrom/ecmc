# EL7041-0052 and EL5042 DC timing test

## Test configuration

The normal `EL7041-0052` hardware description does not enable distributed
clocks. Its Beckhoff ESI entry nevertheless provides a DC mode using
`AssignActivate=0x300`, an application-period SYNC0, and zero SYNC0 shift. For
this test, apply that DC configuration immediately after adding the EL7041.

Use `EL5042_DC`, rather than the non-DC `EL5042` description:

```text
##############################################################################
## DC timing test for EL7041-0052 and EL5042

require ecmccfg v11.0.9_RC1 "ENG_MODE=1,MASTER_ID=0,ECMC_VER=v11.0.9_RC1"

# EL7041-0052 stepper drive
${SCRIPTEXEC} ${ecmccfg_DIR}addSlave.cmd, "SLAVE_ID=14,HW_DESC=EL7041-0052"

# EL7041-0052 ESI DC mode: application-period SYNC0, zero shift.
ecmcEpicsEnvSetCalc("ECMC_TEMP_PERIOD_NANO_SECS",1000/${ECMC_EC_SAMPLE_RATE=1000}*1E6)
${SCRIPTEXEC} ${ecmccfg_DIR}applySlaveDCconfig.cmd, "ASSIGN_ACTIVATE=0x300,SYNC_0_CYCLE=${ECMC_TEMP_PERIOD_NANO_SECS},SYNC_0_SHIFT=0,SYNC_1_CYCLE=${ECMC_TEMP_PERIOD_NANO_SECS}"

${SCRIPTEXEC} ${ecmccfg_DIR}applyComponent.cmd, "COMP=Motor-Generic-2Phase-Stepper,MACROS='I_MAX_MA=1500,I_STDBY_MA=1000,U_NOM_MV=48000,R_COIL_MOHM=1230'"
epicsEnvSet(DRV_SID,${ECMC_EC_SLAVE_NUM})

# EL5042 two-channel BiSS-C encoder, with its standard ESI DC mode.
${SCRIPTEXEC} ${ecmccfg_DIR}addSlave.cmd, "SLAVE_ID=9,HW_DESC=EL5042_DC"
${SCRIPTEXEC} ${ecmccfg_DIR}applyComponent.cmd, "COMP=Encoder-RLS-LA11-26bit-BISS-C,CH_ID=1"
${SCRIPTEXEC} ${ecmccfg_DIR}applyComponent.cmd, "COMP=Encoder-RLS-LA11-26bit-BISS-C,CH_ID=2"
epicsEnvSet(ENC_SID,${ECMC_EC_SLAVE_NUM})

libversionShow

${SCRIPTEXEC} ${ecmccfg_DIR}loadYamlAxis.cmd, "FILE=./cfg/axis.yaml,DEV=${IOC},AX_NAME=M1,AXIS_ID=1,DRV_SID=${DRV_SID},ENC_SID=${ENC_SID},ENC_CH=01"
${SCRIPTEXEC} ${ecmccfg_DIR}loadYamlEnc.cmd, "FILE=./cfg/enc_open_loop.yaml,DEV=${IOC},ENC_SID=${DRV_SID}"
```

The `SYNC_1_CYCLE` argument is retained to match the ESI/ecmccfg convention for
this `0x300` mode. It must not be interpreted as proof that the terminal uses a
separate SYNC1 application event.

## Collecting results

Start runtime and wait until both slaves are operational. Timing SDOs are read
serially to avoid mailbox contention. Run the report after a few seconds and
repeat it if either timing object says `(pending)`:

```text
EcPrintSlaveConfig(14)
EcPrintSlaveConfig(9)
```

Also capture the ecmc cycle time, terminal revision numbers, domain execution
rate/offset, and complete working-counter state. If `EcPrintSlaveConfig` expects
the master's zero-based enumeration rather than the configured bus position in
the deployed setup, use the corresponding enumeration indices; the report
prints the resolved physical position for confirmation.

## ESI expectations

- EL7041-0052 (`0x1b813052`, ESI revision `0x00100034`) supports DC mode with
  `AssignActivate=0x300`, application-period SYNC0, and zero shift.
- EL5042 (`0x13b23052`) supports the same high-level DC schedule. Its input
  timing object is expected to provide the encoder acquisition information.

## First hardware readback

At a 1,000,000 ns ecmc/DC cycle with zero configured shift, the installed
terminals reported:

| Terminal/direction | Sync type | Cycle | Shift | Min cycle | Calc/copy | Sync error |
|---|---:|---:|---:|---:|---:|---:|
| EL5042 input | 2 | 1,000,000 ns | 0 ns | 100,000 ns | 100,000 ns | 0 |
| EL7041 output | 2 | 1,000,000 ns | 0 ns | 250,000 ns | 0 ns | 0 |
| EL7041 input | 2 | 1,000,000 ns | 0 ns | 250,000 ns | 250,000 ns | 0 |

All three directions reported supported-mode mask `0x0807`, zero minimum and
maximum delay fields, and command zero. Sync type 2 establishes that the
effective PDO synchronization is tied to SYNC0. The calculation/copy values are
device processing information; they are not by themselves physical encoder or
drive propagation delays.

The remaining unknown is the PDO-to-SYNC cycle association: which SYNC0 acquired
the EL5042 value received by ecmc, and which later SYNC0 applies the EL7041
setpoint sent by that ecmc cycle. This must be established before deriving the
control compensation interval.

### Fast-ramp correlation

A 200-cycle trace with a faster velocity ramp produced a clear relationship
between the EL7041 velocity setpoint and its returned open-loop counter delta:

```text
openLoopDelta[N] ~= velocitySetpoint[N - 5] / 256
```

Least-squares alignment gave a scale of `0.0039052`, close to the expected
`1/256 = 0.00390625`. Lag five had mean-square error `1.05`, compared with
`6.60` at lag four and `17.08` at lag six. The observed command-to-returned-
counter pipeline is therefore five 1 ms cycles for this configuration.

This five-cycle result includes RxPDO transport/application, internal counter
update, TxPDO acquisition/copy, return transport, and ecmc receive ordering. It
must not yet be labeled as the EL7041 drive-application delay alone.

The same trace showed the EL7041 TxPDO toggle changing every ecmc cycle, while
the EL5042 channel-1 TxPDO toggle changed every two ecmc cycles. The latter is a
channel-specific update-rate observation that is not visible in the slave-wide
1 ms `0x1C33:02` value and should be repeated for channel 2.

### Set-counter handshake test

The next test removes motor mechanics and velocity quantization from the
measurement. Run `documentation/plc/el7041-set-counter-timing.plc` while the
motor is stationary and the axis is disabled. Before loading the PLC, create
eight 128-sample data storages:

```text
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=10,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='cycle'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=11,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='state'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=12,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='set counter request'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=13,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='set counter done'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=14,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='set counter value'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=15,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='returned counter'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=16,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='EL7041 TxPDO toggle'"
${SCRIPTEXEC} ${ecmccfg_DIR}addDataStorage.cmd "DS_ID=17,DS_SIZE=128,SAMPLE_RATE_MS=-1,DS_TYPE=0,DESC='returned minus target'"
```

Copy the complete PLC into the IOC application's `cfg` directory, then load it
at the default EtherCAT-cycle rate:

```text
${SCRIPTEXEC} ${ecmccfg_DIR}loadPLCFile.cmd "PLC_ID=0,FILE=./cfg/el7041-set-counter-timing.plc,PLC_MACROS='DRV_SID=14,TARGET=12345,PRE_CYCLES=10,POST_CYCLES=20',DESC='EL7041 set-counter timing'"
```

Do not reuse already-created data-storage IDs; replace the old test block or use
another contiguous range and change the IDs in the PLC.

The recorded arrays are:

| Storage | Signal |
|---:|---|
| 10 | PLC execution sequence |
| 11 | handshake state |
| 12 | outgoing set-counter request, control bit 2 |
| 13 | returned set-counter-done acknowledgement, status bit 2 |
| 14 | outgoing requested counter value |
| 15 | returned EL7041 counter value |
| 16 | EL7041 TxPDO toggle, status bit 15 |
| 17 | returned counter minus requested value |

The request-to-acknowledgement and request-to-new-position cycle differences
bound the complete RxPDO-to-TxPDO terminal path. They still include the return
TxPDO and ecmc receive ordering and therefore are not, alone, the physical drive
application delay.

#### First set-counter result

At a 1 ms EtherCAT cycle the recorded handshake was:

| Trace cycle | Request | Done | Returned counter | State |
|---:|---:|---:|---:|---:|
| 8 | 0 | 0 | 4486 | 0 |
| 9 | 1 | 0 | 4486 | 1 |
| 10 | 1 | 0 | 4486 | 1 |
| 11 | 1 | 0 | 4486 | 1 |
| 12 | 0 | 1 | 12345 | 2 |
| 13 | 0 | 1 | 12345 | 2 |
| 14 | 0 | 1 | 12345 | 2 |
| 15 | 0 | 0 | 12345 | 3 |

Both the set-counter acknowledgement and the new returned counter value became
visible three PLC/EtherCAT cycles after the PLC asserted the outgoing request.
The acknowledgement became low three cycles after the PLC cleared the request.
The EL7041 TxPDO toggle alternated on every recorded cycle throughout the test.

Consequently, this configuration has a repeatable three-cycle PLC-visible
RxPDO-to-TxPDO handshake path. This is a stronger bound than the five-cycle
velocity-to-counter-delta result: the additional two cycles in the velocity
test belong to the velocity-generator/counter-update behaviour rather than the
basic set-counter handshake. Neither result alone locates the output application
instant within a DC cycle; that requires the ecmc send/receive ordering and SYNC0
phase to be included in the timing model.

## Generic timing overrides

ecmc resolves the endpoint SYNC source and shift automatically from the retained
DC configuration, the ESC schedule (`0x0990`/`0x09A0`), and the asynchronously
read `0x1C32`/`0x1C33` values. The ESC start time is preferred because it is the
actual programmed SYNC phase and already includes the configured shift. A DC
path remains unresolved if the actual ESC schedule cannot be read; ecmc does
not invent an absolute phase from the configured shift.

```text
TIME ─────────────────────────────────────────────────────────────────────────►

 EL5042     ● sample             C[k-1]                            C[k]
             S[k-1]                │<──────── T = 1 ms ─────────────>│
             │<──── 250 us ───────>│                                │
             └─ 100 us copy ─●     │                                │
                             ready └──────── PDO ────────► Rx ───────●
                                                                    │
 ecmc                                                        control ─► Tx
                                                                    │
 EL7041                                                             │
                                                                    └─ 500 us ─●
                                                                                output
                                                                                applied

 S[k-1] = C[k-1] - 250 us
 output  = C[k] + 500 us

 DELAY = 250 us + 1 ms + 500 us = 1.750 ms

 Generation selection at the EL5042:

 sample ── 100 us copy ── ready ──────────────►
                            │
 PDO frame after ready  ────┘ current sample  (cycleOffset = -1)
 PDO frame before ready ───── previous sample (cycleOffset = -2)

 The 100 us selects the PDO generation. It is not added again to DELAY.
```

For every endpoint, `cycleOffset` is relative to the most recent selected
endpoint event at or before the exact application time:

```text
input  cycleOffset < 0 : age of the PDO value
output cycleOffset > 0 : lead to the application event
eventOffsetNs           : terminal/electronics correction from SYNC
updateDivisor           : cycles between fresh values
uncertaintyNs           : bound carried into the combined result
```

For the shifted test above, EL5042 input uses `cycleOffset=-1` and the next
EL7041 application event uses `cycleOffset=+1`. At zero EL5042 shift, the frame
arrived before its 100 us copy completed and the input age was `-2` instead.
Reported calculation/copy time is retained separately because its relation to
the physical event is terminal-specific and it is not generically additive.

```text
Cfg.EcSetSlaveTimingOverride(9,2,-1,0,100000)
Cfg.EcSetSlaveTimingOverride(14,1,1,0,0)
```

If a hardware description has authoritative information that is absent from
those objects, it can supply a correction during configuration:

```text
Cfg.EcSetSlaveTimingOverride(slavePosition,direction,cycleOffset,eventOffsetNs,uncertaintyNs)
```

Direction uses the EtherCAT convention `1=output`, `2=input`. `eventOffsetNs`
is a signed correction relative to the endpoint's reported SYNC event. The
override should only be present in a hardware snippet when its values are known;
there is intentionally no guessed default. For example:

```text
ecmcConfigOrDie "Cfg.EcSetSlaveTimingOverride(${ECMC_EC_SLAVE_NUM},1,${ECMC_EC_TIMING_OUT_CYCLES},${ECMC_EC_TIMING_OUT_OFFSET_NS},${ECMC_EC_TIMING_OUT_UNCERTAINTY_NS})"
```

An endpoint delay is considered calculable only when both endpoints have a
resolved DC SYNC source, event offset, and known integer-cycle association.

If an endpoint produces a fresh value only every N EtherCAT cycles, declare it
without adding cyclic RT work:

```text
Cfg.EcSetSlaveTimingUpdateDivisor(slavePosition,direction,N)
```

The divisor is endpoint metadata. A future timed-value producer must advance
its event sequence only when the terminal's update toggle/counter confirms a
fresh value.

For a non-DC slave the same endpoint structure is used with `cycle-only`
quality. Its nominal event is related to the ecmc receive/send cycle and its
default uncertainty is one reported cycle. A hardware override may reduce that
uncertainty when the terminal documentation defines the relationship.

A timestamp PDO can be linked without changing the consumer of the timing:

```text
Cfg.EcLinkSlaveTimingTimestamp(slavePosition,direction,entryId,bits,correctionNs)
```

`bits` is 32 or 64. A 32-bit timestamp is extended around a nearby 64-bit DC
time and is only valid within half of its wrap interval. When linked, hardware
timestamp quality takes precedence over schedule-derived timing. The signed
correction is reserved for a documented input/output electronics delay.

If a terminal does not allow its active synchronization type to be read in
OP, the hardware description can declare the ESI-defined source explicitly:

```text
Cfg.EcSetSlaveTimingSource(slavePosition,direction,source)
```

Here `direction` is `1` for output and `2` for input; `source` is `0` for
cycle-only, `1` for SYNC0, and `2` for SYNC1. `EL5042_DC` applies
`Cfg.EcSetSlaveTimingSource(slavePosition,2,1)` by default because some EL5042
revisions reject the asynchronous `0x1C33:01` read in OP.

Resolved static timing between an input slave and output slave can be inspected
with:

```text
EcPrintControlTiming(inputSlavePosition,outputSlavePosition)
```
The combined result can be inspected with:

```text
EcPrintControlTiming(9,14)
```

where the first argument is the encoder/input slave and the second is the
drive/output slave. Until any required hardware overrides are configured, the
command reports the result as unresolved and `EcPrintSlaveConfig()` identifies
which endpoint fields are still unknown.

## Capability use

```text
Hardware latch + timestamp  -> exact position and DC event time
Hardware latch only         -> exact latched position; event time is bounded
DC sampled PDO              -> nominal event time from SYNC + generation age
Non-DC PDO                   -> cycle-bounded time
EL2252-style timed output    -> queue an absolute future DC switching time
Normal PDO output            -> one fixed application phase per cycle
```

Touchprobe and position-compare objects should exchange
`ecmcEcTimedValue<T>` values. The value carries its DC event time, sequence,
quality and uncertainty, so EL5102/EL7062 latch sources, timestamp inputs,
ordinary encoders and timed outputs use the same consumer interface.

```text
position + event time ── crossing prediction ── future DC time ── timed output
hardware latch         (must meet output lead)                  (for example EL2252)
```

Timing-object and ESC discovery runs only during startup. Static delay
calculation runs on request, not in the cyclic control path. Future cyclic
users retain an already-resolved schedule and update only the timed value's
sequence and event time when a fresh PDO/toggle arrives.
- The EL7041 output timing object and EL5042 input timing object must be read
  after activation before deriving the encoder-acquisition-to-drive-application
  interval.

## DC schedule discovery

For every DC-configured slave, ecmc also reads the ESC cyclic-unit registers
once after the slave reaches OP: start time `0x0990`, SYNC0 cycle time
`0x09A0`, and SYNC1 cycle time `0x09A4`. `EcPrintSlaveConfig(slave)` prints
these raw values as `DC ESC schedule`. The register requests are created before
master activation, execute asynchronously at startup, and stop after the three
reads complete.

Select a DC reference clock during configuration, before the master is
activated. For this test, use the EL5042:

```text
Cfg.EcSelectReferenceDC(0,9)
```

Do not move the EL7041 DC addition into its general ecmccfg hardware description
until the terminal has been exercised in both DC and legacy SM-synchronous
configurations.
