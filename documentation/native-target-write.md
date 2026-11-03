# Native motion on target-position writes

Each axis defaults to the existing staged behavior: writing a target does not
start motion; execute must be triggered separately.

Opt in for an individual axis:

```text
Cfg.SetAxisAutoExec(1,1)
GetAxisAutoExec(1)
```

The longer `SetAxisAutoExecuteOnTargetPos` / `GetAxisAutoExecuteOnTargetPos`
spellings remain accepted aliases. Use the short forms with MCU-Cmd.AOUT:
that record field holds 40 bytes, including the string terminator, independently
of the record's larger OMAX buffer.

With the setting enabled, writing `ax1.targpos` (the `-TgtPosCmd` PV),
`Main.M1.fPosition`, or calling `setAxisTargetPos(1,value)` starts a position
move. The selected command must be ABS (3) or REL (2). Motion uses the configured
velocity, acceleration and deceleration and the existing position-move functions
with cmdData 0. Each write triggers, including a repeated identical REL distance.
A write during an active move of the same type retargets that move using its
existing ABS/REL semantics. Requests are not queued during homing, velocity motion,
pending auto-enable, or coordinated start preparation.

Configure the command and motion parameters before enabling this mode. Merely
enabling the setting does not execute an already staged target. Set it back to 0
to restore staged execution; changing the setting does not stop an active move.
Native STOP remains the way to stop motion.

Normal axis auto-enable is supported through the existing execute/enable state
machine and timeout. For example:

```text
Cfg.SetAxisAutoEnableTimeout(1,5)
Cfg.SetAxisEnableAutoEnable(1,1)
```

Motion waits for the normal stable drive acknowledgement and group start
permission. Startup holds, blocked communication, master/slave ownership,
axis errors, external trajectory sources and non-finite targets reject the
request. With auto-enable disabled, a disabled axis rejects target-triggered
motion. Internal sequencer/PLC setpoint updates and motor-record VAL handling
are unchanged. There is no new RT thread, allocation or per-cycle motion trigger.

## Readback and status layout

The original 32-bit `axN.status` retains all flag positions 0–23. Bit 24 now
reports `autoExecuteOnTargetPos`, and bits 25–31 are reserved. The new read-only
32-bit `axN.status2` contains:

| Bits | Meaning |
| --- | --- |
| 0–7 | Low byte of sequence state |
| 8–15 | Last interlock |
| 16–31 | Reserved |

Internal sequence states remain unchanged. State 1003, used for post-homing
motion, appears as 235 (`1003 & 0xff`) in the packed readback. The new layout
replaces the former three-bit sequence and five-bit interlock fields in status
bits 24–31. This is a packed-status compatibility change: update ECMC and
ecmccfg together, and update/rebuild plugins that decode the old packed word or
use the internal axis-status structure. The public plugin-client status structure
keeps its existing field names; its implementation reads the new word.

ecmccfg exposes `-AutoExecuteOnTargetPos-RB` from status bit 24 and `-StatExt`
from status2. Existing `-SeqState` and `-LastIlock` PV names decode the new word.
The main and native axis panels show a compact amber **NO EXEC** badge only
when target-write mode is enabled. The expert panel shows **NO EXEC needed**.
The tooltip explains that a target write starts motion. The read-only badges
leave the problem indicator visible and preserve existing panel dimensions.
One additional asyn parameter per axis is used for status2.

## Tests and installation

Standalone dispatch/layout/auto-enable checks (no IOC):

```bash
python3 tests/test_native_target_write.py
```

Rebuild/install ECMC, reinstall ecmccfg records/panels, and restart the dedicated
SIM IOC before running the live group. This group tests both PV and text target
writes, ABS/REL, repeated REL writes, retargeting, auto-enable and the panel mode
readback in open and closed loop:

```bash
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run --case target_write
```

Live tests have not been executed by the offline checks.
