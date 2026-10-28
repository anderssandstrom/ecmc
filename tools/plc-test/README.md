# Standalone PLC library tests

This first implementation builds `ecmcPlcTest` directly from ECMC's ExprTk
wrapper, expression engine, data storage implementation, error implementation,
and production storage, EtherCAT and LUT PLC functions. No source extraction
or second expression interpreter is involved.

It requires a C++17 compiler, CMake 3.16+, and a POSIX host (Linux). **EtherLab,
EPICS, asyn, an IOC build, root privileges, and hardware are not required.**
Python 3 is optional and is used only for integration tests.

From the repository root containing `devEcmcSup`:

```sh
cmake -S tools/plc-test -B /tmp/ecmc-plc-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ecmc-plc-test -j2
/tmp/ecmc-plc-test/ecmcPlcTest --suite tools/plc-test/examples/smoke.suite
/tmp/ecmc-plc-test/ecmcPlcTest --suite tools/plc-test/examples/libraries.suite
ctest --test-dir /tmp/ecmc-plc-test --output-on-failure
```

The normal regression command also builds this executable in a temporary
directory and runs both the runner checks and library cases automatically:

```sh
python3 tests/run_tests.py
```

To run just that integration, use `python3 tests/test_plc_libraries.py`.
It honors `CXX`, builds without optimization/debug symbols to reduce compile
time, and removes its build directory afterwards. No prebuilt binary is used.

If working from the enclosing repository, use `ecmc/tools/plc-test` for the
source and example paths. ExprTk is a large template library; the first build
can take time and substantial memory. Subsequent runner changes rebuild quickly.

## Suite format

The initial format is deliberately small and line-oriented; it is not YAML or
IOC shell syntax. Blank lines and whole-line `#` comments are accepted. Quote
names or paths containing spaces. PLC paths are relative to the suite file.

```text
case interlock
var global.interlock 1
var global.error 0
var global.enable 1
var static.cycles 0
plc interlock.plc
run 5
expect global.error 0
expect static.cycles 5
set global.interlock 0
run 1
expect global.enable 0
end
```

| Command | Meaning |
| --- | --- |
| `case NAME` / `end` | Delimit a named independent case |
| `var NAME VALUE` | Declare a `global.*` or `static.*` scalar before compilation |
| `storage INDEX SIZE MODE` | Create real storage; mode 0 normal, 1 ring, 2 FIFO |
| `lut INDEX FILE` | Load or replace a real lookup table from a suite-relative file |
| `ec` | Create the in-memory EtherCAT backend (initially absent per case) |
| `ec_memmap INDEX SIZE` | Create a zeroed F64 memory map with SIZE elements |
| `ec_time APP RECEIVE SEND PERIOD OFFSET` | Set nanosecond timestamps, period, and signed monotonic offset |
| `ec_domain INDEX STATE` | Set a domain's state to 0 or 1 |
| `ec_slave POSITION CYCLE_OFFSET EVENT_OFFSET_NS` | Configure a cycle-only input event relative to receive time |
| `plc FILE` | Compile one expression file per case |
| `set NAME VALUE` | Change a declared scalar between evaluations |
| `run COUNT` | Evaluate the expression COUNT times, preserving case state |
| `expect NAME VALUE [ABS_TOL]` | Check a scalar; default tolerance is zero |

Each case must evaluate its PLC and make at least one assertion. All declared
numeric inputs and tolerances must be finite. Non-finite actual results fail
assertions. Undeclared variables and unavailable functions fail compilation.
Use ExprTk comments and syntax in `.plc` files; ECMC file aliases and loading
preprocessing are not implemented here.

Each case gets a fresh expression, variables, storage objects, LUTs, offline
EtherCAT backend and library error state. A failure ends that case;
subsequent cases still run.
Cases share one process, so a crash or timeout ends the whole invocation.
For process isolation, put cases in separate suites and invoke the executable
separately.

`--timeout SECONDS` sets a wall-clock limit for the whole invocation (default
30 seconds, maximum 3600). This also bounds expressions containing infinite
loops. Exit status is 0 for all passed, 1 for failed cases, 2 for suite/CLI
errors, and 124 for timeout. Assertion diagnostics name the case, suite line,
variable, expected value, actual value, and tolerance.

## Supported library and boundaries

The following are the production functions, registered with the real ExprTk
wrapper: `ds_append_data`, `ds_clear_data`, `ds_get_data`, `ds_set_data`,
`ds_get_buff_id`, `ds_set_buff_id`, `ds_get_err`, `ds_is_full`, `ds_get_size`,
`ds_get_avg`, `ds_get_min`, `ds_get_max`, `ds_err_rst`, and `ds_append_to_ds`.
Tests can assert returned ECMC errors by assigning them to declared variables.

The private `offline/` headers replace logging, asyn publication and the
EtherCAT backend. Publication methods do nothing. They do not simulate a running
IOC or test asyn behavior. The production storage algorithms compile unchanged.
These include paths apply only to this executable, never the IOC build.
`ds_push_asyn` is intentionally unavailable because there is no asyn endpoint.

`lut_get_value` uses the same production wrapper and table implementation as
the IOC. Tests cover interpolation, endpoints, clamping, reload, invalid IDs,
missing objects and fresh-case isolation. Table files need at least three rows.

The production `ec_*` wrappers cover bit operations, bit printing, error/reset,
memory-map read/write/copy, copying maps into storage (including scale/offset),
domain state, configured timestamps and cycle-only input-event reconstruction.
Clock values stay fixed until another `ec_time` command; evaluations do not
advance time. Local clock conversions honor the process timezone; library
regressions explicitly use UTC. Timestamp setup accepts exact integers within
the double interface's precision limit.

The backend supplies **only F64 memory maps**. It is an in-memory fixture, not
the production EtherCAT master/memory-map implementation: these tests verify
the real PLC wrappers, not device communication, byte conversion or PDO wiring.
Slaves have cycle-only timing; DC/SYNC schedules and hardware timestamps are
not configured by the suite language.

All `ec_sdo_*` PLC wrappers are registered. They exercise real argument
validation, return values and error propagation against an adapter that reports
missing master/slave/SDO objects. Successful transfers, busy/completion timing
and payload conversion are not simulated. The existing async-SDO regression
tests cover the production SDO implementation separately.

`ec_mm_push_asyn` and `ec_get_time_frm_src` remain unavailable (publication and
live host-clock access). Calling unavailable functions fails compilation.

`test_libraries.py` runs named cases for all 14 supported storage functions,
all three buffer modes, cursor/bounds, statistics, clearing, copy/truncation,
missing objects, invalid numeric indices and error recovery, plus LUT and
EtherCAT-wrapper cases. `test_runner.py` covers suite validation, sequencing,
assertion failures, clean state and timeouts.

This runner evaluates expressions directly through `exprtkWrap`; it does not
yet execute `ecmcPLCMain`/`ecmcPLCTask` scheduling, first-scan handling, axis
cycles, or PLC object bindings. `run` counts expression evaluations, not elapsed
controller cycles. No realtime thread is created and no time advances implicitly.

Motion, real EtherCAT transport, plugins, file I/O functions, full startup files,
multiple interacting PLCs, and automatic variable discovery are not yet
supported. No hardware or plugin backend is linked or loaded. Suite and PLC
files are trusted test inputs, not sandboxed untrusted programs.

The next extension is reusable manually stepped motion cycles.
