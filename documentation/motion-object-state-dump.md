# Motion object state dump

`ecmcDumpMotionState` writes a consistent snapshot of the configured motion
objects to one flat YAML file:

```text
ecmcDumpMotionState("/tmp/ecmc-motion-state.yaml")
```

The same dump can be requested through the RT logger PVs:

```text
caput $(IOC):MCU-RTLog-DiagLevel 2
caput $(IOC):MCU-RTLog-DiagDump 1
caget $(IOC):MCU-RTLog-DiagBusy
caget $(IOC):MCU-RTLog-DiagStatus
caget -S $(IOC):MCU-RTLog-DiagFile
```

The PV-triggered filename is generated automatically under `/tmp` as
`ecmc_motion_diag_<pid>_<timestamp>.yaml`. Status is `1` while queued,
`0` after a successful write, `-1` after failure, and `2` when a second
request is rejected because a dump is already active.

The dump contains all configured axes, their axis data, encoders, monitor,
trajectory, sequencer, controller and drive state.  It also contains all axis
groups and all master/slave state machines, including private runtime members,
old-value latches, counters and referenced-object addresses.  The motor-record
controller and every configured motor-record axis are included as well, so
`moveReady`, cached axis status, command-cycle counters and callback-related
state can be compared with the underlying motion axis.

Keys use the C++ member names so a dump can be compared directly with the
source code, for example:

```yaml
axes[1].data_.status_.cycleCounter: 123456
axes[1].encoders[0].scaleNum_: 10
masterSlaveSMs[0].state_: 2
```

The command takes the snapshot while holding `ecmcRTMutex`.  YAML formatting
is done into memory under the lock to keep related values consistent.  The
mutex is released before the file is opened and written, so filesystem I/O is
never performed while the realtime state is locked.

This command is intended for fault diagnostics.  The snapshot can be large,
especially when filters contain long buffers, and should not be called
cyclically.
