# Drive brake state-machine regression tests

These tests compile the complete production `ecmcDriveBase.cpp`,
`ecmcDriveStepper.cpp`, `ecmcDriveDS402.cpp`, axis data and error implementation.
They execute the normal drive `readEntries()` / `writeEntries()` calls with
controlled feedback and captured output entries. No source extraction, second
brake state machine, PLC, IOC, EtherLab, hardware or realtime thread is used.

Run through the normal regression suite or individually:

```sh
python3 tests/run_tests.py
CC=clang CXX=clang++ python3 tests/test_drive_brake.py
```

The individual script builds in a temporary directory and removes it afterwards.
It requires CMake 3.16+, C++17 and ASan/UBSan (including explicit vptr checks).
To keep a build for inspection:

```sh
cmake -S tests/drive -B /tmp/ecmc-drive-brake -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/ecmc-drive-brake --parallel 2
ctest --test-dir /tmp/ecmc-drive-brake --output-on-failure
```

The `offline/` headers model only memory-backed drive registers, their metadata,
read/write failures and asyn publication. They reuse the publication adapter
under `tools/plc-test/offline`. CSP encoder access is rejected explicitly; the
fixture uses an S32 CSV setpoint. The test target and all its include paths,
sources and sanitizer options are separate from the IOC build.
The unavailable CSP encoder adapter supplies out-of-line virtual definitions
for RTTI/vtable sanitizer linkage; its operational methods reject access.

## Checked expectations

See [the expectation inventory](../EXPECTATIONS.md#drive-brake) for source
references and open questions B1/B2. The API documents sequencing and cycle
units (DOC). No unauthorized release and fault handling are inferred software
invariants (INFERRED), not an independently approved drive safety specification.
Exact timing, output pipeline offsets and error encodings preserve implementation
behavior (COMPAT). Fresh-enable/reset policy still needs confirmation (REVIEW).

Brake output **0 means applied**, **1 means released**.

| State/path | Checks |
| --- | --- |
| Closed | No amplifier request or brake release, even with unsolicited enabled feedback |
| Opening | Request amplifier first; wait for enabled feedback; count only enabled samples |
| Open | Brake released only with enabled feedback and an active enable sequence |
| Closing | Apply brake first; hold amplifier for close-ahead delay during normal shutdown |
| Enabled feedback lost | Apply brake and drop amplifier request immediately, including opening/closing |
| Feedback returns | Do not reopen/restart without a fresh enable command |
| Enable timeout | End opening in closed state; a late response or reset alone cannot reopen |
| Drive error / status-read error | End brake sequence; recovered I/O cannot resume an abandoned request |
| Brake-write error | Report the error; captured output remains unchanged until a successful write |
| Controller drive/master interlock | Apply brake, cancel enable; release alone does not restart |
| DS402 | Real enable handshake, operation-enabled loss, faults, recovery and normal shutdown |

`brake_checks.cpp` covers:

- All combinations of open/close delay 0, 1, 3 and 10 at 0.5, 1 and 10 ms
  sample periods, including delayed initial feedback.
- Feedback loss at every sampled point around opening and closing boundaries,
  simultaneous disable/loss, intermittent feedback and return without rearming.
- Cancelled opening, rapid command reversals and re-enable during closing.
- Startup domain unavailability, enable timeout, negative delay rejection,
  missing brake output, brake-disabled bypass, all three hardware alarm inputs,
  status read failure and brake write failure.
- Every enable-request/enabled-feedback trace of length six (4^6 traces), at
  three timing configurations: 12,288 bounded traces, checking independent
  inferred safety invariants rather than duplicating the implementation. Their
  authorization model assumes the restart policy under review in B2, and the
  minimum release-time assertion preserves the offset under review in B1.
- Real Stepper and DS402 output handling. The controlled DS402 plant responds
  to control words 0/6/7/15/128; it does not model a physical drive.

## Timing convention

**COMPAT / REVIEW B1:** tests preserve the existing strict
`counter > configured_delay` behavior; the public API does not specify the
extra samples or pipeline offsets as a requirement.
The opening counter starts at zero on the first enabled-feedback sample, so
brake release occurs on enabled sample **N+2** for configured open delay N.
Closing retains the amplifier request for N+1 sampled invocations after disable
and clears it on invocation N+2. Delays are cycle counts, not milliseconds.

The tests separately observe internal amplifier requests and output control
words. The Stepper writes the amplifier request from the preceding cycle;
DS402 has its own handshake cycle ordering. Published drive-enabled status
also uses the preceding brake command. These pipeline delays are checked, not
silently removed from the fixture.

These are controller/drive software expectations. Mechanical brake response,
electrical polarity/wiring and actual bus delivery are not simulated. In
particular, a failed brake write or lost bus cannot prove that the physical
brake applied; tests verify the software demand and error handling.
