# Production homing probes

Run `CC=clang CXX=clang++ python3 tests/test_homing_sequences.py` from the repo
root. This builds only a standalone sanitized test binary in a temporary dir.
It compiles production virtual-axis, sequencer, encoder, monitor and trapezoidal
trajectory code, reusing the existing offline infrastructure adapters.

An ideal velocity follower integrates physical position independently of
engineering-coordinate reference changes. Integer encoder counts and geometric
end/home switches feed the production code. Expected edge/midpoint and absolute
phase references are computed from the fixture geometry. Success cases cover
1–10, 14, 15 and absolute 21/22. Abort probes cover initial/reverse/cam-crossing
STOP, disable, missing cam, idle without replay, fresh retry and lag-monitor
restoration. This does not build an IOC or test EPICS motor-record callbacks,
real drive electronics, CA timing, physical PID dynamics or latch handshakes.
