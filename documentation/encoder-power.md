# Encoder relay power control

Encoder relay power control"
weight = 45
+++

Encoder relay power control reduces the time an absolute encoder, such as a
BiSS encoder in vacuum, is powered. It is disabled by default and configured
separately for each encoder. Existing axes keep their previous behavior.

## Power-off and wake-up

After a move completes, the encoder stays powered until the axis has remained
atTarget for the configured power-off delay. When a positive controller deadband
is smaller than the atTarget tolerance, the axis must also be within that
deadband. Losing either condition resets the delay. atTarget monitoring must be
enabled with a positive tolerance; otherwise the configuration is rejected.

The encoder can power off while the amplifier remains enabled and holds torque.
ECMC suspends its feedback correction while feedback is unavailable: CSV velocity
output is zero and CSP holds its position setpoint without ECMC outer-loop
correction. Drive auto-disable remains independent. If the drive disables first,
encoder power is removed after drive-enabled feedback goes low.

**While encoder power is off, position is stale and physical drift cannot be
detected.** Use this mode only where the drive and mechanics can hold position
without that encoder, for example an appropriate stepper setup. A drive's own
internal feedback loop must remain functional. Do not power off feedback that
the drive itself needs for commutation or position holding.

Every new motion request wakes the encoder, including when the drive is already
enabled and drive auto-enable is disabled. ECMC waits for the power-up delay and
for the ready input, if linked. Without a ready input, the configured delay is
used. Two consecutive valid RT cycles qualify readiness, and a fresh absolute
sample is acquired before motion starts. Relative moves use that fresh position.
STOP cancels a pending move; a wake-up timeout faults the axis and disables it.

Intentionally unpowered or warming encoders do not raise not-ready or encoder
hardware alarms during idle/startup. Once ready, loss of readiness or an encoder
alarm is a fault. Transport and relay-output errors are not treated as normal
idle conditions.

## Configure an encoder

This first implementation supports absolute encoders. Incremental encoders are
rejected because cycling power can lose their reference. Configure the relay
after loading the axis/encoder YAML, before starting runtime. These native
commands work with either the Python/Jinja or ECB configuration path; there are
no new YAML keys.

Example for encoder 2 on axis 6; replace the output entry with the actual relay:

```iocsh
ecmcConfigOrDie "Cfg.SelectAxisEncConfig(6,2)"
ecmcConfigOrDie "Cfg.LinkEcEntryToObject(ec0.s5.binaryOutput03,ax6.enc.power)"
ecmcConfigOrDie "Cfg.SetAxisMonEnableAtTargetMon(6,1)"
ecmcConfigOrDie "Cfg.SetAxisMonAtTargetTol(6,0.01)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrPol(6,1)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrDelay(6,0.1)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrTimeout(6,5)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrOffDelay(6,2)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrEnable(6,1)"
```

| Setting | Meaning | Default |
| --- | --- | --- |
| `SetAxisEncPwrEnable(axis,enable)` | Enable relay power control | 0 |
| `SetAxisEncPwrPol(axis,activeHigh)` | Output 1 powers encoder when 1; output 0 when 0 | 1 |
| `SetAxisEncPwrDelay(axis,seconds)` | Minimum power-up delay, including with ready input | 0.1 s |
| `SetAxisEncPwrTimeout(axis,seconds)` | Maximum time allowed for wake-up | 5 s |
| `SetAxisEncPwrOffDelay(axis,seconds)` | Continuous settling time after atTarget | 2 s |

Delays must be finite and nonnegative; the wake-up timeout must be greater than
the power-up delay. Configure with the axis disabled and encoder power off.
Output polarity and ready-input polarity are independent. Link `ax6.enc.ready`
as usual and use `Cfg.SetAxisEncInvHwReady(6,1)` for an active-low ready input.
All encoder settings act on the selected configuration encoder, not automatically
on the primary encoder. Each power-controlled encoder must be ready before the
axis can start. Restore the desired configuration-encoder selection afterward.

Idle power-off is supported for internal trajectories. External trajectories
must retain powered feedback; switching to an external trajectory while an
encoder is off requires waking it first.

## Readback and errors

`PREFIX:AXIS-Enc02-PwrState` reports encoder 2's state through an `asynInt32`
parameter, `ax6.enc.powerstate2`. Each encoder adds one asyn parameter.

| Value | State |
| --- | --- |
| 0 | Ordinary mode: ECMC does not control a relay |
| 1 | Intentionally powered off; position is stale |
| 2 | Warming up; motion waits |
| 3 | Ready; valid feedback available |
| 4 | Fault |

`GetAxisEncPwrState(axis)` and the matching `GetAxisEncPwrEnable`,
`GetAxisEncPwrPol`, `GetAxisEncPwrDelay`, `GetAxisEncPwrTimeout`, and
`GetAxisEncPwrOffDelay` queries read the selected configuration encoder.

`ERROR_ENC_POWER_TIMEOUT` (`0x1441d`) indicates wake-up timed out.
`ERROR_ENC_POWER_CONFIG` (`0x1441e`) indicates invalid power-control settings.
Normal powered-off and warming states do not produce a power-state alarm;
Fault is a major alarm. An error reset is required after a power fault.

## Live slit simulation tests

With the updated schema-3 dedicated slit IOC running:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case encoder_power
```

This group selects the relay mode temporarily, publishes EL5042 raw position
zero and ready low while off, and tests power-off dwell, re-enable with holding
torque, both relay polarities, early/late readiness, timeout and cancellation.
See `tests/integration/slit/README.md` for fixture and cleanup details.

Off-state simulation can also publish an all-ones raw counter word. The rollover
case alternates this with zero and checks retained position and repeated
relative moves after wake-up, without rehoming between cycles. Standalone tests
verify the raw register/multi-turn value for both overflow and underflow sized
jumps, including encoders without a ready input.
