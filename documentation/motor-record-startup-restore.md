# Motor-record startup restoration

Use the IOC startup gate when motor positions or limits must be restored before
motion producers can execute:

```iocsh
ecmcConfigOrDie "Cfg.SetStartupGate(1)"
```

With ecmccfg, set `ECMC_STARTUP_GATE=1` before `setAppMode.cmd` runs. Gate `0`
explicitly selects legacy startup and does not provide the ordering guarantee
below. EtherCAT communication and native axis initialization must run while the
motor handshake is pending; holding those would prevent the handshake completing.

## Save and restore configuration

For motor-record soft-limit restoration, enable `restoreMotorSoftLims=1` and
`syncSoftLims=1` in the motor axis options. In an ecmccfg axis YAML file:

```yaml
axis:
  parameters: "restoreMotorSoftLims=1;"
epics:
  motorRecord:
    syncSoftLimits: true
```

Save the motor's dial limits. Position saving is a separate motor-record feature:

```text
$(P)$(M).DVAL
$(P)$(M).DLLM
$(P)$(M).DHLM
```

Configure autosave restore passes 0 and 1 before `iocInit`. Avoid competing
restores through the `-CfgDLLM`/`-CfgDHLM` output records. Independent soft-limit
enable flags require their own ordering and are not encoded by the two dial
limit fields.

The restored pair is validated and transferred under one RT lock. `(0, 0)`
disables both limits. A nonzero valid pair restored from `(0, 0)` enables both;
otherwise the existing per-direction enable flags are preserved. Invalid
input changes neither endpoint nor enable state.

The motor-controller `asynReport` axis report includes `Startup soft limits`:
waiting for IOC running, waiting for the snapshot, invalid snapshot, waiting for
sync enable, waiting for transfer, or complete. The snapshot is taken once by
the IOC initialization hook. An invalid snapshot is not repaired by editing
live motor fields: correct the saved DLLM/DHLM/MRES and restart the IOC. Merely
restarting ECMC runtime does not rerun that hook. A transient transfer failure
is retried against the original valid snapshot.

ECMC snapshots the dial limits after autosave/record initialization and before
IOC running. Limits stay in engineering units; MRES magnitude must not scale
them. Negative MRES reverses direction and swaps the raw EGU endpoints. An EGU
flag default such as `field(MFLG, "$(MFLG=8)")` in an ECMC-specific motor template
establishes units from database loading. The compatibility init hook also sets
the EGU bit before motor record initialization. Do not change MRES to disguise
a unit conversion error.

The motor's `RSTM` selects whether a saved position is loaded. Its default
`NearZero` restores a nonzero saved position when the initial readback is near
zero, using the motor's deadband. It does not request restoration of a saved zero.
ECMC leaves this decision with the motor record; it does not force position zero
or unconditionally load the saved position.

## ESS motor communication-alarm compatibility workaround

During IOC initialization, the motor may query position before the first valid
RT sample. Device support then defers position restoration and the record raises
`COMM_ALARM`. On the first healthy callback it decides to restore the saved
position, but `devMotorAsyn::build_trans()` rejects the load while the previous
`STAT` or `NSTA` is still `COMM_ALARM`. Motor processing can subsequently regard
that rejected load as complete and replace DVAL with the initial zero readback.
This path exists in ESS 7.2.8.1 and was also present in ESS `ess-master` commit
[`0156312e`](https://github.com/EuropeanSpallationSource/motor/commit/0156312e13802ddef8cc1ca9711d20c542555d43).

ECMC provides a **one-time compatibility workaround**, rather than changing
motorECMC. Before dispatching the first healthy motor status, it locks the linked
motor record and uses `recGblResetAlarms()` to discard the stale startup COMM
state. Pending or existing non-COMM alarms are preserved. It requires IOC running,
a valid finite position, successful asyn parameter status, no incoming motor
communication/problem bit, and a live native axis which has started, left startup,
is not communication-blocked and has no axis error. Live state is checked again
after acquiring the record lock. Active record processing is not interrupted.

Status and commands still use asyn. However, this is deliberately an exception to
normal record-alarm ownership: ECMC directly recovers the linked motor's startup
alarm under its record lock before the synchronous device-support callback. It
must remain narrow, documented and tested. The workaround never repeatedly clears
runtime communication faults or general axis faults. It does not access motor's
private save/restore structures.

## Completion and motion release

Clearing COMM or queuing a position load is **not** startup completion. For each
loaded motor record ECMC waits for successful soft-limit initialization, healthy
status delivery, `MIP_DONE`, DMOV true, no motion, no remaining COMM state, and a
post-command RT sample if position loading was requested. Position loading marks
the driver's sample acknowledgement pending again. The native axis exposes an
atomic readiness flag so the RT thread can check completion without taking motor
record or motor-controller locks.

The RT thread keeps all ordinary cyclic motion producers held until every native
axis is initialized and every loaded motor record has completed this handshake.
Native axes without a loaded motor record are ready by default. A failed limit
restore, pending position load, missing valid status, or unfinished record
processing cannot release the gate. Drives remain disabled during the hold,
including axes using an external trajectory source. Configured startup enable is
applied only after release. Early execute/enable requests are rejected with busy;
they are not replayed after startup. STOP/disable and coordinate restoration remain
available.

While the motor/axis handshake remains pending, the RT logger reports the hold
and blocking active-axis slots at the configured EtherCAT startup timeout
interval (30 seconds if no positive timeout is configured). This diagnostic
does not release motion or latch a new axis error. Consult the motor-controller
report for the soft-limit reason; if restoration is complete, inspect motor
record processing, position-load acknowledgement and bus/axis readiness.

This holds axis/general PLC execution, motion sequences, PVT, master/slave state
machines, ordinary plugin cyclic execution, C++ logic and position compare.
Configuration and plugin/logic enter-RT initialization hooks still run to prepare
runtime; supported axis motion/enable commands from those hooks are held too.
The dedicated safety plugin and bus/disabled-axis I/O continue operating.

## Verification

### Diagnostic PVs

The records are included in `ecmccfg/db/core/ecmcMotorRecord.template` and
load automatically with each motor record. No separate diagnostic template is
installed by ecmc. They use the existing `PREFIX`, `MOTOR_NAME`, `MOTOR_PORT`
and `AXIS_NO` macros and require a driver providing the startup parameters.

| Suffix appended to the motor PV name | Meaning |
| --- | --- |
| `-StartupHeld` | This axis's startup command hold; 1 means its enable/motion requests are rejected |
| `-StartupHoldReason` | Reason for this axis's hold, or `released`; 256-byte CHAR waveform |
| `-StartupBlockingAxis` | First blocking axis ID; own ID for local initialization/restore; -1 for none/common I/O |
| `-StartupBlockingGroup` | Blocking master/slave state-machine index, or -1 |
| `-StartupSharedHeld` | Shared-program startup hold: general PLCs, sequences, PVT, plugins and C++ logic |
| `-StartupSharedBlockingAxis` | First axis holding shared startup, or -1 for none/common I/O |
| `-StartupMotorReady` | This axis's motor-record startup handshake is complete |
| `-StartupLimitsReady` | This axis's initial soft-limit synchronization is complete |
| `-StartupRestoreStatus` | Full soft-limit restore reason as a 256-byte CHAR waveform |

These are independent: soft limits can be complete while position restoration
still holds the motor handshake. A physical axis releases independently of
other axes; a configured virtual master additionally waits for its own group's
members. Shared programs retain an all-axis wait, shown separately. A ready axis
can therefore show `StartupHeld=0` with `StartupSharedHeld=1`. Gate opt-out can
release the hold while restoration is incomplete.

The troubleshooting panel labels the gate as **Axis hold** and shows the reason,
blocking axis/group, shared-program hold and shared blocking axis. The existing
`STARTUP_COMMANDS_HELD` motor-port parameter now reports the addressed axis's
hold. Shared-program state is exposed by `STARTUP_SHARED_COMMANDS_HELD` and
`STARTUP_SHARED_BLOCKING_AXIS`. Other new parameters are `STARTUP_HOLD_REASON`,
`STARTUP_BLOCKING_AXIS` and `STARTUP_BLOCKING_GROUP`; existing PV names are retained.
The parameters are published on every motor poll, including early-return paths;
changes within a poll are reflected on the next poll. They do not require the
RT logger and do not control startup. Before network access becomes available,
use console diagnostics and the motor-controller `asynReport` instead.

### Tests

The standalone regression checks are:

```sh
python3 tests/test_motor_softlimit_restore.py
python3 tests/test_motor_startup_comm_recovery.py
python3 tests/test_motor_startup_execution_gate.py
```

For an IOC test, use a known nonzero saved DVAL and dial limits different from
YAML defaults. Confirm restoration before the log announcing release of PLCs and
motion command producers. Attempt an early enable/move and verify it is rejected
and not replayed. Repeat with no loaded motor record, and with a stalled restore
or bus fault. Confirm the dedicated safety plugin continues operating and a later
runtime communication fault stays reported. The combined gate extension still
requires hardware verification; standalone tests model the relevant startup paths.
