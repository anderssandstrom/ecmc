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

The example is intentionally small on the `ecmc` side. Compile it into a shared
library and load it either through `Cfg.LoadNativeLogic(...)` directly or,
preferably in IOC startup, through:

```iocsh
iocshLoad("$(ecmccfg_DIR)loadNativeLogic.cmd",
          "LOGIC_ID=0,FILE=/path/to/native_logic.so,ASYN_PORT=NATIVE.LOGIC0")
```

`ecmc` queries:

```cpp
ecmc_native_logic_get_api()
```

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
