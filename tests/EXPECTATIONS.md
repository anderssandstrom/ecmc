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
| DS402 state/control words, exact warnings/errors, timeouts and captured-output behavior after failed writes. | **COMPAT** against the production state machine and controlled fixture. No external DS402 conformance review was performed. A failed write cannot establish that the physical brake applied. |

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
| [test_el7041_model.py](test_el7041_model.py) | **DOC / USER**: scaling of 2000 full steps/s at signed 16-bit full scale, 200 motor steps/rev and 12800 encoder counts/rev; independently calculated expected counts. Control/status mappings follow ecmccfg `ecmcEX70XX.cmd` and `ecmcEL7031-chX.template`, described in [the model README](../tools/simulation/README.md). Checks fractional accumulation, wrapping, enable delay, limits and failure cleanup. No physical drive, CA network or RT timing claim. |
| [test_simulation_ioc.py](test_simulation_ioc.py) | **DOC**: prefix-only discovery using aliases of normal first-slave/hardware-type/next-slave metadata, exact supported-model selection, independent workers and read-only listing in [the model README](../tools/simulation/README.md). Controlled CA endpoints and per-worker deterministic clocks; no IOC connection or throughput measurement. |
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
