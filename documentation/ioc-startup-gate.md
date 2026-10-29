# IOC startup gate

`ecmccfg/scripts/setAppMode.cmd` passes `ECMC_STARTUP_GATE` to the configuration
command before starting runtime. Set the variable explicitly to `1` before
that script runs to require the complete startup sequence described here;
defaults differ between ecmccfg versions.

Without that script, the C++ default is enabled. To select legacy startup
directly, use this before `Cfg.SetAppMode(1)`:

```iocsh
ecmcConfigOrDie "Cfg.SetStartupGate(0)"
```

Use `Cfg.SetStartupGate(1)` to enable the gate (the default). The command accepts
only `0` or `1` and is configuration-mode only. Its setting is used on each
runtime entry. C++ no longer reads `ECMC_STARTUP_GATE` directly; `setAppMode.cmd`
translates the variable into the command above.
Disabling the gate also disables the new one-time startup reset and
the post-IOC stabilization wait. The separate timing-discovery wait and the
45-second default bus startup timeout remain in effect.

Normal axis startup and PLC execution wait for `initHookAfterIocRunning`.
`Cfg.SetAppMode(1)` still starts the RT thread and EtherCAT communication before
IOC initialization, and waits for bus readiness and timing discovery/publication.
It does not wait for the IOC hook, so startup scripts can proceed to `iocInit`.
During this configuration-thread wait, the console always shows the startup
counter (`Waiting for EtherCAT bus startup (N/timeout s).`) once per second,
independent of the asyn trace mask. With `MASTER_ID=-1`, the message instead
identifies realtime-thread startup. These progress messages run outside the
cyclic RT thread and do not change the timeout or readiness checks.

After the hook, the RT thread requires two continuous seconds of bus readiness
and completed timing publication. Input reads and disabled-axis output handling
continue while waiting. The dedicated safety plugin continues to execute.
Axes remain in their startup state, with enable commands disabled and position
compare execution held.

Once the bus is stable, a single reset attempt clears latched slave-not-online/
not-operational errors and axis hardware-not-ready/hardware-status-not-OK errors.
Other errors are not automatically cleared. Axis initialization then proceeds
with drives disabled and motion/enable requests held. Native axes must leave
startup, and every **loaded motor record** linked to ECMC must finish its startup
handshake: soft-limit initialization, first healthy status delivery, any requested
position load, record processing, and a subsequent RT sample acknowledging that
load. An axis with no loaded motor record has no motor handshake to wait for;
creating an asyn motor axis object alone does not add this wait.

After common IOC/bus stabilization, each axis releases its own direct enable
and motion commands, axis PLC and position compare when its native initialization
and motor-record handshake complete. Configured enable-at-startup is deferred
until that axis releases. An unready physical motor does not hold another
physical motor, including motors in the same mirror/slit slave group.

Virtual/master axes belonging to a configured master/slave state machine also
wait for the native/motor handshakes of that machine's master and slave groups.
Each machine starts when its own groups are ready; unrelated axes do not hold it.
Physical/slave members retain independent direct startup release. Existing
runtime ownership/command-blocking rules still apply after a coordinated group
takes control.

General PLCs, motion sequences, PVT, ordinary plugin cyclic callbacks and C++
logic retain the aggregate startup gate, because their axis dependencies are
not declared here. They wait for all configured axes. The per-axis and shared
program holds have separate diagnostic PVs. This change does not infer arbitrary
PLC, plugin or sequence dependencies.
Execute/enable requests during the hold return `ERROR_AXIS_BUSY` without latching
a new axis error or queuing the request for later. STOP, disable, position loading
and status polling remain available. Plugin/logic enter-RT initialization hooks
still run during runtime setup; their supported axis motion/enable requests are
subject to the same hold. The dedicated safety plugin continues to run.

See [Motor-record startup restoration](motor-record-startup-restore.md) for the
ESS motor communication-alarm compatibility workaround and its completion checks.

This is a startup gate, not a runtime fault-recovery mechanism. After release,
normal fault handling remains active; the gate does not repeatedly reset errors.

`AxisTouchProbeArm` and `AxisPositionCompareArm` also accept requests in
configuration mode, after their encoder/axis and EtherCAT links are configured.
They validate configuration and store the request without command-time PDO
access. `OK` means accepted, not that the hardware is already armed. Touch-probe
control is applied by the cyclic encoder writer when its domain is valid;
position compare additionally waits for the startup gate and valid timing.
PC cancellation also defers its idle write to the cyclic thread. This does not
mask domain faults or suppress motor-record STOP requests from interlocks.
If the bus fails to stabilize within the configured startup timeout after IOC
running (45 seconds by default), execution remains held and an error is logged.
Resolve the fault and restart runtime to retry. Axis hardware errors can still
prevent that axis and dependent virtual masters from completing initialization.

For applications that never call `iocInit`/`iocRun`, set this before entering
runtime:

```iocsh
epicsEnvSet("ECMC_WAIT_FOR_IOC_RUNNING", "0")
```

This skips only the IOC-hook requirement, not bus stabilization or axis startup.
If motor records are loaded, they still require IOC initialization and their
startup handshake; this option is intended for applications without motor records.
An axis PLC waits for its own axis. General startup PLCs/plugins remain behind
the shared gate. Code required to establish readiness must initialize its signals
before that gate, for example by configuration or external simulation injection.

For a hardware test, check the logs in order: bus/timing startup complete, IOC
running, bus stable/axis initialization released, motor-record position restoration
complete, then that axis's direct commands and PLC released. Confirm that one
unready physical axis does not prevent another physical axis from enabling, that
a virtual master waits for its participating axes, and that a separate ready
master/slave group runs while unrelated startup remains pending. Also test a native
axis with no loaded motor record: it must not wait for a nonexistent motor record.
Verify outputs stay disabled before release and that a later bus fault is still
reported without automatic clearing. The behavior has not yet been hardware tested.
