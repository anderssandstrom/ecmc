# Modular slit acceptance tests

This program tests a running **simulation IOC** derived from the supplied
`ecmccfg/examples/PSI/lab_setup/master_slave/slit_epics_2026/slit_matrix/startup_test_sim.cmd`.
Its separate configuration is the sibling `slit_matrix_sim_test` directory.
The original slit configuration is unchanged. No test command starts/builds an
IOC or changes the real EtherCAT driver.

The separate setup retains axes 5/6 (TR_LO/TR_HI), axes 12/13 (CENTERY/GAPY),
the production slit kinematic equations and master/slave state machine. It
declares only EK1100, EL7031 slave 12, EL7041-0052 slave 14, EL5042 slave 9
and brake-output EL2008 slave 5;
unrelated rack terminals are omitted. Both physical axes have internal counters
and EL5042 feedback. LO hardware limits are linked to status bits 11/12 instead
of the constant ONE entries in the original file, so both blades can be tested.

## Start and select tests

Start the IOC from the **new configuration directory**, using your normal IOC
launcher:

```sh
cd /path/to/ecmccfg/examples/PSI/lab_setup/master_slave/slit_epics_2026/slit_matrix_sim_test
iocsh startup.cmd
```

The configuration uses `SIM=1`, `ENG_MODE=1`, 1000 Hz and default IOC/PV prefix
`SIMSLIT:`. It loads the local `test-marker.db` and preloads coherent encoder
positions/ready flags before runtime. Set `TEST_IOC`, `ECMCCFG_TEST_VER` and
`ECMC_TEST_VER` in the IOC environment if needed; versions default to `sandst_a`.
The installed ecmccfg must include the startup simulation-mode indicator PV.

From the ECMC repository, first list the plan without any EPICS access:

```sh
python3 tools/simulation/slit_test/run.py
python3 tools/simulation/slit_test/run.py --case limits --loop closed
```

Execute one module or the whole suite:

```sh
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case limits
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case enable
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case feedback --loop closed
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case sync
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case brake
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run
```

`--case` is repeatable. `--loop open|closed|both` defaults to both. `--config`
overrides the configuration-directory location. PyEpics is required only for
live execution (`python3 -m pip install pyepics`). The usual Channel Access
environment selects the IOC host. The runner checks both `MCU-Cfg-Sim-Mode=1`
and the dedicated `SIM:SlitTest:Schema` marker matching `profile.json` (currently
schema 2), plus axis IDs and motor coordinate
conventions **before any writes**. It refuses a normal/non-test IOC.

Do not run `ioc.py`, its GUI or another plant on these same simulation fields
while this runner owns them. The runner supplies its own coupled model. For
manual panel experiments, `--plant-only --prefix SIMSLIT:` starts that model
without running motion tests. Existing caQtDM IOC/axis panels can observe it.

## Independent test modules

| Module | Coverage |
| --- | --- |
| `cases/motion.py` | Both physical axes, positive/negative moves, forward kinematics, stop during motion, native 16-bit encoder wrap in both directions |
| `cases/limits.py` | Forward/backward hardware switches during motion, escape with switch held, bouncing inputs without automatic restart, motor-record soft-limit rejection, controller soft-limit boundary through axis PVs |
| `cases/enable.py` | Missing enable acknowledgement, lost enabled feedback during motion, drive fault |
| `cases/feedback.py` | Primary encoder not-ready at idle/during motion, readiness recovery without automatic restart, unready secondary isolation, frozen EL5042 following error, independent LO move while HI feedback is invalid |
| `cases/noise.py` | Closed-loop EL5042 measurement noise: small noise preserves at-target; larger noise clears it without fault/disable, followed by stable recovery |
| `cases/sync.py` | Center/gap moves and inverse kinematics, physical-to-virtual ownership transitions, external slave trajectory selection, slave feedback fault during master motion, coordinated STOP and fresh-command recovery |
| `cases/torque.py` | Reduced-torque bit 2 clears during physical/coordinated motion, returns after settling, and responds to closed-loop encoder disturbance |
| `cases/brake.py` | Applied when disabled; release after enable; application before power-off; cancellation while opening; release during physical/coordinated motion; failed/lost enable |

There are 78 planned test executions with both loop modes selected. Primary
EL5042 not-ready checks are explicitly skipped in open-loop mode. Every case
uses `Rig.prepare()` to clear injected faults, stop/disable axes, reset errors,
select encoder/gains and return the blades to zero. Teardown clears fault
injection and stops/disables all axes. A failed setup also leads to the runner's
final cleanup; `--keep-going` attempts later independent cases instead of the
default fail-fast behavior. The dedicated IOC configuration is owned by this
suite; gain/encoder/velocity settings are test settings, not a saved user session.

Preparation waits for the plant's restored ready/fault bits and for the
state-machine RESET command to finish. Before each physical return-to-zero
move it requires disabled drive feedback and fresh idle motor readback, then
uses the motor record's `.SYNC` operation to align record targets with readback.
This avoids a stale same-target request after fault cleanup or direct axis-PV
commands. It does not redefine position or silently retry a failed test move.
The motor record's SYNC behavior is implemented in
[motorRecord.cc](https://github.com/epics-modules/motor/blob/master/motorApp/MotorSrc/motorRecord.cc).
Missing reset/SYNC acknowledgement fails preparation; failure snapshots also
include `.PACT`, `.MIP` and `.SYNC`. A preparation failure does not establish
that the subsequent test body's independence/recovery behavior failed.

The new recovery case restores encoder readiness while retaining `0x14417`
and verifies that power stays off. It then resets, verifies that the cancelled
move does not restart, and sends a fresh zero-position request. The secondary
case removes EL5042 readiness while the internal counter is primary and
requires successful motion with no axis fault. The brake-cancellation case
observes amplifier ON/brake applied, withdraws enable, and requires the brake
to remain applied for one second before checking a fresh enable can recover.
These are functional CA observations; per-cycle timing remains covered by the
standalone production-code tests.

Cleanup requests STOP first and keeps the plant updating both encoder images
while the IOC ramps velocity down. It waits for trajectory idle and a drive
velocity command below 0.01 mm/s before requesting disable (allowing small
quantized holding corrections in closed loop). A bounded cleanup failure is
reported and disable is still attempted. The plant is closed only afterward.

The open-loop case selects encoder 1 and Kp=Ki=Kd=0, Kff=1 (feedforward using
the drive's internal counter). Closed-loop selects encoder 2 (EL5042), Kp=2,
Ki=Kd=0, Kff=1. Following-error monitoring remains enabled in both modes.
The test never bypasses the IOC's readiness, limit or ownership logic to obtain
a passing result.

An axis error during a normal move fails immediately with its hexadecimal code,
even if motor put completion has not arrived. Failure snapshots include the
axis last/directional interlocks and raw `SM00-Stat`/`SM00-StateCmd` before
cleanup. These diagnostics identify coordinated aborts without treating them
as ordinary arrival timeouts. A later snapshot can still miss a transient
interlock; the IOC state-machine log is needed to resolve such cases.

Lost enabled feedback while enable is requested must latch
`ERROR_AXIS_AMPLIFIER_ENABLED_LOST` (`0x14317`), remove the enable request and
apply the brake. A transient warning alone is not accepted. Recovery requires
an error reset and a new enable request. The production drive regression checks
this with and without a brake and preserves more specific existing faults.

The controller soft-limit case stages each new target and clears execute before
enabling, so recovery does not reuse the previous rejected target through a
masked write to the shared control word. Enable/execute readbacks are included
in failure snapshots. It first proves valid motion through the same axis
PVs, clears prior soft-limit diagnostics with a valid return, then requests an
out-of-range target. Predictive rejection may stop at the starting position and
set a direction-specific warning/interlock without setting `ErrId`. Success
requires that specific response plus bounded, settled position/setpoint. Merely
remaining idle or faulting for an unrelated reason is insufficient. Warning IDs,
interlocks, enable flags, command values and motor bounds are included in reports.

The motor soft-limit case uses a distinct in-range target before each rejection:
-0.1 before requesting +0.6, then +0.1 before requesting -0.6, with bounds
[-0.4, +0.4]. This establishes fresh LVIO through real motor processing. A
rejected target rolls VAL back to the last valid command; repeating that same
value may leave LVIO unchanged (see the
[motor-record target/limit logic](https://github.com/epics-modules/motor/blob/master/motorApp/MotorSrc/motorRecord.cc)).
The test never writes LVIO directly. It verifies that each invalid request sets
LVIO without moving, and restores the original limits afterward.

## Brake outputs

EL2008 slave 5 `binaryOutput01` is the LO brake; `binaryOutput02` is HI. The
production drive brake state machines write these fields: 0 = applied, 1 =
released. Opening delay is 250 RT cycles and closing lead is 200 cycles at
1000 Hz. These deliberately observable windows allow CA tests to see the
amplifier-on/brake-applied phases during opening and closing. The tests observe
the base simulation readback PVs and never inject `-Set` values into the brakes.
Brake output values are included in test snapshots, including fault reports.
Other fault modules also check that configured brakes apply after a fault.

Restart the dedicated IOC after updating its startup/profile for these brake
links. This module checks functional sequencing through asynchronous CA reads,
not exact cycle counts or atomic signal sampling. The existing
`tests/test_drive_brake.py` C++ tests retain that cycle-boundary coverage. The
Python plant does not model brake mechanics or coil/armature feedback.

Missing-enable tests allow 20 seconds for the drive's default 15-second state
machine timeout, including CA publication margin. This is separate from the
axis YAML's 2-second automatic-enable timeout: a direct `-EnaCmd` write does
not start the motion auto-enable timer. Both missing-enable cases require
`ERROR_DRV_STATE_MACHINE_TIME_OUT` (`0x14613`). The brake case checks that the
brake stays applied throughout the wait. Sudden feedback-loss tests keep their
shorter fault-response deadline.

Both missing-enable cases first establish a disabled drive: enable command
and axis enabled state low, native amplifier command and enabled-feedback bit
low, and brake applied. Preparation's zero-position moves can leave drives
enabled; suppressing feedback in that state would test lost enable (`0x14317`)
instead of a fresh request without acknowledgement (`0x14613`). Failure to
establish the disabled precondition is reported before injection.

## Coupled plant and fault meaning

`plant.py` reuses the simple EL70xx model. The supplied YAML scaling is:

```text
raw velocity / 32768 × 10 = motor velocity in mm/s
12800 internal counts = 1 mm
EL5042 raw = round((physical_mm + 1380) × 4096), modulo 2^26
EL5042 status bit 2 = ready; warning/error bits 0/1 remain clear
```

Thus physical zero starts with internal count 0 and absolute raw position
5652480 on EL5042 channels 01 (HI) and 02 (LO). The configuration preloads those
values, and the plant refuses inconsistent initial counter images instead of
silently redefining position. It keeps unwrapped physical travel and publishes
both encoder images from the **same motion**, independently of axis engineering
setpoints. EL5042 position is never just copied from the demanded axis position.

Fault injection is per blade: remove ready, freeze only the absolute feedback,
remove enable acknowledgement, inject drive fault, or open a hardware limit.
An invalid primary encoder is expected to raise `ERROR_ENC_NOT_READY` (0x14417)
and remove enable, including the RT request consumed by the drive/brake path.
This also cancels an enable request while the brake is still opening. The
test requires brake application and disabled/zero drive commands in addition
to the error code. Invalid-domain handling is unchanged.
Frozen primary feedback during motion is expected to raise
`ERROR_MON_MAX_POSITION_LAG_EXCEEDED` (0x14c08); frozen secondary feedback in
open-loop mode must not prevent internal-counter motion. These expectations
come from `ecmcEncoder::readHwReady` and `ecmcMonitor` in the current source.
Failures remain failures in the report; the harness does not change IOC policy.

Schema 2 explicitly links each EL70xx drive status bit 3 to `drv.alarm0` and
control bit 1 to `drv.reset`. The drive-fault case first confirms the raw fault
bit is published, then requires `ERROR_DRV_HW_ALARM_0` (0x1460e), disabled/zero
drive commands and an applied brake. An unlinked status bit is not sufficient
to establish a monitored drive fault. Restart the dedicated IOC after this
configuration update; an older marker is refused before test writes.

An invalid encoder can retain its last velocity. Fault-stop assertions therefore
check disabled axis feedback and outgoing zero-velocity/disabled drive commands,
not a zero value from invalid encoder feedback. Hardware-limit and normal stop
tests additionally check valid position/velocity feedback.

## Evidence and limitations

The runner writes `slit-report.json` and `slit-report.xml` (JUnit), or paths
selected by `--report`. Reports include test/mode/status, elapsed time, failure
details, axis diagnostics and move snapshots. Plant errors, bounded-wait
timeouts and cleanup write failures produce a nonzero process exit. Ctrl+C or
SIGTERM stops/disables axes and withdraws enabled plant feedback; reports retain
completed test results.

Motor `.VAL` requests use asynchronous put-notify: motor-record put completion
can wait until the motion is finished. Each request has its own completion
event. The runner first observes motion acceptance (or a completed no-op), then
waits for that request's completion, DMOV/Busy and the target position within
the motion timeout. The short PV I/O timeout is used for ordinary settings, not
as a deadline for completing a move. Explicit submission failures, axis errors,
motor limit violations/alarms and failure to arrive still fail the test. Motion
submission failures include motor alarm/status and axis/startup/blocking state
captured before cleanup, so diagnostics are not replaced by the teardown state.

Motor `.STOP` uses an ordinary nonblocking put with **no callback and no
`use_complete`**. Another put-notify on the same active motor record would queue
behind the pending `.VAL` request and could execute only after the move finished;
see [EPICS processNotify queuing](https://docs.epics-controls.org/en/latest/appdevguide/lockScanProcess.html#processnotify).
The normal STOP test checks DMOV, cleared Busy, low actual velocity, an early
stopping position and no axis error/motor alarm. It does not wait for a STOP
put-notify callback. A following error during STOP
still fails the test. Diagnostic snapshots include outgoing drive words, plant
position/encoder flags, last/max plant step interval and age of its last feedback
publication to help distinguish stalled feedback, disable ordering and IOC faults.

These are live integration/acceptance tests of IOC behavior. Initial live runs
are in progress; the full suite has **not yet been validated** against the IOC.
`tests/test_slit_acceptance.py` validates the plant,
injection, guard, test selection and reports offline using controlled PVs; it
does not substitute for the live suite. No DC scheduling, motor inertia,
electrical dynamics, mechanical backlash or safe stopping distance is modeled.
The 100 Hz Python plant cannot establish a real RT-performance guarantee.
Additional terminal/protocol models and scenarios can be added as separate
modules without adding them to the existing ones.

Encoder noise tests use the existing closed-loop setup (EL5042 primary encoder,
Kp=2); open-loop runs skip these two cases. Run them separately with:

```sh
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case noise --loop closed
```

The plant accepts `plant.inject('TR_HI', encoder_noise_mm=.014)`: the value is a
nonnegative peak amplitude in mm, with uniform noise sampled once per plant
step before EL5042 count quantization. Each blade has a seeded generator
(default seed: drive slave number; optional profile key `noise_seed`). Sample
sequences are repeatable; live timing and controller responses are asynchronous.
Noise affects only the absolute encoder measurement, leaving physical position
and the internal motor counter separate. Freezing the encoder holds its last
measurement, including any noise. Clearing faults removes noise.

The small-noise case uses +/-0.001 mm. The disturbance case uses +/-0.014 mm,
above the configured 0.01 mm at-target band and below the 0.02 mm following-error
band. It requires observed loss of `-AtTarget`, no fault or disable while noisy,
and stable at-target recovery after noise removal. Monitor settings stay
unchanged. At-target may flicker during noise; it need not stay continuously
false. Snapshots include `-AtTarget`, physical position, measured encoder
position before quantization, noise amplitude and measurement error. This
external Python plant tests behavior; it does not reproduce RT noise spectra.

After fault reset, preparation waits for the motor MSTA problem bit to clear
alongside fresh idle readback before SYNC. Native error reset alone does not
acknowledge the motor poller's status update. This wait is bounded; a persistent
motor problem fails setup. The normal motion completion checks still reject
nonzero LVIO or alarm severity and retain their triggering values.

Run the reduced-torque output tests with:

```sh
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case torque
```

Both dedicated physical-axis YAML files already enable reduced torque and link
it to drive control bit 2. These cases observe that native control-word readback
without writing the output. They require the bit high at rest, low during
physical and coordinated motion, and high again after settling. The encoder
noise case runs only in closed-loop mode: +/-0.014 mm measurement disturbance
must clear the bit and at-target without an axis fault, then both recover after
noise removal. External trajectory handling uses the existing state-machine
control-deadband signal; Busy alone does not define reduced torque for slaves.
The plant does not model motor-current or holding-torque physics.

Additional boundary checks are included in the existing modules:

```sh
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case limits --case sync --case motion
```

Directional recovery keeps one hardware switch active: a request into that
switch must remain stationary, while a fresh move away must succeed without
resetting or clearing the input. Both directions and both blades are tested.
Bounce cases interrupt a move with three low/high pulses, waiting for each
edge to become visible at the axis. After stopping short of the target, the
axis must stay stationary for one second after the input recovers; only a new
command can move it. The plant continues publishing throughout deceleration.
These CA-driven pulses are limited by plant/publication timing; they do not
represent electrical contact bounce at microsecond resolution. The standalone
production monitor tests separately step one-cycle pulses and latch policy.

Coordinated STOP is issued to CENTERY after both blades start moving. Both
native velocity commands and measured velocities must settle, all axes must
remain fault-free, and both blades must stop short of the old target. The test
then checks kinematics and a fresh coordinated return move.

The wrap case moves each blade +0.1 -> -0.1 -> +0.1 mm and observes the native
16-bit internal counter crossing 0/65535 in both directions. Axis position must
remain in the expected travel interval and reach each target without alarms.
Existing +/-1 mm soft limits stay unchanged. In open loop this exercises the
primary incremental encoder; in closed loop EL5042 remains primary and the
internal counter is secondary. Production encoder reconstruction is also
checked sample-by-sample offline against an independent signed integer count.
