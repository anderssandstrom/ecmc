# Cpp Logic Minimal

This example shows the simplest intended C++ usage of the additive C++ logic
interface in `ecmc`.

The example uses:

- `ecmcCpp::LogicBase`
- `ecmc` for live `ecmc` item bindings
- `epics` for EPICS-facing exported values
- `ECMC_CPP_LOGIC_REGISTER_DEFAULT(...)`

Main source:

- [`main.cpp`](./main.cpp)

The example is intentionally small on the `ecmc` side. Compile it into a shared
library and load it either through `Cfg.LoadCppLogic(...)` directly or,
preferably in IOC startup, through:

```iocsh
iocshLoad("$(ecmccfg_DIR)loadCppLogic.cmd",
          "LOGIC_ID=0,FILE=/path/to/cpp_logic.so,ASYN_PORT=CPP.LOGIC0")
```

`ecmc` queries:

```cpp
ecmc_cpp_logic_get_api()
```

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
