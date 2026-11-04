# Encoder relay power control

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
after loading the axis/encoder YAML, before starting runtime. The `encoder.power` YAML group below works with both Python/Jinja and ECB.
Native startup commands remain available for existing configurations.

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

`PREFIX:AXIS-Enc02-PwrState` reports encoder 2's power state decoded from
bits 8…10 of `ax6.enc.status2`. Each encoder has one general status-word asyn
parameter, with additional flags for readiness, homed and absolute type.

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

The wake-position-jump case injects a measurement change greater than five
atTarget tolerances while powered off. It checks both signs, retained standby
feedback, fresh-position relative targeting, and physical travel without an
extra correction for the injected measurement shift. Production stepping checks
also cover ABS/REL starts with following-error monitoring enabled.

## Counter handover mode

For a linear stepper/absolute-encoder axis, an optional handover mode keeps
control connected to an always-powered incremental counter while the absolute
encoder is off. It complements the original power mode. Select it explicitly;
backup index zero retains the original behavior. This first version supports
CSV real axes and virtual test axes, internal encoder feedback, and no modulo
range. The counter must not have encoder power control enabled.

When the completed move has satisfied atTarget, any tighter controller deadband,
and the encoder idle delay, ECMC references the counter to the absolute position
and copies its Homed state. It selects counter feedback before removing absolute
encoder power. Counter position remains live and the CSV controller continues
using it while the absolute encoder is off or warming. Manual primary selection
is blocked while handover owns that selection. The usual primary-encoder PV
shows the currently selected encoder.

A new request restores absolute power and waits for its configured delay and
ready input, if linked. Before selecting it again, ECMC compares the fresh
absolute position to the referenced counter. A discrepancy above the configured
limit raises `ERROR_ENC_POWER_MISMATCH` (`0x1441f`), disables the axis and cancels
the pending move. Counter feedback remains selected. Reset alone does not replay
the rejected move; a new request is required. Within the limit, ECMC switches
feedback and rebases the trajectory/controller before releasing the move. REL
commands use the fresh absolute coordinate.

The first wake after startup establishes the absolute coordinate; the counter
has not yet been referenced to it, so a wake mismatch comparison is not available
until the first completed handover. Motion remains blocked until absolute
feedback is valid. This is not an absolute-position plausibility check at boot.

In this new mode, a normal drive disable does not shorten the encoder's settling
delay. Faults and explicit cancellation still abort pending motion. A counter
cannot detect missed steps or external mechanical movement that it does not
measure; the wake comparison detects disagreement before motion resumes.

Configure with the axis stopped/disabled and the absolute encoder powered off.
After the usual relay/delay setup, for absolute encoder 2 and counter 1:

```iocsh
ecmcConfigOrDie "Cfg.SelectAxisEncConfig(6,2)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrEnable(6,1)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrBackup(6,1,0.02)"
```

The selected configuration encoder supplies the absolute feedback. Backup
indices are one-based; the final argument is the positive, finite mismatch
limit in axis engineering units. There is one handover pair per axis.
`GetAxisEncPwrSource(6)`, `GetAxisEncPwrBackup(6)` and
`GetAxisEncPwrDiffTol(6)` report the configured pair and tolerance. Source and
backup queries return zero when handover is disabled.

Disable handover with `Cfg.SetAxisEncPwrBackup(6,0,0.02)` while stopped/disabled.
This leaves the current feedback selection in place. Before manually selecting
the absolute encoder, restore its power and valid feedback. Disable handover
before disabling encoder power control itself.

The existing schema-3 slit fixture already has both encoders and the relay
links. Rebuild/install ECMC and restart the IOC to use the new commands, then:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case encoder_handover --loop closed
```

This group checks repeated counter/absolute handover, positive and negative
wake mismatches, cancellation without replay, and missing-ready timeout. All
seven cases skip in open-loop mode, where the normal fixture already uses the
counter as primary. No additional fixture links are required.

## Generic operator selection and inspection power

The axis does not infer an encoder's hardware role from its index. The operator
selector is **Auto power**, **Encoder 1**, … **Encoder 8**. Only indices configured
on that axis are accepted. In the slit example encoder 1 happens to be the
counter and encoder 2 the absolute encoder; another axis can assign them differently.

| Mode value | Behavior |
| --- | --- |
| 0: Auto power | Default. Uses the configured relay policy and optional source/backup pair. Without either feature configured, existing axis behavior is unchanged. |
| 1…8: Encoder N | Fixed control feedback from that configured encoder. Keeps its own reference; selecting it does not reference or home either encoder. A power-controlled selected encoder stays powered and must finish its delay/readiness checks before selection and motion. Unselected encoders are not automatically powered by moves. |

Changing mode requires a stopped, disabled axis with no pending motion. The
initial implementation supports internal encoder feedback and CSV real axes
(or virtual test axes). The fixed selector also works without a handover pair.
It does not assume BiSS, EL5042, or open-loop hardware. The automatic power pair
still requires the absolute source and always-powered incremental backup
specified in its configuration.

Use `PREFIX:AXIS-EncMode` and `-EncMode-RB` for requested/accepted mode. The
existing `-SetEncIdCmd-RB` shows the encoder actually controlling the axis. Mode
acceptance can precede selection while a powered encoder warms up. No mode
change enables the drive or executes motion. Returning from fixed powered
feedback to Auto starts a fresh atTarget/deadband dwell before power-off in
handover mode.

For inspection, `PREFIX:AXIS-EncForceOn=1` holds the configured power source on
until explicitly released. It does not change a fixed feedback selection or
start motion, and remains set after completed moves. With no handover pair,
it applies to the current primary encoder, which must have power control enabled;
release it before changing the fixed selection. `-EncForceOn-RB` acknowledges the
hold. Releasing it (`0`) restarts the qualified settling delay; a fixed selected
power-controlled encoder remains powered regardless. Fault handling still takes
precedence over the hold. Use these controls rather than writing the relay PDO
or its hardware PV directly.

The native command equivalents are:

```iocsh
ecmcConfigOrDie "Cfg.SetAxisEncMode(6,1)"
ecmcConfig "GetAxisEncMode(6)"
ecmcConfigOrDie "Cfg.SetAxisEncPwrCtrl(6,1)"
ecmcConfig "GetAxisEncPwrCtrl(6)"
# Release inspection power and return to the configured automatic policy:
ecmcConfigOrDie "Cfg.SetAxisEncPwrCtrl(6,0)"
ecmcConfigOrDie "Cfg.SetAxisEncMode(6,0)"
```

Operator mode and inspection controls add **no asyn parameters**. Their EPICS
records share the existing axis packed words; status readbacks derive from the
single existing Int32 status reader.

| Existing word | Bits | Mask | Meaning |
| --- | --- | --- | --- |
| `axN.control` | 14…17 | `0x0003c000` | Mode: Auto power=0, fixed encoder=1…8 |
| `axN.control` | 18 | `0x00040000` | Force ON |
| `axN.status` | 25…28 | `0x1e000000` | Accepted mode |
| `axN.status` | 29 | `0x20000000` | Accepted Force ON |

Both words remain 32 bits. Existing control bits 0…13, status bits 0…24, and the
sequence/interlock word retain their layout. Writes to control fields must be
masked to preserve unrelated commands.

The `encoder_handover` simulation group includes fixed Encoder 1/2 selection
through both EPICS and native text commands, plus inspection power that retains
counter feedback and survives a completed move. Rebuild/install ECMC and ecmccfg
and restart the dedicated test IOC before running the new controls.

## Encoder status word

Each encoder publishes one read-only 32-bit `axN.enc.statusM` asyn parameter
through an Int32 interface. It replaces the dedicated `enc.powerstateM`
parameter; both are not allocated. The encoder status is independent of the
axis status/control words.

| Bit(s) | Mask | Meaning |
| --- | --- | --- |
| 0 | `0x00000001` | Encoder relay power control enabled |
| 1 | `0x00000002` | Relay power commanded ON (logical state, independent of output polarity) |
| 2 | `0x00000004` | Feedback ready: initialized, valid transport and no current encoder error; powered feedback has completed warm-up/readiness checks |
| 3 | `0x00000008` | Encoder homed/referenced |
| 4 | `0x00000010` | Configured absolute encoder type; clear for incremental |
| 8…10 | `0x00000700` | Power state: 0 Ordinary, 1 Off, 2 Warming, 3 Ready, 4 Fault |

Other bits are reserved and zero. The power-command bit reports ECMC's logical
relay command, not a measurement of supply voltage. With power control disabled,
it is clear even if the hardware has an independent, continuously powered supply.
Homed and absolute describe the encoder object and remain visible while its
supply is intentionally off; they do not imply that a fresh position is available.

For example, encoder 2 exposes:

- `PREFIX:AXIS-Enc02-Stat`: complete status word.
- `-Enc02-PwrEnable-RB`, `-Enc02-PwrOn`, `-Enc02-Ready`: individual power/readiness bits.
- `-Enc02-Homed`, `-Enc02-Absolute`: reference and encoder-type bits.
- `-Enc02-PwrState`: existing five-state display, decoded from bits 8…10.

All derived records use a single native Int32 status reader, so homed, absolute
and power readbacks require no extra asyn parameters. The existing expert-panel
power-state displays keep their PV names. Deploy matching ECMC and ecmccfg
updates and restart the IOC because the native parameter name has changed.

The live `encoder_power` group also injects the EL5042 error and warning bits
independently and together during intentional power-off and warm-up. It checks
raw hardware status, controller error/warning IDs and encoder record alarm
severities. A persistent error after wake-up must fault the primary absolute
encoder and reject the queued move. Hardware diagnostic PVs remain free to
report their raw channel condition; suppression applies to ECMC's encoder path.

## YAML configuration: Python/Jinja and ECB

Both parsers accept `encoder.power` on the axis's initial encoder or in a
separate encoder YAML loaded with `loadYamlEnc.cmd`. Put the group on the
encoder whose supply is switched; its index becomes the handover source.
The optional backup index identifies an always-powered incremental encoder.
The backup may be configured later during startup. No hardware type is inferred
from an index.

For example, in the YAML loaded as encoder 2, after configuring counter 1 and
positive, enabled axis atTarget monitoring:

```yaml
encoder:
  # Existing absolute encoder position, scaling, ready and primary settings...
  type: 1
  power:
    enable: true
    output: ec0.s2.BO05.0
    activeHigh: true
    startupDelay: 0.1
    startupTimeout: 5.0
    idleDelay: 2.0
    backup:
      encoder: 1
      maxPositionDifference: 0.02
```

| Key under `encoder.power` | Meaning | Default |
| --- | --- | --- |
| `enable` | Enable automatic relay power control | `false` |
| `output` | Full writable EtherCAT bit path, required when enabled | none |
| `activeHigh` | Logical ON writes 1 when true, 0 when false | `true` |
| `startupDelay` | Minimum warm-up time in seconds, including when ready is linked | `0.1` |
| `startupTimeout` | Maximum wake time in seconds; must exceed startupDelay | `5.0` |
| `idleDelay` | Continuous settled atTarget/tighter-deadband time before power-off, seconds | `2.0` |
| `backup.encoder` | Tracking encoder index, 1…8; must differ from source | none |
| `backup.maxPositionDifference` | Positive permitted source/backup discrepancy on wake, engineering units | none |

Omitting `power` generates no power commands. An empty group or `enable: false`
selects ordinary operation. Omitting `backup` retains the power-control behavior
without automatic feedback handover. Use YAML booleans `true`/`false`, and finite,
nonnegative delays. The existing `encoder.ready` input remains optional. Existing
`monitoring.target` and controller deadband settings supply the settling criteria;
the new group does not silently enable or change these monitors.

The shared template emits power setup after monitoring in an axis file, or after
encoder creation in an additional encoder file. It clears the initial delay
before setting the requested timeout, so a valid timeout shorter than the default
0.1-second delay also works. Both schemas reject invalid types, negative delays,
out-of-range backup indices and incomplete backup settings. Cross-field guards
emit a diagnostic and `ecmcExit` for a missing enabled output, a non-absolute or
external-feedback source, an invalid timeout/delay relation, or zero discrepancy
tolerance. Native validation also rejects missing/disabled atTarget monitoring,
invalid links, a nonexistent/self/unsuitable backup, CSP handover and modulo axes.

Encoder files may be loaded in either order. Final axis validation checks that
both encoders exist and have suitable types, power settings and drive mode
before runtime starts. Runtime mapping changes require a complete valid pair
and a stopped, disabled axis, with the source not ready. There is one handover
pair per axis. Startup remains in Auto power mode; fixed encoder
selection and the inspection Force ON hold are operator controls, not YAML keys.
Deploy the updated ecmccfg templates/schema files. Order-independent startup
requires ECMC with deferred encoder-pair validation; rebuild/install ECMC and
restart the IOC when upgrading from the earlier implementation.

Simulation client and live integration commands shown here now run from the
**ecmccfg repository root**. The dedicated IOC is in
`tests/integration/slit/ioc/`; native C++ tests remain in ECMC.
