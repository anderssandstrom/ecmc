# Cpp Logic Trace

This example shows the reusable triggered trace helper in:

- [`ecmcCppTrace.hpp`](../../ecmcCppTrace.hpp)

It demonstrates:

- one live sample input
- one digital trigger input
- pre-trigger and post-trigger sample settings
- a fixed-capacity exported waveform

The helper keeps a rolling history and, on a trigger rising edge, captures:

- `PreTriggerSamples` values before the trigger sample
- the trigger sample itself
- `PostTriggerSamples` values after the trigger sample

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
