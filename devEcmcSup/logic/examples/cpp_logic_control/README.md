# Cpp Logic Control

This example shows how to use the C++ logic control and utility helpers:

- [`ecmcCppControl.hpp`](../../ecmcCppControl.hpp)
- [`ecmcCppUtils.hpp`](../../ecmcCppUtils.hpp)

The example uses:

- `ecmcCpp::Pid` to turn a position error into a velocity command
- `ecmcCpp::RateLimiter` to ramp the velocity setpoint
- `ecmcCpp::HysteresisBool` for a simple in-position window
- `ecmcCpp::Ton` to delay drive enable
- `ecmcCpp::RTrig` to toggle the setpoint on an in-position edge

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
