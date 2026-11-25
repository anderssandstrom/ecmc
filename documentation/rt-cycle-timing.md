# Measuring EtherCAT cycle phases

Use the local IOC shell to capture a finite set of consecutive cycles. Arm the
capture while running to sample settled operation, or before entering runtime
to sample the first cycles. At 1 kHz, 10,000 cycles take about 10 seconds.

```iocsh
ecmcRtTimingStart(10000)
# After the capture completes:
ecmcRtTimingWrite("/tmp/ecmc-rt-timing.csv")
```

One capture may be active at a time. The limit is 65,536 cycles. If runtime
stops during a capture, the completed rows can be exported. Starting another
capture replaces the previous result. The IOC allocates the buffer on arm and
writes the CSV from the IOC shell. Only an active capture adds clock reads.

Each CSV row contains monotonic timestamps in nanoseconds. Compare the
following differences between runs:

| Difference | What it measures |
| --- | --- |
| `wake_ns - scheduled_ns` | Delay returning from the absolute sleep |
| `after_asyn_ns - wake_ns` | Time through the asyn port lock, or zero if skipped |
| `after_motor_mutex_ns - after_asyn_ns` | Time through the motor-access mutex |
| `before_send_ns - after_motor_mutex_ns` | Work after acquiring the locks and before sending |
| `after_ethercat_ns - before_send_ns` | EtherCAT output path, including process-image updates, DC calls and the master send call |
| Consecutive `after_ethercat_ns` differences | Interval between returns from the EtherCAT output path |

`after_ethercat_ns` is zero when EtherCAT is inactive. It is a software
timestamp. For the actual time on the wire, use a packet capture with
hardware timestamps or an external EtherCAT measurement. Compare the same
workload and capture settings, since the additional clock reads have a cost.
