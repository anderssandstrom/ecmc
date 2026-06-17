# EtherCAT DC Delay Measurement Notes

This note summarizes the pre-runtime EtherCAT frame delay measurements used to
estimate an ECMC application-time offset for Distributed Clocks (DC).

## Goal

The goal is to make the host time supplied to IgH:

```cpp
ecrt_master_application_time(master, host_time_ns + offset_ns)
```

match the effective DC time at the slaves as well as possible during normal
operation.

ECMC writes the application time cyclically and the DC clocks are steered
slowly toward that value. For this reason, the typical host-visible timing is
more relevant than the absolute fastest observed frame. The fastest frame is
useful as a lower-bound diagnostic, but the elapsed-time median, mode, and
average are better estimates of the steady-state zero point.

## Measurement Method

The pre-runtime test runs after EtherCAT master activation and before the ECMC
realtime thread starts. For each sample, it:

1. Queues the EtherCAT domains.
2. Timestamps just before `ecrt_master_send()`.
3. Sends the frame.
4. Waits for a configured delay.
5. Timestamps just before `ecrt_master_receive()`.
6. Receives and processes domains.
7. Accepts the sample only when the domain working-counter state is complete.

The test reports:

- `min`: fastest observed host-visible send-to-receive elapsed time.
- `min_repeat`: lowest time bucket that occurred more than once.
- `mode`: elapsed-time bucket with the most successful samples.
- `elapsed_p50`: 50th percentile of successful measured elapsed times.
- `elapsed_p90`: 90th percentile of successful measured elapsed times.
- `elapsed_p99`: 99th percentile of successful measured elapsed times.
- `avg`: average host-visible send-to-receive elapsed time.
- `max`: largest observed elapsed time, mostly useful for jitter/outliers.
- `dc_first_to_last`: physical first-to-last slave DC topology delay, read from ESC register `0x0928`.

The DC topology delay is read from the physical first and physical last scanned
slaves, not from the first/last configured ECMC slaves.

### Configuration Command

Enable the pre-runtime measurement before switching ECMC to runtime:

```iocsh
Cfg.SetEcFrameDelayTest(enable,samples,startDelayNs,stepDelayNs,maxDelayNs)
Cfg.SetAppMode(1)
```

Example used for most measurements in this note:

```iocsh
Cfg.SetEcFrameDelayTest(1,100000,1000,1000,100000)
Cfg.SetAppMode(1)
```

This means:

```text
enable       = 1
samples      = 100000
startDelayNs = 1000
stepDelayNs  = 1000
maxDelayNs   = 100000
```

Disable the test with:

```iocsh
Cfg.SetEcFrameDelayTest(0,1,1,1,1)
```

## Offset Estimate

A simple model is that half of the measured host-visible elapsed time represents
the path from the host timestamp point to the physical last slave:

```text
elapsed_total / 2 ~= host_to_first_slave + first_to_last_topology
```

The physical first-to-last topology delay is read from the DC delay registers as
`dc_first_to_last_ns`. This value is already a one-way first-to-last delay, so it
is not divided by two again.

The preferred calibration estimate should be based on an elapsed-time percentile
or on the elapsed-time mode:

```text
offset_p50_ns = (elapsed_p50_ns / 2) - dc_first_to_last_ns
offset_p90_ns = (elapsed_p90_ns / 2) - dc_first_to_last_ns
offset_p99_ns = (elapsed_p99_ns / 2) - dc_first_to_last_ns
offset_mode_ns = (mode_ns / 2) - dc_first_to_last_ns
```

Use `p50` or `mode` for a typical operational value, and `p90`/`p99` for more
conservative settings. The average-based estimate is still useful as a
comparison, but is more sensitive to long-latency outliers:

```text
offset_avg_ns = (avg_total_ns / 2) - dc_first_to_last_ns
```

This is an operational estimate, not a pure physical propagation measurement.
The individual send and receive host/NIC path delays are not measured
separately.

For lower-bound diagnostics:

```text
offset_min_repeat_ns = (min_repeat_ns / 2) - dc_first_to_last_ns
```

Earlier test versions also tried to report requested-delay thresholds. Those are
not reliable as calibration values on this setup: a requested 1 us sleep can
wake much later, for example around the 60 us host-visible cluster. The logged
elapsed percentiles therefore use the measured send-to-receive time, not the
requested sleep value.

At the time of writing, this branch adds the measurement command above, but it
does not yet add a `Cfg.` command that applies `offset_ns` to the normal cyclic
`ecrt_master_application_time()` call. Applying the offset still requires an
ECMC code/config hook in the normal EtherCAT send path.

## Observed Results

The historical runs below were captured before elapsed percentiles were added to
the log. Their listed calibration values are therefore average-based estimates.
For new runs, compare these values:

```text
offset_p50_ns = (elapsed_p50_ns / 2) - dc_first_to_last_ns
offset_mode_ns = (mode_ns / 2) - dc_first_to_last_ns
offset_avg_ns = (avg_ns / 2) - dc_first_to_last_ns
```

The calculated offset is not itself the measured bus time. If two topologies
have nearly the same host-visible elapsed time, the shorter topology will give a
larger calculated offset because less physical first-to-last DC delay is
subtracted:

```text
offset = elapsed / 2 - dc_first_to_last
```

### Short Topology

Physical topology: 23 slaves. In one run, the last configured slave was not the
physical last slave.

| Run | Config note | `dc_first_to_last` ns | `min` ns | `min_repeat` ns | `avg` ns | `max` ns | `last_wc` | `offset_avg = avg / 2 - dc` ns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | short topology | 5129 | 25065 | 25065 | 60671 | 141428 | 4 | 25207 |
| 2 | short topology | 5129 | 25570 | 25570 | 60815 | 544454 | 4 | 25279 |
| 3 | short topology | 5129 | 25188 | 25188 | 60794 | 171586 | 4 | 25268 |
| 4 | 23 slaves, last configured 12 | 5114 | 25783 | 25783 | 60808 | 176992 | 4 | 25290 |
| 5 | short topology, elapsed percentiles | 5123 | 25704 | 25704 | 60859 | 1296755 | 4 | 25306 |

Average of the short-topology `offset_avg` values:

```text
(25207 + 25279 + 25268 + 25290 + 25306) / 5 = 25270 ns
```

Recommended starting offset for this topology:

```text
+25300 ns
```

Example calculation from run 5:

```text
offset_avg_ns = (60859 / 2) - 5123 = 25306 ns
offset_p50_ns = (60000 / 2) - 5123 = 24877 ns
offset_mode_ns = (60000 / 2) - 5123 = 24877 ns
offset_p90_ns = (61000 / 2) - 5123 = 25377 ns
```

If an application-time offset config command is added, this topology should be
configured with approximately:

```iocsh
# Example command name; this command is not present yet.
Cfg.SetEcApplicationTimeOffsetNs(25300)
```

### Longer Topology

Physical topology with more slaves. The physical first-to-last DC delay
increased to about 12.1 us.

| Run | Config note | `dc_first_to_last` ns | `min` ns | `min_repeat` ns | `avg` ns | `max` ns | `last_wc` | `offset_avg = avg / 2 - dc` ns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | more slaves, not same configured set | 12053 | 32748 | 32748 | 60819 | 89698 | 4 | 18357 |
| 2 | added one configured slave later in chain | 12111 | 34361 | 38582 | 60897 | 128738 | 7 | 18338 |
| 3 | configured physical last slave 43 | 12111 | 36341 | 36341 | 60920 | 95494 | 5 | 18349 |
| 4 | long topology, elapsed percentiles | 12025 | 36520 | 36520 | 60770 | 139074 | 5 | 18360 |
| 5 | long topology, elapsed percentiles | 12093 | 36205 | 36205 | 60829 | 123616 | 5 | 18321 |
| 6 | long topology, elapsed percentiles | 12093 | 31911 | 32190 | 60777 | 124921 | 4 | 18295 |

Average of the longer-topology `offset_avg` values:

```text
(18357 + 18338 + 18349 + 18360 + 18321 + 18295) / 6 = 18337 ns
```

Recommended starting offset for this topology:

```text
+18300 ns to +18400 ns
```

Example calculation from run 6:

```text
offset_avg_ns = (60777 / 2) - 12093 = 18295 ns
offset_p50_ns = (60000 / 2) - 12093 = 17907 ns
offset_mode_ns = (60000 / 2) - 12093 = 17907 ns
offset_p90_ns = (61000 / 2) - 12093 = 18407 ns
```

If an application-time offset config command is added, this topology should be
configured with approximately:

```iocsh
# Example command name; this command is not present yet.
Cfg.SetEcApplicationTimeOffsetNs(18300)
```

## Interpretation

The physical DC topology delay was stable for each topology:

```text
short topology:  ~5.1 us
longer topology: ~12.1 us
```

The host-visible average remained close to:

```text
~60.7 us to ~60.9 us
```

This suggests that the typical elapsed time is dominated by host/NIC/IgH timing,
while ESC register `0x0928` correctly tracks the physical topology delay.

This also explains why the shorter bus can produce a larger calculated offset:
the measured elapsed time is almost unchanged, but the physical topology delay
being subtracted is smaller.

Example with recent measurements:

```text
short bus elapsed/2: 60859 / 2 = 30429 ns
long bus elapsed/2:  60777 / 2 = 30388 ns

short bus offset_avg: 30429 - 5123  = 25306 ns
long bus offset_avg:  30388 - 12093 = 18295 ns

offset difference: 25306 - 18295 = 7011 ns
dc delay difference: 12093 - 5123 = 6970 ns
```

So the shorter bus is not measured as slower. The host-visible elapsed time is
nearly the same; the offset changes because the formula subtracts the physical
first-to-last DC topology delay.

The `min` and `min_repeat` values changed more with configured-domain content
and working-counter behavior. They are useful lower-bound diagnostics, but less
appropriate for the DC steady-state zero point than the elapsed median, mode, or
average.

## Practical Recommendation

For new measurements, use an elapsed-percentile or mode-based offset as the
first operational compensation value:

```text
calibration_offset_p50_ns = (elapsed_p50_ns / 2) - dc_first_to_last_ns
calibration_offset_p90_ns = (elapsed_p90_ns / 2) - dc_first_to_last_ns
calibration_offset_p99_ns = (elapsed_p99_ns / 2) - dc_first_to_last_ns
calibration_offset_mode_ns = (mode_ns / 2) - dc_first_to_last_ns
```

Use `p50` or `mode` for a typical value and `p90`/`p99` for more conservative
values. Use the average-based value as a comparison:

```text
calibration_offset_avg_ns = (avg_ns / 2) - dc_first_to_last_ns
```

Historical average-based starting values:

```text
short topology:  +25300 ns
longer topology: +18300 ns to +18400 ns
```

Suggested future startup pattern:

```iocsh
# Optional measurement run:
Cfg.SetEcFrameDelayTest(1,100000,1000,1000,100000)

# Future offset command, if implemented:
# Cfg.SetEcApplicationTimeOffsetNs(25300)   # short topology
# Cfg.SetEcApplicationTimeOffsetNs(18300)   # longer topology

Cfg.SetAppMode(1)
```
