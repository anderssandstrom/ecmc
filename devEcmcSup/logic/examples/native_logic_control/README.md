# Native Logic Control

This example shows how to use the native control and utility helpers:

- [`ecmcNativeControl.hpp`](../../ecmcNativeControl.hpp)
- [`ecmcNativeUtils.hpp`](../../ecmcNativeUtils.hpp)

The example uses:

- `ecmcNative::Pid` to turn a position error into a velocity command
- `ecmcNative::RateLimiter` to ramp the velocity setpoint
- `ecmcNative::HysteresisBool` for a simple in-position window

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
