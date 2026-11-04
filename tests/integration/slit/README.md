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
python3 tests/integration/slit/run.py
python3 tests/integration/slit/run.py --case limits --loop closed
```

Execute one module or the whole suite:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case limits
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case enable
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case feedback --loop closed
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case sync
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case brake
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case encoder_power
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run
```

`--case` is repeatable. `--loop open|closed|both` defaults to both. `--config`
overrides the configuration-directory location. PyEpics is required only for
live execution (`python3 -m pip install pyepics`). The usual Channel Access
environment selects the IOC host. The runner checks both `MCU-Cfg-Sim-Mode=1`
and the dedicated `SIM:SlitTest:Schema` marker matching `profile.json` (currently
schema 3), plus axis IDs and motor coordinate
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
| `cases/encoder_handover.py` | Two-encoder handover with live counter feedback while absolute power is off, repeated wake-up, positive/negative mismatch rejection without replay, and ready timeout; closed-loop fixture only |
| `cases/encoder_power.py` | EL5042 raw zero/ready low while off, both relay polarities, short/long post-settling delays, motor auto-enable, native relative wake with drive still enabled and auto-enable off, early/late readiness, wake timeout, STOP cancellation, atTarget/deadband dwell reset |
| `cases/noise.py` | Closed-loop EL5042 measurement noise: small noise preserves at-target; larger noise clears it without fault/disable, followed by stable recovery |
| `cases/sync.py` | Center/gap moves and inverse kinematics, physical-to-virtual ownership transitions, external slave trajectory selection, slave feedback fault during master motion, coordinated STOP and fresh-command recovery |
| `cases/homing.py` | Motor/native homing 1–10, encoder-configured 14, manual reference, absolute 21/22; STOP/disable/fault/missing-cam aborts and motor status flags |
| `cases/native.py` | Native ABS/REL/VEL commands, staging before execute, repeated execute requests, native STOP, center/gap commands |
| `cases/isolation.py` | Blocked virtual motor/native command bursts during physical motion; both-direction physical jog release/STOP with no virtual controller errors and fresh coordinated recovery |
| `cases/softlimit_sync.py` | Motor-to-ECMC and ECMC-config-to-motor limit values; enable-flag propagation in both interfaces |
| `cases/torque.py` | Reduced-torque bit 2 clears during physical/coordinated motion, returns after settling, and responds to closed-loop encoder disturbance |
| `cases/brake.py` | Applied when disabled; release after enable; application before power-off; cancellation while opening; release during physical/coordinated motion; failed/lost enable |

There are 266 planned test executions with both loop modes selected. Primary
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

The runner writes `test-results/slit/slit-report.json` and
`test-results/slit/slit-report.xml` (JUnit) under the ECMC repository, regardless
of the working directory. This generated directory is ignored by Git and created
on demand. Use `--report /path/to/report.json` to choose another location; the
matching XML is written alongside it. Runs overwrite the selected report pair. Reports include test/mode/status, elapsed time, failure
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
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case noise --loop closed
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
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case torque
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
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case limits --case sync --case motion
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

Native motion and soft-limit synchronization can be run separately:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case native --case softlimit_sync
```

These modules run in both loop modes. Native motion stages `-MtnCmd` (1 VEL,
2 REL, 3 ABS), `-MtnCmdData=0`, `-TgtPosCmd` and `-TgtVelCmd` with execute low,
enables the axis, then writes `-ExeCmd=1`. The execute command bit auto-resets
after each request. Writing `-ExeCmd=0` only clears the command trigger; it
does not acknowledge or clear sequencer `-ExeCmd-RB` and does not stop motion.
The sequencer status may remain high at idle. Use `-StpCmd=1` to stop. Native arrival
checks use axis Busy, velocity, position and error, without requiring a motor
VAL put-notify. Normal test preparation still uses motor-record return-to-zero
moves; the native test bodies issue their commands through axis PVs only.

Coverage includes no motion before execute, ABS both directions, repeated REL
commands with independent expected absolute positions, signed velocity moves
that continue after a zero execute write and stop through `-StpCmd`, position
motion interrupted by native STOP, and native center/gap commands.
These are moderate nonzero moves so the Busy transition can be observed over
CA. This is behavioral coverage, not an RT command-latency benchmark.

Limit synchronization tests temporarily enable `-CfgSyncSftLim` and restore its
original setting using test cleanup, including after a failure. They require
coherent initial motor/ECMC limit pairs, positive MRES and the fixture's
DIR=0/OFF=0 coordinates. They change motor `.LLM/.HLM` and require
`.DLLM/.DHLM` plus `-CfgDLLM-RB/-CfgDHLM-RB` to agree. Conversely, writes to
`-CfgDLLM/-CfgDHLM` configure the ECMC monitor through the existing motor-driver
configuration interface and must propagate back to motor user/dial limits.
Enable writes through native `-SftLimBwdEna/-SftLimFwdEna` and motor-driver
`-CfgDLLM-En/-CfgDHLM-En` must agree on both readbacks. Only one side is disabled
at a time; the tests make no unbounded move. Original pairs and enable flags
are restored in `finally`, including after a failed assertion. Polling waits
allow the motor record to update more slowly than the native ECMC state.

## Homing coverage

Restart the dedicated IOC with the updated `slit_matrix_sim_test/startup.cmd`
and `test-marker.db` before running homing. Both physical-axis YAML files must declare `encoder.homing` so ecmccfg loads
the motor homing records, including `-HomProc.OMSL`. Startup Cfg commands alone
do not create these PVs. No ECMC rebuild is required by these test additions. The new `SIM:SlitTest:HomingSchema=2` capability marker prevents
homing against an old setup that still links the home sensor to constant ONE.
The base fixture schema stays 2 so other modules keep their existing guard.

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case homing
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case homing --loop closed
```

The homing module contains 44 cases per loop mode. Both motor `.HOMF/.HOMR`
and native `MtnCmd=10`, command data and momentary execute are covered. Motor
HomProc is put in supervisory mode during each case so its normal DOL link
cannot override the selected procedure. The UI cmdData calcout is also disabled
during these explicit sequence tests so it cannot asynchronously replace the
native sequence number. Original parameters, OMSL and calcout disable fields
are restored.

| Procedures | Reference checked |
|---|---|
| 1, 2 | Backward/forward end switch on the departure edge |
| 3, 4 | First home-cam edge after leaving the backward/forward limit |
| 5, 6 | Cam midpoint after leaving the backward/forward limit |
| 7, 8 | First cam edge during backward/forward search |
| 9, 10 | Cam midpoint during backward/forward search |
| 14 | Delegates internally to encoder procedure 1; `-MtnCmdData` retains request 14; reference must match the backward-limit edge |
| Native 15 / motor 25 | Reference changes without physical travel; 25 is the motor-record alias for 15 |
| 21, 22 | Absolute phase relative to the next/previous turn, using EL5042 scaling |

21/22 skip in open-loop mode because that mode's primary incremental encoder
has no absolute bits. Encoder-not-ready abort cases also skip there. Thus the
homing module alone plans 88 cases across both modes with 6 expected skips;
the complete suite plans 266 cases with 26 expected mode-specific skips.
Mode-specific skips are decided before setup writes or cleanup registration.
Index-latch 11/12 and external-drive 26 require additional hardware handshakes
and are not claimed as covered by the current slit plant.

Only while a homing case runs, physical HI uses end switches at +/-0.4 mm and
an active-high cam from -0.08 to +0.08 mm on drive-status bit 13. The plant
continues publishing during reversal, stopping and faults. It does not jump
encoder counts to complete a sequence. An independent +/-0.8 mm physical bound
stops the plant if a search runs away. Other modules retain their ordinary
fault-controlled limit inputs. Successful references are checked against these
physical edges/midpoint, not merely a Homed flag.

Moving searches must show native Busy=1, motor DMOV=0/MOVN=1 and Homed=0, allowing
for the motor poller's slower updates. Completion must show Busy=0, DMOV=1,
MOVN=0, SeqState=0 and Homed=1, with no motor alarm/limit violation. Immediate
manual reference changes need not expose a moving transient, but must complete
their request, set Homed, update position and leave the physical plant still.
Full IOC snapshots are collected after completion or abort, so diagnostic PV
reads do not delay observation of motion or the intended abort phase.
The motor adapter must clear its previously latched Done state when native
Busy is observed, even when no motor-record command handler ran. The regression
fix for this requires rebuilding ECMC and restarting the IOC; updating only
the Python harness does not correct native-motion DMOV/MOVN reporting.

Abort cases stop a search during its initial leg, its reversed leg, or between
cam edges; they also test disable, drive fault, primary encoder-not-ready and
missing cam (unexpected end switch, error 0x14d04). An aborted search must clear
Busy, return DMOV/MOVN to idle and leave Homed false. Restoring inputs/reset
must not replay it. Each case then issues a fresh search and requires success.
After reset, motor completion flags have up to five seconds to settle before
the sustained idle check. Throughout that interval, native Busy/SeqState must
stay zero, Homed must stay false, the drive must remain stationary and physical
position must remain within 0.015 mm of the aborted position. A transient
native restart fails immediately; persistent incorrect motor flags time out.

21/22 produce large engineering coordinates with this encoder's 26 absolute
bits. Those cases temporarily widen both synchronized coordinate limits to
+/-20000 mm; the physical bound remains +/-0.8 mm. Their expected phase is
computed from the physical position, 4096 counts/mm, -1380 mm encoder offset,
and the 2^26-count period. After every homing case, cleanup stops the plant's
motion while maintaining feedback, references each encoder back to the actual
physical position using manual homing, restores the loop's primary encoder,
returns physically to zero, and restores homing parameters and limits. Cleanup
never overwrites the raw plant counters to conceal a reference error.
The cleanup return requires standstill and both physical and encoder positions
within 2 micrometres of zero. It does not require a new Busy pulse for an
already-zero move; the substantive native motion tests still require Busy.


Runtime configuration and interface bursts
-----------------------------------------

The existing `MCU-Cmd` asyn record is used for synchronous text commands and
numeric queries. Parser errors, missing replies and transport alarms fail the
case. The test restores the record's transfer/format fields afterward. Do not
use that command record interactively while a test owns it.

Motor and native post-home cases enable a 0.2 mm post-home target on the primary
encoder at runtime, verify the settings by query, and use encoder-configured procedure 14 to home to the backward
switch. They require Busy=1, DMOV=0, MOVN=1 during internal sequencer state 1003 (published as 235 in the
eight-bit SeqState PV) while Homed is already true, then verify the new-coordinate target and the
independent physical position. Encoder configuration selection, post-home
settings and the usual references are restored afterward. Cleanup uses 14 again
to reload restored settings into the sequencer before manual re-referencing.

`--case interfaces` runs five cases in each loop mode: physical motor/native
moves with the state machine disabled, repeated motor VAL writes, repeated
native requests, alternating motor/native requests, and text-command requests
through `MCU-Cmd`. Each burst starts from verified motion, repeats both equal
and alternating bounded targets, requests STOP, verifies no queued restart,
and proves both interfaces accept fresh moves. Motor bursts use ordinary
asynchronous CA writes rather than serializing put-notify completion callbacks.
These are finite correctness/stress cases, not RT throughput measurements.

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case interfaces
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case homing
```

The interface cases use existing IOC records. The current full suite plans
266 executions with 26 mode-specific skips and uses the schema-3 fixture
described under encoder supply relay tests below.


Motor coordinate and completion controls
---------------------------------------

`--case motor` adds four cases per loop mode. OFF=0.3 and both DIR values are
checked using the independent mapping `user = OFF + sign * dial`; native dial
position must remain unchanged by coordinate edits and must reach each commanded
user target with the correct sign. SPMG Pause must stop and Go must resume its
saved target; SPMG Stop must cancel that target so Go cannot replay it. A fresh
move is required afterward. Saved coordinate/SPMG settings are restored.

The two zero-tolerance cases temporarily set `Cfg.SetAxisMonAtTargetTol(axis,0)`
through MCU-Cmd and query it back. The production monitor uses strict `<`, so
AtTarget cannot become true at zero tolerance. After the trajectory is stationary,
DMOV and the motor request's callback must remain incomplete. STOP must still
complete the operation; restoring the original tolerance must allow fresh normal
moves. The native case checks the same motor completion behavior without a
motor-request callback. Neither case waits indefinitely for AtTarget.

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case motor
```

Homing cleanup restores the original SyncSoftLimits setting before restoring
limits. With sync enabled it requires motor user/dial limits to acknowledge the
restored controller pair. With sync disabled it restores the saved motor pair
independently and verifies that controller limits and enable flags remain
unchanged. Cleanup never enables synchronization solely to restore independent
pairs. Setup requires matching pairs only when synchronization is enabled.


Post-home cleanup reinstates the position-based switch scene before its
configuration-reload search: unittest teardown runs before registered cleanups
and clears that scene. Reference cleanup refuses to start further motion when
physical position has already exceeded the independent travel bound. After a
previous run exceeded that bound, restart the dedicated SIM IOC to reload its
known initial encoder/reference state before rerunning.

Switch-reference checks include ramp-down: `physical_final - encoder_final`
identifies the latched physical switch edge after ECMC applies its reference
correction. The position PV has up to three seconds to publish that correction
after completion/Homed, with the same edge and 0.025 mm tolerance. Errors, loss
of Homed or restarted motion fail immediately. Recent physical/encoder/offset
samples are retained in the report to distinguish publication lag from a
persistent wrong reference.


The text-command burst retargets the running move with `Main.M6.fPosition=...`
and repeats `Main.M6.bExecute=1` (48 command/reply transactions). It does not
clear text bExecute between requests: text bExecute=0 requests ramp-down, unlike
the momentary axis-PV trigger. Immediate re-execute during that ramp-down can
legitimately return ERROR_AXIS_BUSY. The final explicit STOP and no-replay/fresh-
command checks remain required; command errors are not ignored.

SPMG writes use ordinary asynchronous CA puts, like STOP, so they can interrupt
a pending motor VAL put-notify. Field acknowledgement is checked separately;
Pause must actually stop the trajectory, Go must expose resumed native Busy and
reach the original target, and Stop followed by Go must not replay it.

Before Go after SPMG Stop, the test also requires the motor record's cancellation
acknowledgement: DMOV=1, MOVN=0, MIP=0 and user/dial targets synchronized to
readback. Native standstill alone is insufficient. The axis must stay stationary
and fault-free throughout this bounded callback wait; stale targets still fail.


Blocked command and jog isolation
--------------------------------

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case isolation
```

Four cases run in both loop modes. The motor/native blocked-command cases first
prove that physical HI is moving and both virtual axes are blocked. They submit
24 requests to center/gap and require their record-completion callbacks while
physical motion continues. Throughout, virtual Busy and ErrId must remain zero,
the physical target/trajectory ownership must remain unchanged, and the other
blade must remain still. After physical STOP, no rejected request may execute
later. A fresh physical move and center move prove recovery without resetting
away controller faults.

The jog cases use motor JOGF/JOGR on each physical axis, in both directions,
with JVEL=0.2 and JAR=1. They verify signed displacement/velocity and moving
Busy/DMOV/MOVN, then stop either by releasing the jog button or via motor STOP.
Jog writes are ordinary asynchronous puts, so release cannot queue behind a
motion put-notify. Virtual controller ErrId must stay zero during travel,
ramp-down and sustained standstill. Completion also requires cleared jog buttons
and MIP, idle Busy/DMOV/MOVN, an unchanged other blade and no replay. Kinematics
and a fresh coordinated move are checked afterward; jog settings are restored.
Rejection of blocked commands may report write errors at the requested record;
that must not become a latched controller error on the virtual or physical axes.

The complete default plan is 266 executions, with 26 mode-specific skips.
The isolation cases themselves are Python-only; the current default fixture
also includes the encoder relay links described below.

After restoring encoder references and returning physically to zero, homing
cleanup waits for the motor record's fresh readback and synchronizes its target
before restoring narrow soft limits. This avoids applying them while motor RBV
still reflects the large absolute-homing coordinate. A stale motor readback
blocks cleanup; LVIO checks remain strict and are never manually cleared.

Manual homing 15/25 also requires publication of the requested encoder position
before completion is accepted. Idle/Homed may already be true before the command,
so those flags alone are insufficient. During this wait the physical plant must
remain within 0.003 mm of its pre-command position; unchanged/incorrect encoder
reference still fails. The same acknowledgement is used for cleanup re-referencing.

For a quick rerun of one failing case, use the repeatable `--test` substring
filter. Setup, plant startup, guards and cleanup still run normally:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case homing --loop open --test test_native_sequence_15
```

Omit `--loop open` to check both modes. Repeat `--test` to select multiple names
with OR matching (for example `--test test_native_sequence_15 --test test_motor_sequence_25`).
Module, loop and name filters are combined. Omitting `--run` previews the selected
tests without connecting. An empty selection is an error before any IOC writes,
so a typo cannot silently report a successful zero-test run. Omit all filters
for the complete suite.

Plant timing failures during command bursts
------------------------------------------

The plant watchdog remains `max(0.25 seconds, five model periods)`. If it trips,
shutdown deliberately sets a drive fault on both simulated drives. A subsequent
ERROR_DRV_HW_ALARM_0 therefore does not by itself show that a blocked command
faulted ECMC. Isolation checks inspect plant health before classifying axis errors.
The report records latest/maximum command-read and feedback-write times and a
failure-time snapshot captured before shutdown injection. A healthy plant plus
an axis error still fails.

Blocked bursts retain all 24 requests, with isolation scans between four-command
batches instead of before every individual write. Cheap plant liveness checks
remain before each request. This reduces synchronous observer traffic competing
with feedback; it does not relax the watchdog or error requirements. Use
`--test test_blocked_motor_commands_during_physical_move --loop open` for a short
rerun. The generated field PVs should use the dedicated simulation asyn port after
installing the earlier ECMC port-separation change.

The plant also uses a separate Channel Access context (and IOC connection) from
the test-command client. Previously `CAThread` attached both to the initial
context: a dedicated asyn port alone did not separate their CA connection.
A fresh-build live run still measured a 399 ms command read during motor spam.
Connection separation prevents plant reads from queuing behind motor processing
on that same client connection; the live rerun must confirm whether it resolves
this stall. The context closes after the plant disconnects its PVs, including on
failure. The watchdog, all 24 blocked requests, and fault assertions are unchanged.
This is a Python runner change; no IOC rebuild or restart is needed to try it.

Plant context cleanup explicitly clears its cached CA channels before destroying
the context. In PyEpics 3.5.x, disconnecting a PV removes callbacks but retains
the channel in a process-wide cache. Destroying its context first leaves invalid
handles for Python's exit cleanup, which can crash after an otherwise successful
test run. The cleanup only clears channels belonging to the plant context; the
test-command context remains intact. This correction is Python-only.

## Native target-write mode

`target_write` adds six cases per loop mode (12 live executions): PV and text
ABS/REL moves with repeated identical relative writes; retargeting an active
absolute move; and target-triggered auto-enable from a disabled drive. It also
checks that mode selection does not run an old target, default staged writes
remain idle, motor DMOV/MOVN settle, and the panel readback follows selection.
Temporary mode/auto-enable settings and command transport fields are restored.

```bash
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case target_write
```

This feature requires rebuilding/installing ECMC, installing matching ecmccfg
records/panels and restarting the SIM IOC. Status bits 24–31 changed: mode is
bit 24; sequence/interlock bytes moved to `axN.status2`. The post-home phase is
now 235 (`1003 & 0xff`) in `-SeqState`. Existing status flags 0–23 retain their
positions. See [native target-write mode](../../../documentation/native-target-write.md)
for the setting, status layout and packed-status compatibility requirements.
The main/native/expert panels show the mode without changing panel dimensions.

## Encoder supply relay tests

The `encoder_power` group requires the updated dedicated fixture (schema 3),
ECMC encoder-power support, and the ecmccfg `Enc02-PwrState` record. Restart the
IOC after updating its configuration. The fixture links EL2008 output 03 to
LO encoder 2 and output 04 to HI encoder 2; the relay mode stays disabled outside
these cases. The plant couples supply to those outputs only while a power test
opts in, and restores its ordinary continuously powered model during cleanup.

During a power test, an off supply publishes **EL5042 positionActual = 0 and
encoder-ready = 0**. Power-on also models a physical startup delay. The physical
position and drive's internal counter are preserved; ECMC must retain its last
valid encoder position, gate the next move until fresh feedback, and avoid a
false following error. Test cleanup restores ordinary feedback before removing
idle error suppression, then restores native settings and configuration-encoder
selection. Fault/STOP cases must never replay the cancelled target.

During these cases the drive auto-disable policy remains enabled with a temporary
60-second timeout. Disabling that policy would make the slit state machine
immediately disable its physical group at motion completion, preventing the
encoder-only idle power test. Cleanup restores both the original drive timeout
and its enable flag; the encoder's configured dwell is checked independently.

The configured power-off delay starts after the completed move is atTarget and,
when smaller, inside the controller deadband. Encoder power must remain on for
this dwell; losing qualification restarts it. CA timing checks allow 0.2 seconds
for observation latency; exact cycle timing is covered by `tests/test_encoder_power.py`.
Three feedback-change cases require closed-loop EL5042 feedback and are skipped in
open-loop mode. The remaining cases also run with the internal counter primary.

The rollover case also alternates zero and the configured raw counter's all-ones
word while supply is off. It verifies that the jump exceeds ECMC's rollover
threshold, retained position stays unchanged, and repeated wake/relative moves
reach their expected physical and encoder positions without resetting/homing
between cycles. The standalone production checks also place the valid sample
near the top of the raw counter and test zero during power-off, covering both
rollover directions with and without a ready input. They assert the actual raw
register and multi-turn value as well as position and velocity.

`test_wake_position_jump_exceeds_five_at_target_tolerances` injects positive and
negative EL5042 measurement steps while power is off, using six times the
configured atTarget tolerance. Standby feedback must remain retained. A relative
request with the amplifier already enabled must wait for wake-up and use the
fresh shifted encoder position; physical travel must equal only the requested
relative distance. The test leaves following-error monitoring unchanged and
finishes powered off before cleanup removes the measurement shift.

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case encoder_power --test wake_position_jump --loop closed
```

## Counter handover tests

The `encoder_handover` group uses the existing schema-3 relay links and both
configured feedback devices. It requires the ECMC build providing
`Cfg.SetAxisEncPwrBackup`. The fixture starts with this optional mode disabled;
these cases select absolute encoder 2 and counter 1, then remove the mode before
restoring the ordinary encoder-power fixture. Manual primary changes remain
blocked while handover is active. Existing primary-encoder readback must show
counter 1 while asleep and absolute encoder 2 after a valid wake.

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case encoder_handover --loop closed
```

Seven cases cover repeated handover, each mismatch sign, missing ready, generic fixed encoder selection through PV/native commands, and persistent inspection power.
They skip in open loop. Production stepping additionally checks operation with
no ready input, live counter changes while asleep, an unready backup, accepted
small differences with following-error monitoring enabled, and preservation of
the encoder dwell through drive disable.

The generic `-EncMode` selector provides Auto power=0 or Encoder N=1…8 and
requires a disabled axis. It does not encode hardware types. The slit fixture
assigns encoder 1 to the counter and encoder 2 to absolute feedback. `-EncForceOn`
holds the configured source powered for inspection without changing fixed
feedback selection. Both commands reuse existing axis control/status words;
there are no extra native asyn parameters for these controls. See
[encoder power control](../../../documentation/encoder-power.md) for bit masks
and readbacks.

Encoder-power diagnostics tests inject EL5042 warning bit 0, error bit 1, and
both together while its supply is intentionally off. The bits remain asserted
during warm-up; axis/encoder error IDs, axis warning ID and encoder EPICS alarm
severities must stay clear. Clearing diagnostics permits normal motion. A
closed-loop case retains the error through wake-up and requires hardware-alarm
fault `0x14410`, with the queued target rejected and no physical motion. No IOC
configuration changes are needed: the existing EL5042 warning/alarm links are used.
