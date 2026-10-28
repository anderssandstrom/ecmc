# Hardware-free regression tests

For standalone tests that execute real PLC expressions and storage library
functions through ExprTk, see [the PLC test executable](../tools/plc-test/README.md).
`test_plc_libraries.py` builds it and runs its integration checks automatically
with this runner; it needs no EtherLab or EPICS.
The actual PLC checks are grouped under [`plc/`](plc/) as
`runner_checks.py`, `storage_checks.py`, `ethercat_checks.py`, `lut_checks.py`
and `motion_checks.py`, with limit/rejection/recovery cases in
`motion_safety_checks.py`.
They share fixture generation in `plc/suite.py`. Examples remain with the tool.

The [drive brake tests](drive/README.md) separately compile and step the real
Stepper/DS402 brake logic, including timing, feedback loss, interlocks and faults.

The [axis monitor tests](monitor/README.md) step the real monitor with controlled
feedback to check following error, overspeed, at-target timing and limit latching.

From the repository root, run:

```sh
git submodule update --init -- exprtkSupport
python3 tests/run_tests.py
```

The PLC executable requires the pinned `exprtkSupport` submodule. Initialize it
once after cloning (and update it when its recorded revision changes). No
other submodules are needed for this test suite.

The runner discovers `test_*.py`, runs each in a separate Python process, and
prints a summary. Failed tests include their compiler/assertion/sanitizer output.
It continues after failures and exits nonzero if any test fails or times out.
Use `--verbose` for all output or `--timeout 300` for slower build machines
(the default is 120 seconds per script). You can invoke the runner by absolute
path from another working directory as well.

To run an individual test:

```sh
python3 tests/test_motor_record_dmov.py
```

## Requirements

- Python 3.6 or newer; no third-party Python packages.
- C and C++ compilers supporting C99 and C++17, respectively.
- CMake 3.16 or newer for the standalone PLC, drive and monitor tests (POSIX/Linux).
- AddressSanitizer and UndefinedBehaviorSanitizer support and runtime libraries
  for the async SDO, buffer-boundary, path/thread/lookup, LUT/SoE and
  filter/memory-map/object-path, storage-creation/diagnostic-I/O and
  timing/storage-list tests.

The runner first compiles and runs small C99/C++17 programs with ASan/UBSan.
Compilers default to `cc` and `c++`; if those fail and neither `CC` nor `CXX`
was explicitly set, it tries `clang` and `clang++` and reports the switch.
If no working pair is found, it stops before running the suite and prints the
toolchain diagnostics. Explicit compiler choices are respected and never
silently replaced. Override them using executable names or paths in `CC` and
`CXX` (not shell command strings):

```sh
CC=clang CXX=clang++ python3 tests/run_tests.py
```

Errors such as `cannot find /usr/lib64/libasan.so...` indicate missing or
mismatched compiler runtime libraries. Select a working toolchain or install
its matching runtimes. Sanitizers are not automatically disabled. In a debugger
or traced sandbox where LeakSanitizer reports that it cannot run under ptrace,
set `ASAN_OPTIONS=detect_leaks=0`; address/undefined-behavior checks remain active.
Individual scripts use `CC`/`CXX` directly; automatic selection applies only to
`run_tests.py`.

EPICS, an EtherCAT master, hardware and a built ecmc library are not required.
The wiring test optionally accepts `ETHERCAT_INCLUDE=/path/to/include` to
compile against an installed `ecrt.h` instead of its substitute declarations.

## Coverage

See [the expectation inventory](EXPECTATIONS.md) for which checks have a
documented basis, which use inferred intent, and which preserve current behavior.
Unresolved timing, latching and input-policy questions are marked REVIEW there
and beside the relevant motion assertions. Passing compatibility checks does
not establish that the current behavior is the intended specification.

The [GitHub Actions workflow](../.github/workflows/hardware-free-tests.yml)
runs the suite on Ubuntu 24.04 with GCC and Clang for pushes and pull requests.
It also supports manual runs. It uses the runner's installed compilers and
Python, initializes only `exprtkSupport`, and does not build the full IOC.

| Script | Main checks |
| --- | --- |
| `test_async_sdo_array.py` | Scalar/array payloads, asynchronous request lifecycle, lookup and PLC/C++ interfaces |
| `test_axis_startup_restore.py` | Restored position/homed state, encoder initialization, startup targets and references |
| `test_buffer_boundaries.py` | Command lengths, partial reads, output truncation, storage sizes/indices and buffer modes |
| `test_controller_error_scan.py` | Error priority, cached/live scanning, thread isolation and cache rebuilding |
| `test_drive_brake.py` | Real drive brake opening/closing timing, feedback loss in all states, bounded signal traces, faults, timeouts, Stepper/DS402 output handling and recovery |
| `test_cpp_logic_loading.py` | Configuration, shared-library/ABI checks, instance creation errors/exceptions and cleanup; stops before item binding/asyn registration |
| `test_cpp_logic_bindings.py` | Missing/invalid items, size/type and direction mismatches, auto-size preparation and cleanup requests; fake data-item endpoints |
| `test_ec_port_wiring.py` | Wiring diagnostics, timestamp wraparound, exclusions and unavailable bus information |
| `test_motor_record_dmov.py` | Completion/abort behavior, fresh samples, counter wraparound and command rearming |
| `test_monitor.py` | Real monitor following-error/overspeed thresholds and delays, latching, reset, enable loss, at-target and directional limits |
| `test_motor_softlimit_restore.py` | IOC restore hooks, limit precedence, MRES, retries and position-load sample acknowledgement |
| `test_softlimit_pair.py` | Production monitor pair transfer, enable flags, positive/negative ranges, zero sentinel and invalid-input preservation |
| `test_startup_diagnostics.py` | Startup diagnostic parameter values, independent readiness, full status text and axis-addressed callbacks |
| `test_motor_startup_comm_recovery.py` | Stale startup COMM recovery, position-load completion, preserved alarms and runtime faults |
| `test_motor_startup_execution_gate.py` | Motion producer hold, rejected commands, deferred enable and absent motor records |
| `test_plc_loading.py` | File/index errors, alias expansion, append/reload behavior and enable sequencing; no exprtk compilation |
| `test_plc_libraries.py` | Real ExprTk, storage, offline EtherCAT wrappers, LUTs and real virtual-axis motion; also runner validation, isolation and timeout |
| `test_filter_memmap_object_paths.py` | Zero-sized filter re-enable, memory-map initial contents and partial writes, object-path bounds and null arguments |
| `test_lut_reload_soe.py` | Failed LUT reload ownership, retry/destruction, scalar SoE size bounds and full-width output |
| `test_path_thread_lookup.py` | EtherCAT path bounds and malformed inputs, thread startup and cleanup with/without NDEBUG, lookup NaN and endpoint handling |
| `test_plugin_loading.py` | Legacy plugin ABI, real shared-library loading/unloading, optional callbacks, failures and reload |
| `test_storage_creation_diag_io.py` | Invalid storage-mode rejection preserving existing objects, diagnostic write/close/open failure handling |
| `test_timing_storage_list.py` | Sample-period units, finite timing values and boundaries, storage-list empty tokens and delimiters |
| `test_toolchain_selection.py` | Compiler fallback, explicit compiler choices, retained sanitizer settings and preflight diagnostics |
| `test_startup_gate.py` | IOC/bus/axis and motor-record readiness, timeouts, runtime membership snapshots and axis PLC mapping, cycle dispatch order, stop/disable enforcement, safety/publication/send order, explicit `MASTER_ID=-1` held/ready dispatch and timing-offset updates, and opt-outs |

Each script reads the current local production source, compiles the relevant
code with lightweight stand-ins, and executes C/C++ assertions. Save source
edits before running; no separate ecmc build is needed. Temporary build files
are removed when the script completes normally.

These are focused logic tests. They do not replace IOC integration tests,
autosave file-loading tests, hardware tests or realtime timing measurements.
Some tests extract code using source markers: moving or renaming the relevant
sections can require a matching harness update.

## Adding a regression

Add a `test_*.py` script or extend the relevant existing script. Exercise the
production implementation, label the expectation basis using
[EXPECTATIONS.md](EXPECTATIONS.md), cite its source or reasoning, and verify the test
fails when the old bug is reintroduced. Keep fault injection temporary; do not
leave modified production code in the checkout. The runner discovers new
scripts automatically.
