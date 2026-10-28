# Axis monitor regression tests

`test_monitor.py` builds and steps the production `ecmcMonitor` with real
`ecmcAxisData`. It runs automatically through `tests/run_tests.py` and requires
CMake and a C++17 compiler with ASan/UBSan (including vptr checks).

The fixture controls axis feedback and PLC limit-switch overrides one sample at
a time. It checks:

- Following-error tolerance in both directions, trajectory/drive delays,
  interrupted countdowns, enable loss, monitor disable and error reset.
- Maximum velocity from actual feedback or setpoint, both signs, equality at the
  limit, zero and positive delays, sustained latching, disable and reset.
- At-target tolerance, consecutive samples, loss of tolerance and busy state.
- Forward/backward hard limits, optional latching, moving away, both-limits
  faults, stop-at-any-limit mode and directional PLC interlocks.

The [expectation inventory](../EXPECTATIONS.md#monitor) identifies DOC, INFERRED
and COMPAT expectations, with source references. In particular, the API describes
thresholds and delays, but does not establish all exact sample boundaries or
latch-clearing rules. Questions M1-M4 remain open; the assertions preserve those
behaviors while their intent is reviewed.

**COMPAT / REVIEW:** timing checks preserve the existing sample conventions: following-error
trajectory trips after more than N violating samples, its drive trip at 2N;
at-target requires more than N qualifying samples. Maximum velocity trips at N
violating samples (the first violation when N is zero); the drive delay includes
the trajectory-trip sample. Velocity equal to the limit is permitted. At-target
requires error strictly below its tolerance. These values are observations of
current behavior, not a confirmation that the extra samples or strict at-target
boundary are the intended API contract.

Only logging and external infrastructure use private offline adapters. The
production encoder is linked to retain its real implementation and RTTI, but
these tests do not instantiate an encoder or exercise homing. Existing PLC
virtual-axis suites provide surrounding motion integration coverage. These
controlled samples do not model a physical motor or measure realtime timing.

All build settings and adapters belong to this separate test target. No normal
IOC build files, include paths or runtime test modes are added.
