# IOC startup gate

`ecmccfg/scripts/setAppMode.cmd` passes `ECMC_STARTUP_GATE` to the configuration
command before starting runtime, defaulting to `0` (legacy startup). Set the
variable to `1` before that script runs to enable the gate. `startup.cmd` only
initializes the variable's default.

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

After the hook, the RT thread requires two continuous seconds of bus readiness
and completed timing publication. Input reads and disabled-axis output handling
continue while waiting. The dedicated safety plugin continues to execute.
Axes remain in their startup state, with enable commands disabled and position
compare execution held.

Once the bus is stable, a single reset attempt clears latched slave-not-online/
not-operational errors and axis hardware-not-ready/hardware-status-not-OK errors.
Other errors are not automatically cleared. Axis initialization then proceeds,
including any configured enable-at-startup behavior. Once all axes have left
startup, axis/general PLCs, motion sequences, PVT, master/slave state machines,
ordinary plugins and C++ logic begin executing.

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
prevent the subsequent axis-initialization phase from completing.

For applications that never call `iocInit`/`iocRun`, set this before entering
runtime:

```iocsh
epicsEnvSet("ECMC_WAIT_FOR_IOC_RUNNING", "0")
```

This skips only the IOC-hook requirement, not bus stabilization or axis startup.
Startup PLCs or plugins that are themselves required to make an axis ready will
need their initialization moved to configuration; otherwise they remain held.

For a hardware test, check the logs in order: bus/timing startup complete, IOC
running, bus stable/axis initialization released, then PLC execution released.
Verify outputs stay disabled before release and that a later bus fault is still
reported without automatic clearing. The behavior has not yet been hardware tested.
