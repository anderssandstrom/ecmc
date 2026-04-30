# Logic Examples

This directory contains small focused examples for the additive C++ logic
interface in `ecmc`.

Current examples:

- [`cpp_logic_starter`](./cpp_logic_starter/): smallest practical starting point for a new C++ logic shared library
- [`cpp_logic_minimal`](./cpp_logic_minimal/): minimal C++ example using [`ecmcCppLogic.h`](../ecmcCppLogic.h) and [`ecmcCppLogic.hpp`](../ecmcCppLogic.hpp)
- [`cpp_logic_control`](./cpp_logic_control/): C++ example using [`ecmcCppControl.hpp`](../ecmcCppControl.hpp) and [`ecmcCppUtils.hpp`](../ecmcCppUtils.hpp)
- [`cpp_logic_arrays`](./cpp_logic_arrays/): array and byte-buffer example using `inputAutoArray(...)`, `outputArray(...)`, and `readOnlyArray(...)`
- [`cpp_logic_motion`](./cpp_logic_motion/): C++ example using [`ecmcCppMotion.hpp`](../ecmcCppMotion.hpp) with `MC_*` style wrappers
- [`cpp_logic_scope`](./cpp_logic_scope/): EL3702/EL1252-style scope example using oversampling memmaps, trigger timestamps, and a digital trigger
- [`cpp_logic_trace`](./cpp_logic_trace/): reusable triggered trace example using [`ecmcCppTrace.hpp`](../ecmcCppTrace.hpp)
- [`cpp_logic_retained`](./cpp_logic_retained/): retained parameter example using [`ecmcCppPersist.hpp`](../ecmcCppPersist.hpp)

The utility header also includes IEC-style timing/edge helpers and other small
building blocks such as:

- `ecmcCpp::RTrig`, `ecmcCpp::FTrig`
- `ecmcCpp::Ton`, `ecmcCpp::Tof`, `ecmcCpp::Tp`
- `ecmcCpp::Sr`, `ecmcCpp::Rs`, `ecmcCpp::FlipFlop`
- `ecmcCpp::Blink`
- `ecmcCpp::StateTimer<T>`
- `ecmcCpp::readBit`, `ecmcCpp::writeBit`, `ecmcCpp::setBit`, `ecmcCpp::clearBit`, `ecmcCpp::toggleBit`
- `ecmcCpp::MoveAverage`
- `ecmcCpp::MinMaxHold`

Additional helper headers include:

- [`ecmcCppTrace.hpp`](../ecmcCppTrace.hpp)
- [`ecmcCppPersist.hpp`](../ecmcCppPersist.hpp)

The cpp_logic helper also supports explicit buffer bindings for arrays and
raw bytes, for example:

```cpp
std::vector<double> trajectory(256);
std::array<uint8_t, 512> memmap {};
uint8_t status_blob[64] {};

ecmc.inputArray("ds0.data", trajectory)
    .outputBytes("ec0.s5.mm.CH1_ARRAY", memmap, sizeof(memmap));

epics.readOnlyArray("diag.trajectory", trajectory)
     .writableBytes("diag.status_blob", status_blob, sizeof(status_blob));
```

For raw pointers or partial buffers, keep using the explicit `...Bytes(...)`
or pointer-plus-count forms.

For `std::vector<T>`, there is also an auto-sized form intended for startup
preparation once the resolved `ecmc` item size is known:

```cpp
std::vector<double> trajectory;

ecmc.inputAutoArray("ds0.data", trajectory);
```

That form lets the C++ logic loader resize the vector before realtime
starts, based on the bound `ecmc` item byte size.

C++ logic modules can now be loaded directly from `ecmc` with:

```iocsh
ecmcConfigOrDie "Cfg.LoadCppLogic(0,/path/to/cpp_logic.so)"
ecmcConfigOrDie "Cfg.LoadCppLogic(0,/path/to/cpp_logic.so,asyn_port=CPP.LOGIC0;sample_rate_ms=2;update_rate_ms=20)"
```

For IOC startup scripts, the recommended wrapper is:

```iocsh
iocshLoad("$(ecmccfg_DIR)loadCppLogic.cmd",
          "LOGIC_ID=0,FILE=/path/to/cpp_logic.so,ASYN_PORT=CPP.LOGIC0")
```

The built-in C++ logic control/status PVs load by default through that
wrapper. Custom `epics.*` substitutions are optional and can be enabled with
`LOAD_APP_PVS=1,EPICS_SUBST=...`.

Each loaded C++ logic instance gets:

- its own dedicated asyn port
- built-in control, timing, and debug PVs on that port
- all user-defined `epics.*` exports on that same port

That wrapper is intended to load the built-in core substitutions from:

- `../ecmccfg/db/generic/ecmcCppLogicCore.substitutions`

For EPICS exports declared through the native `epics` builder, substitutions can
be generated offline from a compiled C++ logic module with:

```sh
python3 tools/ecmcCppLogicSubstGen.py \
  --logic-lib path/to/cpp_logic.so \
  --output cpp_logic.subs
```

Load both:

- `../ecmccfg/db/generic/ecmcCppLogicCore.substitutions`
- the generated custom substitutions file for the user exports

For a compact helper reference, see:

- [`../CPP_LOGIC_HELPERS.md`](../CPP_LOGIC_HELPERS.md)
