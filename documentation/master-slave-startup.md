# Master/slave trajectory startup

A new master trajectory from rest waits until the slave group reports all
drives enabled and all trajectory sources external. Enabled feedback includes
the configured brake opening sequence. The master remains busy at its starting
position, with zero velocity and acceleration, while the state machine reports
`PREP_MST` or `WAIT_EXT` through the existing `SMxx-Stat`/`SMxx-StatTxt` PVs.

Previously, enabling a virtual master could start its trajectory while the
state machine was still preparing physical slaves. The slaves missed the
initial velocity samples. In open loop this left a position offset and could
trip following-error monitoring on the master. Following-error thresholds and
delays are unchanged by this fix.

Entering MASTER also requires enabled feedback from the entire master group.
If the physical slaves are already enabled, a request to move one virtual
master must still enable its idle peers. Previously this path skipped master
preparation: for example, CENTERY was enabled while GAPY remained disabled.
The next MASTER cycle treated the partially enabled group as lost enable and
aborted with `ERROR_AXIS_SLAVED_AXIS_INTERLOCK` (`0x1432E`). The IDLE preparation
path now enables both groups and waits for both groups' feedback before the
transition. Direct physical moves retain their separate SLAVE path.

The state machine owns a start-permission flag and attaches it to the master
axes during configuration. Axis command handling keeps execute pending until
that flag allows startup. The sequencer has not started the trajectory while
waiting. Both direct execute requests on enabled axes and automatic enable
requests use this readiness check. `ecmcTrajectoryBase` contains no coordination
wait or start-permission pointer.

The state machine updates permission from its existing group status summary.
There is no additional thread, allocation, group scan, or I/O operation on the
normal RT path. The axis checks readiness only while an execute/enable request
is pending. This describes the code path, not a measured timing result.

STOP, disable, reset, axis errors, startup holds and command blocking cancel a
pending coordinated start. Ongoing trajectories and stop ramps continue through
their existing code paths. Existing preparation and external-source
acknowledgement timeouts remain active. Physical axes in the slave group do
not receive this start permission, so direct physical moves remain independent.
Disabling the state machine restores standalone master operation.

When an amplifier OFF command has already been issued, a new enable request
waits for the corresponding disabled feedback before starting a fresh enable
sequence. This handles rapid restart after brake closure: stale enabled
feedback from the previous request cannot open the brake, and its later fall
is treated as the expected OFF acknowledgement. The existing enable timeout
also bounds this wait if disabled feedback never arrives. Cancelling enable
abandons the wait. A real feedback loss during a fresh ON sequence still
latches `ERROR_AXIS_AMPLIFIER_ENABLED_LOST` (`0x14317`). This handshake applies
to stepper and DS402 drives, with or without the software brake sequence.

A slave interlock is still a fault. Before disabling the group, the fault path
logs each currently interlocked slave's axis ID, active interlock code, error,
enable feedback and trajectory source. This preserves information that the
post-cleanup PV snapshot may no longer contain. The lost-enable/error path
also logs both groups' enable-command, enabled-feedback and error summaries
before disabling them; a master error need not originate in a slave interlock.

Standalone regression tests compile the production axis execute/auto-enable
methods and state-machine methods with a real trapezoidal trajectory and
controlled sequencer/group/publication boundaries. Tests verify that the
sequencer has not received execute while readiness is missing, delayed starts
do not lose motion, and cancellation cannot replay a request. They do not
replace live IOC tests, PVT integration tests or hardware timing measurements.

After rebuilding/installing ECMC and restarting the dedicated slit test IOC,
run from the ECMC repository:

```sh
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run --case sync --loop open
python3 tools/simulation/slit_test/run.py --prefix SIMSLIT: --run
```
