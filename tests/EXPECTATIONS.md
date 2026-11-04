# Basis of test expectations

A passing test means that its assertions hold for the exercised inputs. It does
not establish that every asserted behavior is the intended product contract.
Compiling production source avoids testing a duplicate implementation; it does
not supply an independent specification.

This inventory distinguishes the bases currently available in the repository.
Labels apply to the expectations named in each row, not automatically to every
assertion in that script. In particular, exact return codes, message formatting,
counter values and callback order remain **COMPAT** unless a cited document
specifies them. Documentation is evidence of stated intent, not proof that the
implementation or documentation is correct.

| Label | Meaning | How to use a failure |
| --- | --- | --- |
| **DOC** | A repository API comment or feature document explicitly describes the behavior. The source and symbol are identified below. | Compare the failure with that text; a test, implementation or document may need correction. |
| **INFERRED** | An expectation based on function purpose, arithmetic, resource ownership or a proposed safety invariant; no independent specification was established. | Review the reasoning before changing production behavior. This label does not imply maintainer approval. |
| **COMPAT** | The test records behavior observed in the implementation or fixture. | Detect an unintended behavior change; do not assume the old behavior is correct. |
| **REVIEW** | Intent is ambiguous, unconfirmed or differs from the documented wording. Used alongside COMPAT/INFERRED. | Resolve the semantic question before using the test to justify a production behavior change. Keep the existing assertion visible meanwhile. |

The user's request to exercise brake timing and enabled-feedback loss establishes
coverage scope. It does not approve every sample boundary, restart policy or
interlock-clearing rule chosen by a test author. No other inferred expectation
below is being presented as an agreed requirement.

## Monitor

Additional common-input cases bind real data items to normal monitor/encoder
inputs and check direct updates, conversion, source loss and recovery. Their
contract is described in [common-data-links.md](../documentation/common-data-links.md).

Tests: [monitor_checks.cpp](monitor/monitor_checks.cpp), built by
[`test_monitor.py`](test_monitor.py).

| Expectations | Basis and evidence |
| --- | --- |
| Following error measures actual versus setpoint; excessive error interlocks motion only when monitoring is enabled. Equality at the tolerance does not exceed it. | **DOC**: [ecmcMotion.h](../devEcmcSup/motion/ecmcMotion.h), `setAxisMonPosLagTol`, `setAxisMonPosLagTime`, `setAxisMonEnableLagMon`. |
| Following-error trajectory trip at sample N+1, drive trip at 2N, both on the first violation for N=0. | **COMPAT / REVIEW M1**: `ecmcMonitor::checkPositionLag`. Public text says “this number of cycles” and does not specify the two-stage multiplier or exact boundary. |
| Interrupted following-error countdown restarts; per-condition flags clear while the error remains until reset; enabled feedback must be true in consecutive samples. | **COMPAT**: `checkPositionLag`, `reset`, `errorReset`. The distinction between condition flags and latched error is recorded, not independently specified. |
| Either actual velocity or setpoint exceeding the maximum can trip; equality is allowed; disabled monitoring must not create an overspeed trip. | **DOC**: [ecmcMotion.h](../devEcmcSup/motion/ecmcMotion.h), `setAxisMonMaxVel`, `setAxisMonEnableMaxVel`. The tests exercise both signs; magnitude symmetry is **INFERRED** from the meaning of overspeed. |
| A drive trip must not periodically disappear merely because its delay counter has elapsed while the trajectory trip remains latched. | **INFERRED**: sustained-interlock invariant; `setAxisMonMaxVelDriveILDelay` says an active overspeed interlock eventually turns off the amplifier. Exact clearing policy is separate (M2). |
| Trajectory trips at max(1,N); drive delay includes that sample. Returning speed below the limit leaves the trip latched; disable, enabled-feedback loss or reset clears the tested flags. | **COMPAT / REVIEW M2**: `checkMaxVelocity`. API delay text does not establish the exact first sample, whether the delays are cumulative, or clearing policy. |
| At-target requires remaining near the target for a configured interval. | **DOC**: [ecmcMotion.h](../devEcmcSup/motion/ecmcMotion.h), `setAxisMonAtTargetTol`, `setAxisMonAtTargetTime`. |
| At-target uses strict error < tolerance, trips after N+1 qualifying samples, and is false while localBusy. | **COMPAT / REVIEW M3**: `checkAtTarget`. “Within” does not explicitly define equality, and “this number of cycles” does not resolve the extra sample. Busy suppression is recorded behavior. |
| A latched hard limit survives switch bounce; without latching, a healthy switch permits motion. | **DOC**: [ecmcMotion.h](../devEcmcSup/motion/ecmcMotion.h), `setAxisMonLatchLimit`. |
| Reversing reported velocity clears the directional latch while moving is still true. | **COMPAT / REVIEW M4**: `directionalLimits` tests `checkLimits`; the API says motion must stop before a new motion can be activated. The fixture changes velocity directly, so it does not prove a real command can bypass a required stop. |
| Directional PLC interlocks block the named direction; stop-at-any-limit produces both-limits/drive-summary interlocks. | Direction mapping is **INFERRED** from the API names. Exact both-limits error and summary mapping are **COMPAT** with `checkLimits` and `ecmcAxisData::refreshInterlocks`. |

## Drive brake

Tests: [brake_checks.cpp](drive/brake_checks.cpp), built by
[`test_drive_brake.py`](test_drive_brake.py).

| Expectations | Basis and evidence |
| --- | --- |
| Brake output follows amplifier enable with configurable opening delay and applies before normal amplifier shutdown. Delay units are cycles. | **DOC**: [ecmcMotion.h](../devEcmcSup/motion/ecmcMotion.h), `setAxisDrvBrakeEnable`, `setAxisDrvBrakeOpenDelayTime`, `setAxisDrvBrakeCloseAheadTime`. |
| Waiting for enabled feedback must not consume the opening delay; lost feedback, faults and interlocks demand brake application. No unsolicited feedback or late timeout response may release the brake. | **INFERRED** safety invariants, motivated by the requested state-machine coverage. They are software demand expectations, not proof of physical braking or a complete drive safety specification. |
| Release on enabled sample N+2; clear internal amplifier request on closing invocation N+2; one-cycle Stepper/output/publication offsets. | **COMPAT / REVIEW B1**: `ecmcDriveBase::getBrakeCmd`, `writeEntries` and Stepper/DS402 cycle ordering. API documents sequencing and units but not these extra samples. |
| Fresh enable is required after an abandoned sequence; reset alone or recovered feedback/I/O cannot restart it. | **INFERRED / REVIEW B2**: proposed restart policy exercised by `feedbackLoss`, `interlocksAndTimeout`, `configurationAndFaults`. Explicit maintainer confirmation of each path has not been established. |
| Unexpected loss of enabled feedback while enable remains requested latches `ERROR_AXIS_AMPLIFIER_ENABLED_LOST` (`0x14317`) and clears the enable request. Feedback recovery cannot clear the error; reset and a fresh enable recover. Normal shutdown does not raise this error; an existing hardware/axis error takes precedence. | **INFERRED / REGRESSION**: live slit failure stopped without a persistent diagnostic. The axis-level check already names this error, but misses the drive's later status transition before `postExecute` copies it. `feedbackLoss`, `feedbackLossWithoutBrake` and `ds402Integration` exercise the production drive path with brake opening/open, no brake, normal closing and existing faults. This intentionally replaces the warning-only loss behavior; it is not an EtherCAT frame-loss policy change. |
| DS402 state/control words, exact warnings/errors, timeouts and captured-output behavior after failed writes. | **COMPAT** against the production state machine and controlled fixture. No external DS402 conformance review was performed. A failed write cannot establish that the physical brake applied. |

**INFERRED / REGRESSION — rapid restart after amplifier OFF:** a live brake
case reported `0x14317` on both drives after closing the brakes and immediately
requesting coordinated motion. `delayedDisableAcknowledgement` reproduces a
delayed fall of feedback from the previous OFF command. With the new request
pending, keep the amplifier OFF and brake applied until that acknowledgement;
then start a fresh enable sequence. Waiting must be bounded by the existing
enable timeout and must cancel on disable. Once enabled again, a genuine loss
must still latch `0x14317`. Checks cover no brake, brake, 0/1/12/50-cycle delayed
feedback, cancellation, stuck feedback and DS402 restart. Ordinary `Bench::open`
now supplies acknowledged OFF feedback; the dedicated restart scenarios supply
delayed acknowledgements explicitly. The bounded-signal trace distinguishes a
fall during an ongoing amplifier ON request from a pending OFF acknowledgement.

## Master/slave startup

**INFERRED / REGRESSION**: `ecmccfg/tests/simulation/test_slit_recovery.py` executes the new live recovery,
secondary-readiness and brake-cancellation case methods against controlled
CA/plant boundaries and a virtual clock. Negative controls deliberately restart
on readiness/reset or release a cancelled brake and must fail. These fixture
checks validate test assertions, not IOC behavior or atomic CA sampling.
`ecmccfg/tests/simulation/test_slit_preparation.py` executes real preparation/reset/SYNC sequencing with
delayed acknowledgements. It models a stale motor target at zero while actual
position remains 0.067 mm; preparation must obtain fresh readback and motor
SYNC acknowledgement before requesting zero. SYNC is **DOC** in the linked
motor record source. The live setup trace's exact cause remains unconfirmed;
the fixture does not establish an ECMC defect or validate the independent-axis
test body, which had not run when preparation failed.

Encoder propagation regression: `test_encoder_error_propagation.py` compiles
production `readEntries`, `readHwReady` and `hwReady` with controlled transport,
sub-read and primary-selection boundaries. **INFERRED / REGRESSION**: the live
closed-loop idle test observed primary encoder not-ready with axis `ErrId=0`.
The read pipeline saved `ERROR_ENC_NOT_READY` in `encLocalErrorId_` but published
the final sub-read's successful `errorLocal=0`. The axis must receive the saved
encoder error; later successful reads must not erase it, and a different later
sub-read error must not replace the accumulated earlier error. Assertions fail
before the propagation fix. Primary disabled/enabled behavior, explicit reset
and secondary isolation are covered; full IOC sequencing remains a live check.
The subsequent runtime trace reported `0x14417` with enabled feedback and brake
output still high. A valid-domain not-ready primary encoder must clear both
the external enable command and the RT `status.enable` flag, including a pending
enable before brake opening completes. The regression checks these flags and
couples the production readiness/read methods to the production stepper/brake
implementation: the brake applies, amplifier command and velocity become zero,
enabled feedback clears and the encoder error remains. The velocity selection
and feedback transport are controlled boundaries. A secondary encoder must
leave the primary axis enable flags intact; an invalid-domain sample retains
its existing handling rather than becoming a ready-bit fault.

`ecmccfg/tests/simulation/test_slit_enable_timeout.py` executes the live brake/enable missing-ack cases
with controlled CA/plant boundaries and a virtual drive timer read from
`ERROR_DRV_STATE_MACHINE_TIME_OUT_TIME` (15 seconds). **DOC / REGRESSION**:
`setAxisDrvStateMachineTimeout` controls the direct drive transition; the YAML
axis auto-enable timeout is separate. The old eight-second case deadline
expires before the native fault can occur. The cases now allow 20 seconds,
require `ERROR_DRV_STATE_MACHINE_TIME_OUT` and keep the brake applied during
the wait. A premature brake release or unrelated fault must still fail.
The fixture now starts enabled, matching `Rig.prepare()`, and delays OFF
feedback after command removal. Both missing-enable cases must observe the
disabled command, axis state, native feedback and brake before injection. A
missing OFF acknowledgement fails setup without injecting the test fault;
`0x14317` is not accepted in place of `0x14613`.

Master/slave start regressions: `test_master_slave_start.py` couples production
axis execute/auto-enable and state-machine methods to a production trapezoidal
trajectory, with controlled sequencer, group and publication boundaries.
**INFERRED / REGRESSION**: the live slit trace showed master following error
after initial motion was lost while slave brakes opened. Execute must remain
pending at the axis until drive/brake readiness and external-source
acknowledgement; the sequencer and trajectory have not started in this state.
A negative control removes only the axis start permission and reproduces an
offset exceeding the live 0.03 mm threshold with a 250 ms brake delay.
STOP, disable, fault, startup hold and command blocking cancel the pending
request without replay. Explicitly enabled axes with auto-enable disabled also
wait correctly. A genuine slave interlock remains fatal and its fault log is
invoked before cleanup. Timeout codes and transition details are **COMPAT**
with the production methods. A separate regression starts with both slaves
enabled, CENTERY enabled/pending and GAPY disabled. The old direct transition
into MASTER reproduces `0x1432E`; the production IDLE path must instead enable
the whole master group and wait before transitioning. The regression failed
before that condition was fixed and completes the move afterward. These tests
do not establish that live synchronization passes.
See [master-slave-startup.md](../documentation/master-slave-startup.md).

## PLC libraries and runner

Built and invoked by [`test_plc_libraries.py`](test_plc_libraries.py). Private
offline objects establish controllable inputs; values they return are fixture
contracts, not verification of an EtherCAT master or encoder.

| Test group | Basis and limits |
| --- | --- |
| [storage_checks.py](plc/storage_checks.py) | **DOC**: normal/ring/FIFO retention and clearing, [ecmcMisc.h](../devEcmcSup/misc/ecmcMisc.h), `createDataStorage`, `clearDataStorage`. Arithmetic aggregates, bounds rejection and invalid-write preservation are **INFERRED**. Empty aggregates returning zero, physical ring layout/cursor, cursor=size sentinel, clamped copy counts and wrapper-specific error sentinels are **COMPAT / REVIEW L1**. |
| [lut_checks.py](plc/lut_checks.py) | **DOC**: interpolation and endpoint clamping from the class comment and interpolation comment in [ecmcLookupTable.h](../devEcmcSup/misc/ecmcLookupTable.h). The piecewise table supplies manually calculated expected values. Successful replacement is **INFERRED**. Missing/invalid object returns of zero are **COMPAT**. |
| [ethercat_checks.py](plc/ethercat_checks.py) | **INFERRED**: bit arithmetic and field replacement, invalid-index rejection. **COMPAT**: wrapper-specific missing-map/master sentinels, sign conventions and fixture timing values; implementations are in [ecmcPLCTask_libEc.inc](../devEcmcSup/plc/ecmcPLCTask_libEc.inc). SDO access semantics also have **DOC** support in [runtime-sdo-arrays.md](../documentation/runtime-sdo-arrays.md). No successful hardware SDO transfer is exercised here. |
| [motion_checks.py](plc/motion_checks.py) | **INFERRED**: absolute target, relative displacement, velocity direction, halt/power-off stopping and no held-execute retrigger. Edge behavior is also described by implementation comments in [ecmcPLCTask_libMc.inc](../devEcmcSup/plc/ecmcPLCTask_libMc.inc), but that is not an independent motion specification. **COMPAT**: exact errors, tolerances, sampling and virtual-axis readiness/homed defaults. Explicit external feedback does not test encoder processing or homing. |
| [motion_safety_checks.py](plc/motion_safety_checks.py) | **INFERRED**: reject disabled/disallowed moves and invalid trajectory parameters, preserve targets on rejected commands, stop at directional limits and recover with a valid command. **COMPAT / REVIEW L2**: exact error codes, inclusive soft-limit endpoints and held-execute reset/permission edge policy. Sources are `ecmcPLCTask_libMc.inc`, `ecmcAxisBase`, `ecmcAxisSequencer` and `ecmcMonitor`; no independent PLCopen conformance claim is made. |
| [runner_checks.py](plc/runner_checks.py) | **DOC**: suite syntax, explicit run/cycle distinction and isolated cases in [the tool README](../tools/plc-test/README.md). **INFERRED**: invalid input fails, exceptions are contained and timeout is bounded. **COMPAT**: exact messages/count formatting and test-tool fixture defaults. |

## Other regression scripts

These entries separate functional expectations from implementation-preservation
checks. They do not claim that every branch or integration path was reviewed.

| Script | Basis of expectations and source |
| --- | --- |
| [test_async_sdo_array.py](test_async_sdo_array.py) | **DOC**: staging, busy ownership, upload bounds, previous good data, scalar/array distinction and error clearing in [runtime-sdo-arrays.md](../documentation/runtime-sdo-arrays.md). **COMPAT**: exact call counts and numeric error encodings. |
| [test_axis_startup_restore.py](test_axis_startup_restore.py) | **INFERRED**: preserve restored coordinates/homed state and align startup references before motion. **COMPAT**: absolute/incremental encoder defaults, filter initialization and encoder-reference ordering in `ecmcEncoder`/`ecmcAxisBase`. Extracted startup code is not a homing test. |
| [test_buffer_boundaries.py](test_buffer_boundaries.py) | **INFERRED**: bounded copies, NUL termination, no oversized dispatch, invalid writes do not corrupt buffers. **DOC**: buffer retention modes in `ecmcMisc.h::createDataStorage`. **COMPAT**: partial-read consumption, output truncation/error shape, zero-length behavior and notification counts. |
| [test_simulation_records.py](test_simulation_records.py) | **DOC**: generic simulation I/O field records and native shared storage in [simulation-io.md](../documentation/simulation-io.md). **INFERRED**: reject collisions before registration, never initialize feedback by writing zero, and retain exact 64-bit buffers. Real generator/driver with controlled EPICS static-database calls; no real record processing, asyn conversion, CA/PVA or IOC build. |
| [test_el7041_model.py](https://github.com/paulscherrerinstitute/ecmccfg/blob/master/tests/simulation/test_el7041_model.py) | **DOC / USER**: scaling of 2000 full steps/s at signed 16-bit full scale, 200 motor steps/rev and 12800 encoder counts/rev; independently calculated expected counts. Control/status mappings follow ecmccfg `ecmcEX70XX.cmd` and `ecmcEL7031-chX.template`, described in [the model README](../tools/simulation/README.md). Checks fractional accumulation, wrapping, enable delay, limits and failure cleanup. No physical drive, CA network or RT timing claim. |
| [test_simulation_ioc.py](https://github.com/paulscherrerinstitute/ecmccfg/blob/master/tests/simulation/test_simulation_ioc.py) | **DOC**: prefix-only discovery using aliases of normal first-slave/hardware-type/next-slave metadata, exact supported-model selection, independent workers and read-only listing in [the model README](../tools/simulation/README.md). Controlled CA endpoints and per-worker deterministic clocks; no IOC connection or throughput measurement. |
| [test_ds402_model.py](https://github.com/paulscherrerinstitute/ecmccfg/blob/master/tests/simulation/test_ds402_model.py) | **DOC**: DS402 command/status mappings from `ecmcDriveDS402.cpp` and Beckhoff references in [the model README](../tools/simulation/README.md). Explicit CSV scaling arithmetic; ideal CSP nearest-counter following, instantaneous quick stop and rising-edge fault reset are model assumptions. Native generated-field directions and scanner dispatch use fake CA endpoints. No drive dynamics or live IOC claim. |
| [test_simulation_driver.py](test_simulation_driver.py) | **DOC**: model-free typed storage, shared values, explicit fault injection and lifetime rules in [simulation-io.md](../documentation/simulation-io.md). Standard diagnostic names/bit layouts follow `ecmcEc`, `ecmcEcSlave`, `ecmcEcDomain` and their ecmccfg records; counts and synthetic health/failure semantics follow [simulation-io.md](../documentation/simulation-io.md). Includes 31-device registration, one publication tick per RT phase independent of scalar-write count/slow work, short enable-pulse observation at a specified sampling rate, `ecN.ok` recovery, asyn typo-correction aliases/chains/conflicts sharing the original parameter, and pointer invalidation. Real driver with controlled asyn registration; no network access, packing or physical device model. |
| [test_simulation_configuration.py](test_simulation_configuration.py) | **DOC**: configuration-only backend selection, existing names, memory SDO semantics and separate mixed instances in [simulation-io.md](../documentation/simulation-io.md). **INFERRED**: the C command parser must be able to include its API header. Real registry, complete encoder/drive/monitor linking functions and extracted routing/parser branches with controlled hardware and axes; equivalent scalar/bit attachment, brake/torque/home/interlock setup, invalid-source/slot errors and command blocking for both backends. No IOC startup claim. |
| [test_io_driver.py](test_io_driver.py) | **DOC**: common cyclic I/O contract and timestamp/offset references in [ethercat-simulation-boundary.md](../documentation/ethercat-simulation-boundary.md). **COMPAT**: receive/domain/status order, unchanged integer status, output timestamp forwarding, separate timing readiness, startup reset, slow work and masterless offset forwarding. Real adapter with a controlled endpoint; integration ordering remains covered by `test_startup_gate.py`. No EtherLab/IOC or RT performance claim. |
| [test_domain_health.py](test_domain_health.py) | **DOC**: configurable consecutive incomplete-domain tolerance in `ecmcEthercat.h::ecSetDomainFailedCyclesLimit`. **COMPAT**: exact N/N+1 boundary, complete-sample recovery, limit-setter startup priming and offline-domain summary/local-health distinction in `ecmcEcDomain::checkState`, `setFailedCyclesLimitInterlock` and `ecmcEc::checkDomainsState`. Extracted production methods with controlled working counters; no live transport, axis execution or physical stopping. |
| [test_data_link_startup.py](test_data_link_startup.py) | **DOC**: PLC declarations may precede PLC programs; pending sources resolve before axis input validation in [common-data-links.md](../documentation/common-data-links.md). **COMPAT**: axis/state-machine/PLC validation order without pending links. Executes the real `validateConfig` with fake endpoints; verifies error propagation, not IOC startup or physical motion. |
| [test_common_data_links.py](test_common_data_links.py) | **INFERRED**: unified EtherCAT/PLC/axis command lookup, entry/bit views, filtered/custom domain health, entry lifetime, native scalar access, lazy readable axis sources, bit/computed readers, same-axis/reciprocal links and identical signal self-link rejection, source invalidation, conversion, deferred PLC lookup, atomic pending-source preflight, source health/pins, unlink and runtime-entry expectations, documented in [common-data-links.md](../documentation/common-data-links.md). **COMPAT**: full entry-word storage, whole-value publication, no publication on bit writes and write-before-domain-error behavior. Uses real common access and extracted manager/task methods with fake producer endpoints; real encoder/monitor inputs are additionally covered by `test_monitor.py`. No IOC or hardware performance claim. |
| [test_data_binding.py](test_data_binding.py) | **INFERRED**: proposed read-binding validation, native scalar preservation, explicit sampling, failed-bind invalidation and optional health gating in [the prototype contract](../tools/data-binding-prototype/README.md). **COMPAT**: extracted PLC reference/setter and axis external-setpoint setter behavior. Uses real `ecmcDataItem` storage, but not a running PLC, full axis, PDO conversion, IOC configuration, scheduler or motion-safety policy. |
| [test_controller_error_scan.py](test_controller_error_scan.py) | **INFERRED**: cached and live scans agree for fixed membership; rebuild updates membership and command threads do not use stale RT membership. **COMPAT**: exact error precedence in `ecmcGeneral.cpp::getControllerError`, cache layout and step counts. Two production paths agreeing is an equivalence check, not an independent priority specification. |
| [test_cpp_logic_loading.py](test_cpp_logic_loading.py) | **INFERRED**: reject incompatible ABI/invalid configuration; clean up failed or throwing instance creation. **DOC**: bridge use in [strucpp-ecmc-bridge.md](../documentation/strucpp-ecmc-bridge.md) and ABI declarations in `ecmcCppLogic.h`. **COMPAT**: config normalization/defaults, exact codes and callback ordering. Stops before item binding/asyn registration. |
| [test_cpp_logic_bindings.py](test_cpp_logic_bindings.py) | **INFERRED**: reject missing items, incompatible types/directions/sizes and unsafe auto-sizing; clean up failures. ABI field definitions come from `ecmcCppLogic.h` (**DOC** where explicitly described). Exact flags, statuses and prepare/commit order are **COMPAT** with `ecmcCppLogicLib.cpp`; fake endpoints do not verify asyn. |
| [test_ec_port_wiring.py](test_ec_port_wiring.py) | **DOC**: warning conditions, opt-outs, wrap assumption and unavailable checks in [ethercat-wiring-warnings.md](../documentation/ethercat-wiring-warnings.md). **COMPAT**: message strings and source-hook placement. These assertions verify a diagnostic heuristic, not actual cable topology. |
| [test_filter_memmap_object_paths.py](test_filter_memmap_object_paths.py) | **INFERRED**: reject invalid paths/pointers, deterministic initialized map data, partial writes preserve the remainder, moving-average arithmetic. **COMPAT / REVIEW F1**: zero-sized filter becomes size one, size-one filter enable policy, reset uses zero-filled history, untouched map bytes initially zero. Sources: `ecmcFilter`, `ecmcEncoder`, `ecmcEcMemMap`, `ecmcAsynPortDriverUtils`. |
| [test_lut_reload_soe.py](test_lut_reload_soe.py) | **INFERRED**: failed replacement keeps owned objects valid; retries/destruction are safe; scalar SoE cannot exceed its 64-bit buffer and full-width values survive. **COMPAT / REVIEW F2**: failed encoder LUT reload disables correction despite retaining the old table; accepted odd byte sizes, runtime-write rejection and read-error formatting. Sources: `loadLUT`, `ecmcEncoder::loadLookupTable`, scalar branches in `ecmcCmdParser.c`. |
| [test_motor_record_dmov.py](test_motor_record_dmov.py) | **INFERRED**: a new command must not appear complete from stale status; rearm and counter wrap must work. Post-command sample acknowledgement also has **DOC** support in [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md). **COMPAT**: callback counts/order and specific internal latches in `ecmcMotorRecordAxis`. |
| [test_motor_softlimit_restore.py](test_motor_softlimit_restore.py) | **DOC**: post-autosave snapshot precedence, MRES magnitude/sign, retry, one-time restoration and post-load sample acknowledgement in [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md). **COMPAT**: exact report strings, hook/callback counts, legacy seeding and internal flags. Stand-ins do not exercise autosave or motor record processing. |
| [test_motor_startup_comm_recovery.py](test_motor_startup_comm_recovery.py) | **DOC**: one-time stale COMM recovery and preservation of other/runtime alarms in [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md). **COMPAT**: exact locking/callback sequence in the extracted controller code. |
| [test_motor_startup_execution_gate.py](test_motor_startup_execution_gate.py) | **DOC**: independent physical-axis release, virtual-master group dependencies, separate group/shared-program release, hold reasons/blocker IDs, command rejection without replay, deferred enable and axes without motor records in [ioc-startup-gate.md](../documentation/ioc-startup-gate.md) and [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md). **COMPAT**: internal flag/call ordering; isolated methods do not prove full IOC scheduling. |
| [test_path_thread_lookup.py](test_path_thread_lookup.py) | **INFERRED**: bounded complete path parsing, preserve outputs on rejection, execute startup side effects with NDEBUG, release owned resources on failure. **DOC**: LUT interpolation/clamping in `ecmcLookupTable.h` class comment. **COMPAT / REVIEW F3**: path shorthand/defaults and exact limits/errors, thread priority/fallback sequence, infinity clamping and NaN propagation policy. Sources: `parseEcPath`, `rtThreadCreate`, `startRTthread`, `ecmcLookupTable::getValue`. |
| [test_plc_loading.py](test_plc_loading.py) | **INFERRED**: reject invalid files/indices, respect alias boundaries and comments, propagate append/enable failures. **COMPAT**: accepted alias/comment syntax, substitution and reload ordering in `ecmcPLCMain.cpp`. No compiled PLC execution occurs here. |
| [test_plugin_loading.py](test_plugin_loading.py) | **INFERRED**: reject incompatible ABI/missing required symbols; optional callbacks may be absent; failures unload safely. ABI declarations: `ecmcPluginDefs.h`. **COMPAT**: exact lifecycle sequence, error codes and legacy lookup conventions in `ecmcPluginLib`. |
| [test_softlimit_pair.py](test_softlimit_pair.py) | **DOC**: atomic pair transfer, `(0,0)` sentinel, enable policy and invalid-input preservation in [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md). **COMPAT**: exact return codes/internal representations in `ecmcMonitor::restoreSoftLimitPair`. |
| [test_startup_diagnostics.py](test_startup_diagnostics.py) | **DOC**: independent readiness values, full restore reason and every-poll publication in [motor-record-startup-restore.md](../documentation/motor-record-startup-restore.md), “Diagnostic PVs”. **COMPAT**: exact parameter keys and callback shape in `publishStartupDiagnostics`. |
| [test_startup_gate.py](test_startup_gate.py) | **DOC**: readiness, stabilization, one-time targeted reset, held/allowed producers, masterless operation and opt-outs in [ioc-startup-gate.md](../documentation/ioc-startup-gate.md). **COMPAT**: empty/sparse/full membership, source-index order, axis PLC mapping, live pointed-to state and refreshed membership on runtime re-entry in `ecmcMainThread.cpp::RuntimeObjects`; fresh `StartupState` progress on runtime entry (both stages released when the gate is disabled); internal counter/call boundaries and explicit cycle traces in `cyclic_task`: sequence execution; each axis PLC immediately before its axis (using sparse fixture IDs); PVT, master/slave, plugin, C++ logic and general PLC order; stop/disable enforcement before axis dispatch and after general PLCs; slow diagnostics before safety; safety outside the startup execution gate; publication before EtherCAT send; real I/O adapter in the trace fixture; initialized/unhealthy and uninitialized-real-master distinction; once-per-cycle activation sampling; explicit `MASTER_ID=-1` traces retain held/ready axis/task order and slow axis diagnostics, skip bus I/O/diagnostics, and update the PLC timing offset. Task return-code assignment is also **COMPAT**. Controlled cycles do not test realtime scheduling, locking, shared memory, endpoint internals or physical output timing. |
| [test_storage_creation_diag_io.py](test_storage_creation_diag_io.py) | **INFERRED**: reject invalid mode before replacing an existing object; propagate diagnostic file open/write/close failures. **DOC**: supported modes in `ecmcMisc.h`. **COMPAT**: exact error mapping and cleanup sequence in `createDataStorage` and `ecmcMotionDiag`. |
| [test_timing_storage_list.py](test_timing_storage_list.py) | **DOC**: Hz/ms units in [ecmcMainThread.h](../devEcmcSup/main/ecmcMainThread.h), `setSampleRate`/`setSamplePeriodMs`. **INFERRED**: reject nonfinite/invalid timing without changing configuration; reject malformed list tokens without reading beyond their strings. **COMPAT / REVIEW F4**: list accepts NaN/infinity and commits a valid prefix before a later parse/backend failure; exact configured period limits are compatibility values. |
| [test_toolchain_selection.py](test_toolchain_selection.py) | **DOC**: fallback only for implicit defaults and retention of sanitizer settings in [README.md](README.md), “Requirements”. **INFERRED**: explicit user toolchain choices are respected. Mocked preflight does not establish installed compiler/runtime availability. |

## Questions to resolve before changing semantics

Existing assertions remain active. A REVIEW label is neither permission to remove
them nor a claim that the behavior is wrong.

| ID | Decision needed |
| --- | --- |
| M1 | Should configured following-error N mean N or N+1 violations, and is the drive stage at 2N intentional? |
| M2 | Are overspeed delays cumulative or independent? Which sample starts the drive delay? Should latching persist after speed recovers, and which events clear it? |
| M3 | Is at-target tolerance inclusive? Should N qualifying samples suffice, including the zero-time case? |
| M4 | Does a directional limit latch require observed standstill, or may moving-away feedback clear it? Distinguish internal latch clearing from acceptance of a new command. |
| B1 | Are brake N+2 boundaries and output/publication pipeline delays intended externally observable timing, or implementation artifacts? |
| B2 | Confirm fresh-enable/reset policy separately for feedback loss, cancelled opening, timeout, drive faults and recovered I/O. |
| L1 | Specify empty-storage statistics, cursor=size, copy-count clipping and wrapper return sentinels. |
| L2 | Confirm endpoint inclusivity and whether failed/blocked/reset motion commands consume an execute edge. |
| F1 | Specify zero-sized filter handling and filter reset history before changing startup/filter behavior. |
| F2 | After a failed encoder correction-table reload, retain active correction or disable it? Ownership safety does not decide this policy. |
| F3 | Confirm LUT nonfinite-input and path shorthand policy; an out-of-bounds fix alone does not establish the preferred result. |
| F4 | Should list writes be atomic on malformed input, and should storage accept nonfinite samples? Current tests preserve partial commits and acceptance. |

## Maintaining tests

`ecmccfg/tests/simulation/test_slit_soft_limits.py` checks **DOC** predictive rejection through sequencer
soft-limit warnings and monitor interlocks. A valid response can have `ErrId=0`
and remain near its starting position. The live test must establish an executable
in-range command path and fresh direction-specific diagnostics before accepting
the bounded stop. Unrelated errors, a stationary no-op with no diagnostic,
continued motion, wrong-direction diagnostics and position/setpoint overruns are
failures. This does not establish live IOC behavior by itself.

The motor soft-limit fixture models rollback of a rejected target and retained
LVIO on a same-target no-op. The live case must first complete a distinct valid
move, then establish fresh LVIO for each invalid direction without motion or a
direct LVIO write. Original bounds must be restored afterward.

Slit configuration schema 2 links EL70xx status bit 3 to alarm 0 and control
bit 1 to reset. Offline rendering verifies those links, and production drive
checks require ERROR_DRV_HW_ALARM_0 with the brake applied for fault words with
and without enabled feedback. The live drive-fault case additionally verifies
that injection reaches the raw status PV. Older IOC schemas are refused before
any writes; these checks do not establish that a restarted live IOC passed.

`ecmccfg/tests/simulation/test_slit_motion_requests.py` checks **DOC / INFERRED** motor put-notify
semantics: a short configuration-write timeout must not become the motion's
completion deadline. Fake PyEpics-compatible callbacks establish independent
request completion, continued motion/final-position checks and rejection
diagnostics before teardown. A reported completed write alone is insufficient
to pass a move. These checks do not establish live IOC motion success.

STOP uses an ordinary nonblocking put without notification, so it can interrupt
an active VAL put-notify rather than queue behind it. The fake PV fixture models
the EPICS per-record notification queue. Offline checks
require ramp-down to finish before cleanup disables the amplifier and verify
that an actual 0x14c08 STOP fault remains a failure. Explicit velocity-ramp
arithmetic checks both internal and EL5042 encoder updates until standstill;
there is no plant-level shortcut that freezes position on a STOP request.

`ecmccfg/tests/simulation/test_slit_acceptance.py` checks **USER / DOC** arithmetic from the supplied slit
YAML: Q15 velocity at 10 mm/s, 12800 internal counts/mm, 4096 EL5042 counts/mm,
-1380 mm offset and ready bit 2. The standalone plant and SIM/schema guard use
controlled PVs. Test enumeration and report serialization are infrastructure
checks, not evidence that the [live acceptance suite](integration/slit/README.md)
passed. Primary not-ready and following-error acceptance expectations reference
`ecmcEncoder::readHwReady` and `ERROR_MON_MAX_POSITION_LAG_EXCEEDED` respectively.

Slit brake-module enumeration and configuration wiring are also checked offline:
EL2008 slave 5 channels 01/02, release delay 250 cycles, close lead 200 cycles.
The live module observes output readbacks without manually writing them. Its CA
windows check functional order; exact cycle behavior belongs to
`test_drive_brake.py`, and no physical brake mechanics are inferred.

`ecmccfg/tests/simulation/test_simulation_terminals.py` uses **DOC** raw fields/widths from ecmccfg EL5042,
EL5101 variants, EL5102 and EX7062 CSV/CSP mappings. Exact integer writes, native
range validation and per-channel independence are checked with fake CA endpoints.
Manual encoder values have no inferred mechanical coupling, counter/latch
protocol or oversampling waveform behavior. The GUI test also checks numeric
64-bit entry without float conversion. See [the tool README](../tools/simulation/README.md).

`ecmccfg/tests/simulation/test_simulation_gui.py` uses **DOC** channel layouts from ecmccfg EX1004/1008/
1016 and EX2004/2008 scripts. Input retention, manual input/output writes,
read-only fallback, stale-value display and background-worker access follow
[the simulation tool README](../tools/simulation/README.md). Controlled CA
endpoints establish direction/command behavior, not live IOC latency. Actual
Tk widgets are smoke-tested under a usable display/Xvfb; that portion reports
a skip when the environment cannot provide a display.

For new or changed expectations, put the basis near the assertion or case group
and cite the relevant document/symbol. Calculate arithmetic and trajectory
expectations from explicit inputs rather than calling the implementation under
test to construct its own answer. Treat comparisons between two production
paths as equivalence checks. State fixture assumptions and excluded integration
paths.

If a test exposes a likely bug, first distinguish a documented contradiction or
independently reasoned invariant from COMPAT behavior. Record unresolved choices
here. Resolve intent before changing uncertain production semantics; update the
test and its rationale together when a decision is made. Do not weaken an
assertion simply to make the current implementation pass.

**USER / INFERRED — encoder noise:** `ecmccfg/tests/simulation/test_slit_noise.py` checks bounded uniform
measurement noise independently of physical motion and the internal counter,
including quantization (half a 4096-count/mm step), frozen measurement and reset.
Live noise cases require primary EL5042 feedback in closed-loop mode: +/-0.001 mm
preserves at-target; +/-0.014 mm exceeds the configured .01 mm target band but
stays below the .02 mm lag band. They require loss and recovery of at-target
without fault/disable, without changing monitor settings. Negative controls
reject absent target loss, absent recovery, faults and disable. The production
monitor fixture separately steps both signs of .014 mm disturbance and verifies
at-target clears, requires 101 good samples to recover with time=100, and does
not raise a lag error. These offline checks do not establish live CA timing or
closed-loop stability; the dedicated IOC run remains necessary.

**REGRESSION — motor completion diagnostics:** a live closed-loop setup failure
reported a motor alarm/limit while its later snapshot contained LVIO=SEVR=0.
`ecmccfg/tests/simulation/test_slit_motion_requests.py` injects a one-read nonzero severity or LVIO at
completion and requires failure with the exact triggering values retained in
the trace, even if subsequent reads are healthy. This does not classify the
transient as stale or harmless and does not add an alarm grace period.

**REGRESSION — coherent motor problem publication:** the closed-loop live trace
reported native ErrId=0 with motor MSTA problem bit 9, STAT=7 and SEVR=2 after
an intentional encoder fault. `callParamCallbacksUpdateError()` published a
callback before `poll()` updated `motorStatusProblem_`, allowing old problem
status to accompany a new error classification. The helper now sets problem
status before its callback. `test_motor_error_publication.py` compiles the
production helper and verifies every callback on MCU fault/recovery, local
errors and soft-limit interlocks; removing the fix reproduces inconsistency.
The fixture does not emulate motor record processing or prove the full live
failure is resolved. Preparation also waits for MSTA problem to clear before
SYNC/motion: native reset completion precedes asynchronous motor polling.
A problem bit that never clears blocks setup; completion alarms remain failures.

**USER / DOC / COMPAT — reduced torque:** `tests/drive/brake_checks.cpp`
exercises production drive writes to control bit 2 (the dedicated YAML mapping).
Internal Busy requires full torque even within deadband; idle internal motion
uses control deadband. External trajectories use their externally supplied
deadband flag regardless of Busy, matching the existing implementation and
master/slave interface. Repeated flag transitions must preserve amplifier
command and brake release; optional-function disable is also checked. Live
`cases/torque.py` observes physical/coordinated motion and closed-loop encoder
noise without manually injecting drive-control output bits. No claim is made
about physical current/torque or new disable/fault output policy.

**USER / INFERRED — slit boundaries and limit bounce:** directional recovery
keeps the switch active and requires a blocked request into it plus successful
motion away without reset. Bounce cases require each low/high edge observable,
a stop before the old target, and no replay after input recovery. Coordinated
STOP requires both physical blades stationary, no axis fault, bounded stop
position and successful fresh coordinated motion. Wrap tests observe native
16-bit counter rollover both ways within existing soft limits, while rejecting
position jumps, faults and motor alarms. `ecmccfg/tests/simulation/test_slit_boundaries.py` executes the
live methods with controlled CA/plant boundaries and deliberately broken
responses; these probes validate verdicts, not controller behavior. The real
monitor fixture separately steps single-cycle directional limit bounce with
latch on/off, and the real incremental encoder reconstructs 12000 samples
crossing zero against independently calculated signed counts / 12800. PLC
limit overrides bypass hardware input filtering. Live CA pulse timing and
full coordinated ramp-down still require a dedicated IOC run.

### Shared I/O resolver and timing capabilities

- DOC: common-data-links.md: EtherCAT and simulation resolve through the same
  configuration resolver, including PVT, position compare, status output and
  sequencer mode links (`test_simulation_configuration.py`).
- DOC: writable I/O outputs use the same common command for both backends;
  read-only and converted output bindings are rejected. Native writes preserve
  adjacent storage and reject invalid numeric conversions (`test_common_data_links.py`).
- DOC/COMPAT: bound timing uses its own provider's clock and existing EtherCAT
  cycle calculation. Missing timing, invalid health and dead owners prevent
  access (`test_signal_timing.py`). No physical timing accuracy is asserted.

**USER / DOC — native motion and live soft-limit sync:** `cases/native.py`
uses VEL=1, REL=2 and ABS=3 (`ecmcAxisSequencer::setExecute`, core command
records), command data 0, target position/velocity and a momentary execute request.
Staging must not move, REL endpoints are independently specified, velocity
continues after a zero execute command; velocity and position motion stop
through native StpCmd. The command bit auto-resets and its zero value is not
a sequencer execute-state acknowledgement.
Center/gap commands check actual blade positions. The existing motor/native
interface poll timing difference is allowed for soft-limit synchronization:
motor user/dial values and ECMC config readbacks must converge, as must native
and motor-driver enable flags. CfgDLLM/DHLM write the ECMC monitor through the
motor-driver config interface; they are not raw parser commands. Fixtures use
DIR=0, OFF=0 and positive MRES; tests enable sync temporarily and restore it. Negative-coordinate transforms,
sync-disabled behavior and homing are not covered by these new live cases.
`ecmccfg/tests/simulation/test_slit_native.py` and `ecmccfg/tests/simulation/test_slit_limit_sync.py` execute the real harness
methods with controlled boundaries and negative controls, not production
controller simulations. Existing production trajectory and motor soft-limit
fixtures cover that separate layer. A dedicated IOC run remains required.

**REGRESSION — native execute request versus state:** the live center/gap case
completed both moves, then timed out waiting for ExeCmd-RB=0 after a zero
command write. `ecmcAxisBase::axisAsynWriteCmd` treats execute as an auto-reset
request; it starts a new sequencer 0->1 transition for each accepted non-busy
request. The status bit can remain high at rest. The corrected harness never
uses that status as command-clear acknowledgement, and uses native STOP for
velocity motion. Its boundary fixture preserves execute status high at idle,
accepts repeated requests, and deliberately fails if zero commands stop motion.
This corrects a test-model assumption; production motion semantics are unchanged.
The attached live snapshot also had CfgSyncSftLim=0. Live sync cases now register
cleanup before enabling it, restore original pairs and flags in their existing
finally blocks, and restore the original sync setting through unittest cleanup.

**USER / DOC / INFERRED — homing:** the live homing matrix exercises procedures
1–10, encoder-configured 14, native manual 15 / motor alias 25, and 21/22 on
absolute feedback through both motor home fields and native commands. Procedure
definitions come from `ecmcDefinitions.h` and `ecmcAxisSequencer::seqHoming*`.
Geometry provides independent reference edges at +/-0.4 mm, cam edges +/-0.08
and midpoint zero. Absolute expectations use a 16384 mm period and measured
phase `(physical + 1380) modulo period`: sequence 21 selects phase-period,
22 phase (home offset zero, turn count one, positive scale). Native Busy and
motor DMOV/MOVN are allowed asynchronous propagation but must all expose motion
and settle correctly; Homed must be cleared during search, set only on success,
and remain false after STOP/disable/fault/missing cam until a fresh command.
The standalone production probe drives real virtual-axis, sequencer, integer
encoder, monitor and trapezoidal code using an ideal velocity follower and PLC
sensor overrides. It covers 26 success combinations, 2 post-home moves and 10 STOP/disable/missing
cam abort scenarios with monitor restoration/retries; it does not emulate a
motor record, real drive, network or PID plant dynamics. Boundary probes check
live harness flag verdicts and the optional physical sensor scene. Procedures
11/12 (latch handshake) and 26 (external-drive handshake) are not yet covered.
The homing capability marker is required; the dedicated fixture needs restart.

**REGRESSION — homing record load dependency:** a live case could not connect
HomProc.OMSL because epicsMotorRecord.jinja2 loads ecmcMotorRecordhome.template
only when the YAML declares homing. Configuring defaults using startup Cfg
commands alone was insufficient. Both physical test-axis YAMLs now declare
encoder.homing and the capability marker is 2. test_slit_homing.py renders the
real template section from both YAMLs, requires its dbLoadRecords, and removes
the section as a negative control to reproduce the missing record-load path.

**REGRESSION — homing request versus resolved procedure:** the live motor
sequence-14 case read MtnCmdData=14 during motion. That is correct: its asyn
parameter binds control.cmdData, whereas readHomingParamsFromEnc changes only
status.cmdData through sequencer setCmdData. The live assertion now requires
the requested ID, including 14, rather than expecting the resolved ID on that
PV. The existing reference-geometry check still requires the configured
backward-limit result. The production homing fixture separately checks that
getSeq()->getCmdData resolves request 14 to 1 for both encoder representations.

**REGRESSION — homing observation and cleanup:** collecting a full IOC snapshot
immediately after triggering a search delayed observation until native sequence
9 had completed, preventing its intended cam-phase abort. Start diagnostics now
record request metadata; full snapshots follow completion/abort. Busy, DMOV,
MOVN and Homed motion assertions remain required. A boundary probe rejects full
snapshots during either motor or native start. Mode-inapplicable cases skip
before IOC setup/cleanup. Cleanup verifies physical and encoder zero within
2 micrometres and standstill without requiring a new Busy pulse for an already
zero move; an ignored displaced return remains a failure.

**REGRESSION — post-reset homing motor status:** after a drive-fault abort,
native Busy/SeqState/Homed and drive velocity were zero, but motor DMOV/MOVN
had changed back to 0/1 during reset recovery. The harness now allows bounded
motor-status settling before its sustained idle check, while checking native
standstill, no errors, false Homed and stable physical position on every
observation. Boundary controls reject persistent motor flags, native restart,
sequencer restart, false Homed, errors, velocity and physical drift during
settling. Live rerun is required to distinguish transient propagation from a
persistent motor-interface defect.

**REGRESSION — native motion after latched motor completion:** native sequence-9
abort still failed its moving-flag check after removing the snapshot delay.
Production poll() only re-evaluated completion when moveReady was already false;
native/PLC commands bypass the motor handlers that clear it. Thus native Busy
could coexist with motor Done after an idle period. Observed Busy now clears
moveReady before completion is considered. test_motor_record_dmov.py reproduces
the failure against extracted production code before the fix, then checks native
start/restart, stale AtTarget, monitor enabled/disabled, settling and native abort
alongside existing command-latch and wraparound checks. This is a confirmed
motor-adapter logic bug; the live homing rerun still needs the rebuilt library.


**USER / DOC / INFERRED — runtime configuration and interface tests:** the
post-home cases use the existing MCU-Cmd asyn record, query/save encoder
configuration selection and post-home settings, enable target 0.2, and use
procedure 14 so the sequencer actually reloads those settings. Real positioning
must expose Busy/DMOV/MOVN, retain Homed and finish at the new-coordinate target;
physical position independently verifies the reference. Cleanup restores stored
settings, reloads them and restores references. The standalone production probe
adds incremental/absolute post-home scenarios (38 total). Busy is required during
actual post-home travel; sequencer state 1003 also includes entry and settling.

Interface bursts issue 24 motor VAL writes, 72 native writes, 48 text writes, or 48 mixed
writes, including identical targets. Ordinary motor puts avoid put-notify queue
serialization. STOP must terminate motion and motor activity, there must be no
queued restart, and fresh motor/native moves must succeed. Disabled-state-machine
cases require independent physical motor/native motion and an unchanged other
blade. Boundary negative controls reject failed STOP, replay, native faults,
out-of-bound travel and coupled blades. These do not measure RT performance or
prove real EPICS motor-record behavior; live execution remains necessary.

Motor OFF/DIR cases independently derive dial position from user coordinates;
SPMG Pause/Go resumes while Stop/Go must cancel. Zero AtTarget tolerance is a
user-requested stimulus: the monitor uses strict less-than, so even zero error
cannot satisfy tolerance zero. Motor DMOV/callback must not complete on arrival,
but STOP must terminate the request and restored tolerance must allow new motion.
Boundary controls reject premature completion and ignored STOP. Coordinate/SPMG
behavior itself is covered by the live cases, not an offline motor-record build.

**REGRESSION — absolute-homing limit restoration:** sequence-22 setup found
motor LLM=0 and native low=-1 after the preceding absolute procedure. Cleanup
previously waited only for native config values. It now temporarily enables
SyncSoftLimits and requires motor user/dial limits to acknowledge the original
native pair before restoring the previous sync flag. Persistent disagreement
still fails; this is a harness restoration fix pending live confirmation, not
proof that controller-side synchronization has no defect.


**REGRESSION — homing with independent soft limits:** the first homing case
failed before motion with SyncSoftLimits=0, motor limits 0/0 and controller
limits -1/+1 enabled. Requiring equality while sync is disabled was a harness
error. Setup now only requires equality when sync is enabled and saves both
user/dial motor values independently. After homing restoration, cleanup restores
the original sync flag and, if disabled, restores the motor pair while checking
that controller limits and enable flags remain unchanged. Boundary checks accept
the independent 0/0 pair and reject accidental controller overwrite. No production
controller policy changed for this correction.


**REGRESSION — preserve disabled sync throughout cleanup:** a later disable-abort
case failed only because cleanup unnecessarily enabled sync and waited for motor
0/0 to change to controller -1/+1. Cleanup now restores the original sync flag
before restoring either pair, and requires cross-interface equality only when
that original flag is enabled. The full cleanup boundary now models no implicit
propagation with sync disabled and explicitly rejects attempts to enable it.
This preserves independent controller/motor limits and avoids exercising an
unrequested flag transition. Inspection of local motorECMC suggests unchanged
numeric limits may not be reapplied on a sync-flag-only transition; that separate
behavior remains unmodified and needs independent live verification. Explicit
soft-limit synchronization tests retain their equality requirements.


**REGRESSION — MCU-Cmd ASCII input field:** the live post-home case could not
connect to MCU-Cmd.AIN. The standard asyn record uses AOUT for ASCII output and
AINP for ASCII input, as documented in
[asyn Record](https://epics-modules.github.io/asyn/asynRecord.html).
The harness now reads AINP and the controlled transport rejects any other reply
field name, rather than accepting arbitrary fake PV names. No database, driver
or motorECMC change is needed.


**REGRESSION — post-home phase representation and teardown ordering:** a live
post-home move finished at axis 0.2 / physical approximately -0.2, but the test
waited for SeqState=1003. ecmcAxisStatusWordType.seqstate has three bits and the
record extracts `(status >> 24) & 7`, so internal 1003 is published as 3. The
predicate now requires encoded 3 together with Homed=1 and moving Busy/DMOV/MOVN
flags, rejecting the identically encoded search phase while unhomed. The
standalone production fixture continues checking the full internal phase.
The failure cleanup then searched with no simulated limit switches because
unittest tearDown clears the sensor scene before registered cleanup callbacks.
Cleanup now rearms HomingInputs before reloading the original config with
procedure 14. A boundary test executes the actual callback after clearing its
scene and requires sensors before the cleanup search. Reference cleanup also
rejects starting further motion after a physical-bound violation. No production
motion or motorECMC code is changed for these harness fixes.

**USER — simulated enable timing:** DS402 now defaults to 10 ms per forward
ready/switched/enabled handshake, including re-enable after quick stop. EL70xx
retains its 50 ms default. Timing starts when each command is first observed,
without counting the elapsed interval preceding that observation. Cancellation
and faults discard pending handshakes; fault, disable and quick-stop reactions
are immediate. Configured zero remains available for isolated arithmetic tests.
Model tests reject acknowledgement before 10 ms, exercise cancellation/re-enable
and fault interruption, and check that CSV integration excludes pre-enable time.
Controlled PV tests advance all delayed handshake steps before expecting motion.
CA/scheduling latency is additional and no deterministic wall-clock claim is made.

**REGRESSION / unresolved live cause — final homing reference publication:**
native sequence 1 completed but one immediate sample gave physical-actual near
zero instead of -0.4. Ramp-down is already included: production homing calculates
final position as final encoder minus latched edge plus home offset. Consequently
physical minus actual identifies the edge regardless of stopping distance. The
harness now allows up to three seconds for the separately published final
position to agree with the unchanged edge expectation and 0.025 mm tolerance,
while requiring no errors, continued Homed and standstill. Reference samples are
retained in the report even if the wait fails. Boundary controls accept delayed
position publication but reject a persistent 0.4 mm error or restarted motion.
This does not establish the live cause; a persistent mismatch still requires
controller/configuration investigation.


**REGRESSION — text execute is a level, not the native-PV request bit:** the
text burst returned Error: 82716 (ERROR_AXIS_BUSY, 0x1431c) on bExecute=1 after
bExecute=0 requested STOP and deceleration was still active. This was an invalid
unconditional-success assumption in the harness. The moving text burst now uses
24 pairs of target updates and execute-high writes; it does not stop/restart
between targets. A controlled boundary rejects any execute-low write inside that
burst and checks every target/repeated execute is sent. Explicit STOP, sustained
standstill, motor/native recovery and strict parser-reply checking are unchanged.
This correction does not weaken the controller's busy guard or modify motorECMC.

**REGRESSION — SPMG during pending motor put-notify:** live Pause timed out on
a synchronous SPMG put while the VAL request was still active. Like STOP, SPMG
must use an ordinary asynchronous put so it can interrupt or resume that record.
The harness checks the field readback separately, then verifies standstill,
resumed native Busy and final position, or cancellation with no replay. The
controlled CA boundary reproduces failure of the old notify write behind VAL,
then exercises actual set_spmg() with ordinary Stop/Pause/Go writes and explicit
acknowledgement. No timeout extension or motorECMC change is used.

**REGRESSION — SPMG Stop cancellation acknowledgement:** Go after Stop resumed
the previous -0.7 target. The test switched to Go after native standstill but
before checking the motor record's cancellation callback. motorRecord do_work()
documents that Stop aligns drive/target fields with readback when the motor stops.
The test now keeps SPMG=Stop until DMOV=1, MOVN=0, MIP=0 and both user/dial targets
match readback within 0.015 mm. During this bounded wait native Busy, errors,
drive movement or physical drift fail immediately. Boundary tests accept delayed
motor cancellation, but reject persistent stale target, restart, drift and faults.
The subsequent Go/no-replay assertion remains unchanged. No motorECMC changes.


**USER — dedicated simulation record port:** generated model injection/readback
views now use `<main-port>_SIM<master-id>`, sized from declared fields independently
of the main port's fixed parameter table. Original ec/axis/PLC parameters remain
on the main port. PV names and sim.ec parameter names/types/shared buffers remain
the same; custom private asyn links must select the new port. The main port owns
auxiliaries and propagates communication gates/EPICS state, calculates publication
cadence including their subscriptions and forwards scalar callbacks/initial
refreshes. Auxiliary construction bypasses ECMC initialization and creates no RT
thread. Resizing occurs at configuration time and preserves registered pointers.
The generator fixture fills the main table before loading generated records,
requires the dedicated links/registrations and unchanged main count, and checks
repeated/later loads and bidirectional shared-storage publication. Extracted real
port constructors/lifecycle methods verify single initialization, collision
rejection, independent capacity/growth, state gates, sampling and callbacks with
controlled asyn calls. These tests do not run EPICS device support or benchmark
RT timing. Rebuild ECMC and restart the IOC for this change; no ecmccfg changes.


**USER — blocked-command and physical-jog isolation:** cases/isolation.py adds
motor/native bursts of 24 blocked center/gap requests while a physical axis
owns motion, and motor JOGF/JOGR release/STOP on both blades in both directions.
Moving physical target and internal trajectory ownership must remain unchanged;
other-blade position must remain stable. Submitted blocked record operations
must complete, virtual Busy/ErrId must stay zero, and rejected commands must not
replay after physical STOP. Jog verdicts require signed travel and velocity,
moving Busy/DMOV/MOVN, ramp-down with no virtual ErrId, idle motor flags/MIP and
cleared jog fields. Both routes prove fresh coordinated recovery without a fault
reset. Ordinary jog press/release puts avoid put-notify serialization. Controlled
boundary tests execute actual case methods and reject pending/accepted/queued
blocked requests, virtual faults during travel or stopping, other-blade motion,
changed physical target, ignored stop, travel overrun and replay. These checks do
not establish live IOC or motor-record behavior; run --case isolation on the
dedicated SIM IOC. The default both-mode plan now has 208 executions/15 expected
mode-specific skips. No production or motorECMC change is made by these additions.

**REGRESSION / unresolved live LVIO cause — motor acknowledgement before limit
restoration:** preparation for closed-loop motor procedure 25 rejected its zero
move with LVIO=1, although the displayed motor/native limits were -1/+1 and the
axis was already near zero without a controller fault. The preceding absolute
procedures change coordinates by roughly 1380 mm. Cleanup previously validated
native/physical zero but did not wait for motor RBV and synchronize its target
before restoring narrow limits. That ordering can expose a stale absolute motor
readback to restored limits; the local motor record also retains LVIO on some
no-op paths. Cleanup now calls the existing guarded sync_motor_record() before
restoring limits. The boundary executes that real method inside full cleanup
with motor RBV delayed at 1380 while native position is zero: limits must wait,
and a readback that never updates blocks restoration. Existing LVIO rejection
and completion verdicts remain strict; no flag is manually cleared. Snapshot
now includes SPDB/MRES/RDBD/BDST for persistent/no-op/backlash diagnosis. The
specific live cause is not proven by the single snapshot; rerun from a clean
SIM IOC. No controller, database or motorECMC changes for this correction.

**REGRESSION / unresolved live cause — manual home acknowledgement:** native
procedure 15 returned old actual position ~0.2001 after finish_home accepted idle
flags/Homed. Manual reference can retain Homed=1 and expose no moving transient,
so those pre-command flags alone cannot acknowledge the new request. Manual
success and reference cleanup now include the requested actual position (same
0.002 mm tolerance) in the bounded completion predicate, while continuously
requiring physical position to stay within 0.003 mm of its starting position.
Boundary tests begin already idle/homed, delay only position publication and
require waiting; ignored references and physical movement fail. Motor completion
callbacks are still required for motor-triggered requests. Persistent wrong
position remains a failure and requires live configuration/controller diagnosis;
no production command semantics or reference values were changed here.

**USER — targeted live reruns:** --test selects IDs containing a substring and
may be repeated with OR matching, combined with existing module/loop filters.
An empty selection fails before EPICS import/connection/writes; setup/cleanup and
SIM guards remain active for selected live cases. Boundary/CLI checks select the
single native manual-reference case, both manual-reference routes across modes,
and reject an unmatched live filter before connection.

**REGRESSION / unresolved live stall — blocked burst versus plant watchdog:**
a blocked-motor burst reported TR_LO error 83470 (ERROR_DRV_HW_ALARM_0) and a
405 ms plant-loop stall. The plant shutdown path deliberately trips both models
and publishes that alarm, so the controller assertion may be secondary. Isolation
checks now inspect plant health before and after error reads. Full synchronous
isolation scans run between four-request batches instead of before every request,
retaining all 24 requests, completion acknowledgements and no-replay/recovery
verdicts; liveness is still checked per request. The watchdog threshold is unchanged.
Per-phase latest/max CA read/write timings and failure context are captured before
withdrawal. A controlled test runs the actual plant loop with normal and 400 ms
feedback-write latency, verifies unchanged watchdog behavior and pre-injection
cause retention, and distinguishes a failed plant from a real error with a healthy
plant. A rebuilt/restarted IOC still reported a 410 ms loop with a 399 ms command
read. The runner now uses a separate CA context/connection for the plant instead
of attaching it to the test-command context with CAThread. The offline context
test checks independent ownership, PV cleanup before destruction, and destruction
on target failure. This does not prove the live contention cause or establish RT
performance; a live rerun is needed to validate the connection separation.

**USER / COMPAT — opt-in native target-write motion and status layout:**
the axis defaults to staged target writes. `Cfg.SetAxisAutoExecuteOnTargetPos`
opts into ABS/REL on each native target write, including identical values, with
normal auto-enable and same-type retargeting. Internal PLC/sequencer setters
remain unchanged. The standalone production-method fixture verifies both native
routes, disabled default, status readback, parsed configuration/query commands,
finite input, command type, ownership/communication/startup/error/trajectory
checks, disabled-axis rejection, stable enable acknowledgement and enable timeout.
Trajectory integration and live record processing remain integration-test scope.

Per user request the original status word retains flags 0–23, assigns bit 24 to
the mode and reserves 25–31. Sequence state and last interlock move to status2
bytes 0 and 1; the upper 16 bits are reserved. Compile-time size checks and raw
bit tests verify both words remain 32 bits and every original flag stays at its
old bit. ECMC internal consumers and ecmccfg record decoders use the second word.
This explicitly changes the packed ABI, requiring matching installation and
plugin rebuilds for consumers of internal axis structures. The eight-bit public
sequence readback now encodes post-home state 1003 as 235, replacing the historical
three-bit value 3 described above; the post-home tests follow that new encoding.

The `target_write` integration group has six cases per loop: PV/text ABS/REL,
identical REL writes, active retargeting and normal auto-enable, with panel mode
readback and motor flags checked. The suite now plans 220 executions with the
same 15 mode-specific skips. Main/native/expert panel dimensions are unchanged.
Offline checks do not establish live timing or prove an IOC run has passed.

**REGRESSION — PyEpics exit crash after context separation:** a successful 208-case
live run ended in SIGSEGV; another pre-test startup timeout ended in invalidMutex
during shutdown. Installed PyEpics 3.5.1 disconnects PV callbacks without clearing
channel handles from its global cache. Destroying the plant context before
clearing those handles lets the exit finalizer use invalid channels. A separate
subprocess with one nonexistent loopback PV reproduced exit -11 with the old
order and exit 0 after clear-channel/flush/context-destroy, without IOC writes.
The plant thread now uses that ordering for its own context only. The offline
context fixture models both caches and requires clearing before destruction on
normal and exceptional exits, while preserving the main client's channels.
The separate startup timeout is not established as fixed by this cleanup change.

**REGRESSION — mode indicator competing with the primary status reader:**
native `Main.M6.bBusy?` returned 0 while `-Stat` retained 0x61e6c8 and the
derived Busy/InStartup PVs stayed 1. The new mode bi had requested the existing
status parameter through UInt32Digital; the existing longin uses Int32. The
first record to bind selects the parameter's publication type, so initialization
order can leave the other interface's callbacks stale. The mode indicator now
derives bit 24 through a calc and soft bi in the existing status forward-link
chain, keeping one Int32 reader. The ecmccfg regression exercises the report's
raw word followed by idle/mode transitions, independently updates status2, and
rejects additional asyn status readers. No native startup gate was weakened.

**COMPAT — command record string length:** MCU-Cmd.AOUT is 40 bytes including
the terminator, regardless of OMAX. New short commands `Cfg.SetAxisAutoExec`
and `GetAxisAutoExec` remain within that limit for four-digit axis numbers;
the original long spellings remain aliases. Live target-write tests use the
short forms. The command transport now rejects requests over 39 UTF-8 bytes
before any put. Boundary tests accept 39 and reject 40 without writing.

The Python model and live-harness checks referenced above moved to ecmccfg.
Run `python3 tests/run_simulation_checks.py` from that repository; C++ tests
remain here. See ecmccfg `tests/simulation/README.md` for the relocated coverage.
