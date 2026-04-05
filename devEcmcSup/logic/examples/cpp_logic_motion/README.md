# Cpp Logic Motion

This example shows how the additive C++ logic interface can reuse the
existing `ecmcMcApi` PLCopen-style motion backend through the C++ wrapper in
[`ecmcCppMotion.hpp`](../../ecmcCppMotion.hpp).

It uses:

- `ecmcCpp::LogicBase`
- `ecmcCpp::McPower`
- `ecmcCpp::McReset`
- `ecmcCpp::McMoveAbsolute`
- `ecmcCpp::McMoveRelative`
- `ecmcCpp::McHome`
- `ecmcCpp::McStop`
- `ecmcCpp::McReadStatus`
- `ecmcCpp::McReadActualPosition`
- `ecmcCpp::McReadActualVelocity`

Behavior:

- powers axis `1`
- commands alternating absolute moves between `0` and `12800`
- exports a few status variables through `pv`

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../.. -I../../../motion
```
