# Cpp Logic Control

This example shows how to use the C++ logic control and utility helpers:

- [`ecmcCppControl.hpp`](../../ecmcCppControl.hpp)
- [`ecmcCppUtils.hpp`](../../ecmcCppUtils.hpp)

The example uses:

- `ecmcCpp::Pid` to turn a position error into a velocity command
- `ecmcCpp::RateLimiter` to ramp the velocity setpoint
- `ecmcCpp::HysteresisBool` for a simple in-position window

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
