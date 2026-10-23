# Hardware-free regression tests

From the repository root, run:

```sh
python3 tests/run_tests.py
```

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
- AddressSanitizer and UndefinedBehaviorSanitizer support and runtime libraries
  for the async SDO and buffer-boundary tests.

Compilers default to `cc` and `c++`. Override them using executable names or
paths in `CC` and `CXX` (not shell command strings):

```sh
CC=clang CXX=clang++ python3 tests/run_tests.py
```

EPICS, an EtherCAT master, hardware and a built ecmc library are not required.
The wiring test optionally accepts `ETHERCAT_INCLUDE=/path/to/include` to
compile against an installed `ecrt.h` instead of its substitute declarations.

## Coverage

The [GitHub Actions workflow](../.github/workflows/hardware-free-tests.yml)
runs the suite on Ubuntu 24.04 with GCC and Clang for pushes and pull requests.
It also supports manual runs. It uses the runner's installed compilers and
Python, and does not initialize submodules or build the full IOC.

| Script | Main checks |
| --- | --- |
| `test_async_sdo_array.py` | Scalar/array payloads, asynchronous request lifecycle, lookup and PLC/C++ interfaces |
| `test_axis_startup_restore.py` | Restored position/homed state, encoder initialization, startup targets and references |
| `test_buffer_boundaries.py` | Command lengths, partial reads, output truncation, storage sizes/indices and buffer modes |
| `test_controller_error_scan.py` | Error priority, cached/live scanning, thread isolation and cache rebuilding |
| `test_ec_port_wiring.py` | Wiring diagnostics, timestamp wraparound, exclusions and unavailable bus information |
| `test_motor_record_dmov.py` | Completion/abort behavior, fresh samples, counter wraparound and command rearming |
| `test_startup_gate.py` | IOC/bus/axis readiness, timeouts, task dispatch, masterless operation and opt-outs |

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
production implementation, use explicit expected behavior, and verify the test
fails when the old bug is reintroduced. Keep fault injection temporary; do not
leave modified production code in the checkout. The runner discovers new
scripts automatically.
