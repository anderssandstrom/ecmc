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

This holds axis/general PLC execution, motion sequences, PVT, master/slave state
machines, ordinary plugin cyclic execution, C++ logic and position compare.
Configuration and plugin/logic enter-RT initialization hooks still run to prepare
runtime; supported axis motion/enable commands from those hooks are held too.
The dedicated safety plugin and bus/disabled-axis I/O continue operating.

## Verification

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
