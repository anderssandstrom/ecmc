# EL7062 and EL5042 DC timing test

## Purpose

Collect the effective SyncManager timing reported by an EL7062 drive terminal
and an EL5042 encoder terminal after EtherLab has applied the configured slave
settings. The first implementation reports raw values and deliberately does not
assume that shift, calculation/copy, and delay times are additive.

## Setup

1. Configure both slaves normally, including their `Cfg.EcSlaveConfigDC(...)`
   calls and PDO/SDO configuration.
2. Start ecmc runtime and wait until the slaves are operational. The timing SDO
   requests execute serially after master activation, so they see the applied
   slave configuration without creating a burst of mailbox requests.
3. Request the report for each master slave index. Repeat it after a few seconds
   if either timing object says `(pending)`:

   ```text
   EcPrintSlaveConfig(<EL7062 slave index>)
   EcPrintSlaveConfig(<EL5042 slave index>)
   ```

4. Save the complete output along with:

   - ecmc cycle period
   - terminal product and revision numbers
   - DC `assignActivate`, SYNC0/SYNC1 cycle, and shift configuration
   - domain execution rate and offset
   - whether both slaves were operational and domains had complete working
     counters

## Expected report

For each DC-configured slave, the report includes:

```text
0x1C32:01  output synchronization type
0x1C32:02  output cycle time
0x1C32:03  output shift time
0x1C32:04  output synchronization types supported
0x1C32:05  output minimum cycle time
0x1C32:06  output calculation/copy time
0x1C32:07  output minimum delay time
0x1C32:08  output timing command
0x1C32:09  output maximum delay time
0x1C32:20  output synchronization error sample

0x1C33:01  input synchronization type
0x1C33:02  input cycle time
0x1C33:03  input shift time
0x1C33:04  input synchronization types supported
0x1C33:05  input minimum cycle time
0x1C33:06  input calculation/copy time
0x1C33:07  input minimum delay time
0x1C33:08  input timing command
0x1C33:09  input maximum delay time
0x1C33:20  input synchronization error sample
```

An unavailable subindex is reported separately and is not a startup error.
`synchronizationError` is currently a one-time discovery sample; continuous
validity monitoring will be added separately.

## ESI baseline

The local Beckhoff ESI files provide the following configuration baseline. ESI
values are defaults/capabilities, not substitutes for the post-activation SDO
readback.

- EL5042 (`0x13b23052`, revision `0x00100000` or later): DC mode uses
  `AssignActivate=0x300`, application-period SYNC0, zero SYNC0 shift, and an
  application-period SYNC1 parameter. Its input timing object defaults to sync
  mode `0x22`, cycle time 1,000,000 ns, supported modes mask `0x0807`, and
  minimum cycle time 100,000 ns. The ESI entry exposes `0x1C33`; an output
  timing object is not expected for this input-only terminal.
- EL7062 (`0x1b963052`) and ED7062 (`0x1b961052`), revision `0x00100000` or
  later: DC mode uses `AssignActivate=0x700`, SYNC0 period 62,500 ns, zero
  SYNC0 shift, and SYNC1 at application cycle minus 62,500 ns. The timing
  objects default to output sync mode `1`, input sync mode `3`, application
  cycle time 1,000,000 ns, supported modes mask `0x080a`, and minimum cycle
  time 125,000 ns.

The current ecmccfg `EL5042_DC` and common `EX7062_CSP` configurations match
these ESI DC schedules. Device timing-profile metadata should only be added
after readback confirms the effective values on the installed revisions.

## Analysis

Compare the returned cycle times with the ecmc and configured DC cycle periods.
Interpret the acquisition/application phase using the terminal revision's
documentation and its reported synchronization type. Do not calculate
`shift + calculation/copy + delay` unless the device timing definition confirms
that those values describe independent consecutive intervals.

The findings should identify:

- which timing objects/subindices each terminal implements;
- the EL5042 encoder acquisition event and its relationship to SYNC0/SYNC1;
- the EL7062 setpoint application event and its relationship to SYNC0/SYNC1;
- the PDO cycle association for each terminal;
- a nominal encoder-acquisition-to-drive-application interval;
- uncertainty and any remaining device-specific assumptions.

## Generic touch-probe first step

Touch-probe control is exposed on the axis, while the selected encoder owns
the hardware links. The core is terminal-independent: a timestamp is optional,
and any terminal can be used when it supplies control, new-value status, and a
latched position.

Load `HW_DESC=ED7062_TPDC`; the existing ED7062 and EL7062 hardware
descriptors remain unchanged. For ED7062 channel 1, positive-edge touch probe
1 can then be configured in the encoder YAML as follows (replace `ENC_SID` as
usual):

```yaml
encoder:
  position: ec0.s$(ENC_SID).positionActual01
  control: ec0.s$(ENC_SID).touchProbeControl01
  status: ec0.s$(ENC_SID).touchProbeStatus01
  latch:
    position: ec0.s$(ENC_SID).touchProbePositionPos01_1
    timestamp: ec0.s$(ENC_SID).touchProbeTimestampPos01_1
    control: 0
    status: 1
    armCmd: 17
    armBits: 5
```

`positionActual01` is the terminal's primary feedback/open-loop position. The
touch-probe position source is selected separately through the terminal's SDO
setup and is deliberately kept outside both the generic ecmc core and the
`ED7062_TPDC` hardware mapping.

### ED7062 periodic-position timing result

The touch-probe timestamp remains absolute DC time. Comparison with the
hardware-latched open-loop counter showed that the normal position PDO is best
modeled from its ecmc receive-cycle visibility:

```text
effective position sample = receive time - 1 cycle - 45 us + SYNC_0_SHIFT
```

With a 1 ms EtherCAT cycle and motion near +/-5 engineering units/s, 15
positive-edge captures gave a mean reconstruction error of -1.18 us, a 4.02 us
standard deviation, and a range of -10.37 us to +4.17 us. The terminal adapter
therefore uses a cycle-only input reference with `cycleOffset=-1`, an
`eventOffsetNs` of `-45000 + SYNC_0_SHIFT`, and retains the reported 31.25 us
uncertainty bound. The metadata is installed by the shared EX7062 CSV and CSP
scripts, covering both ED7062 and EL7062 wrappers; the TPDC script carries the
same input metadata. CSP output timing remains separate and is not inferred
from this input measurement. This relationship was confirmed by moving `SYNC_0_SHIFT`
from 0 to -20 us: the uncorrected reconstruction moved by approximately the
same amount. A +20 us test crossed the PDO update boundary and alternated
between repeated samples and two-cycle position steps, so timing compensation
does not make every DC phase operationally safe.

Arm and inspect encoder 1 of axis 1 with:

```text
AxisTouchProbeArm(1,1,1)
AxisPrintTouchProbe(1,1)
AxisTouchProbeArm(1,1,0)
```

The printed result contains one sequence number, engineering-unit position,
raw timestamp, extended 64-bit DC event time, quality, and uncertainty. A
32-bit timestamp is extended around the current master DC time. If no timestamp
entry is linked, position capture still works and uncertainty is one application
cycle.

For validation, run at constant positive and negative velocities and trigger
with an ordinary digital output at varying phases. Compare the terminal's
latched position with a later timestamp-based position estimate. Convert the
position residual to timing error using `time error = position error / velocity`.
This tests input timestamp/interpolation accuracy independently of output
scheduling; EL2252 validation can follow when timestamped output hardware is
available.
