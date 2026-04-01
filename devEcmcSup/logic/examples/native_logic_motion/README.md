# Native Logic Motion

This example shows how the additive native-logic interface can reuse the
existing `ecmcMcApi` PLCopen-style motion backend through the C++ wrapper in
[`ecmcNativeMotion.hpp`](../../ecmcNativeMotion.hpp).

It uses:

- `ecmcNative::LogicBase`
- `ecmcNative::McPower`
- `ecmcNative::McMoveAbsolute`
- `ecmcNative::McReadStatus`
- `ecmcNative::McReadActualPosition`

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
