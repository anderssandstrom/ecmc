# Native Logic Scope

This example shows one way to use the additive native-logic interface as a
small scope/logger:

- two EL3702-style oversampling memmaps are read through `ecmc.inputAutoArray(...)`
- one EL1252-style digital input level is monitored and the positive-edge timestamp is used as the trigger event
- the EL3702 oversampling timestamp and EL1252 trigger timestamp are also read
- incoming analog data is appended continuously into a ring buffer
- on each trigger event, the EL1252 timestamp is aligned against the EL3702 `nextSyncTime`
  marker to estimate the trigger sample offset inside the buffered data
- once enough post-trigger samples are available, a trigger-aligned scope window is copied
  into fixed-capacity EPICS arrays

Main source:

- [`main.cpp`](./main.cpp)

Notes:

- The exact item names depend on your final `ecmccfg` / IOC configuration.
- The item names in this example match the current `ecmccfg` hardware scripts:
  - `ec0.s20.mm.analogInputArray01`
  - `ec0.s20.mm.analogInputArray02`
  - `ec0.s20.nextSyncTime`
  - `ec0.s21.binaryInput01`
  - `ec0.s21.timestampLatchPositive01`
- The EL3702 `nextSyncTime` value marks the start of the next oversampling point.
  The `ecmccfg` DC test notes that this `nextSyncTime` has already passed when the previous scan arrives in `ecmc`, so the example keeps it as a raw marker instead of treating it as the current-frame timestamp.
- The EL1252 `timestampLatchPositive01` value is used as the actual trigger-edge event, matching the existing `ecmccfg/examples/test/scope/plc/scope.plc` logic.
- The result arrays are fixed-capacity because EPICS waveforms cannot be resized at runtime.
  The example therefore exports:
  - `scope.pre_trigger_samples` as a writable number of requested pre-trigger samples
  - `scope.post_trigger_samples` as a writable number of requested post-trigger samples
  - `scope.active_samples` as the valid length currently stored in the waveform arrays
  - `scope.max_samples` as the fixed waveform capacity
- The trigger is placed at `scope.trigger_index`, which follows the configured
  `scope.pre_trigger_samples`.
- Only the first `scope.active_samples` values of `scope.ch1_scope` and `scope.ch2_scope`
  are valid after a capture.

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
