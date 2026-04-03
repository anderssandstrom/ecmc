# Native Logic Minimal

This example shows the simplest intended C++ usage of the additive native-logic
interface in `ecmc`.

The example uses:

- `ecmcNative::LogicBase`
- `ecmc` for live `ecmc` item bindings
- `epics` for EPICS-facing exported values
- `ECMC_NATIVE_LOGIC_REGISTER_DEFAULT(...)`

Main source:

- [`main.cpp`](./main.cpp)

The example is intentionally header-only on the `ecmc` side. A future loader can
compile this file into a shared library and query:

```cpp
ecmc_native_logic_get_api()
```

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
