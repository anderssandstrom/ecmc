# Logic Examples

This directory contains small focused examples for the additive native-logic
interface in `ecmc`.

Current examples:

- [`native_logic_minimal`](./native_logic_minimal/): minimal C++ example using [`ecmcNativeLogic.h`](../ecmcNativeLogic.h) and [`ecmcNativeLogic.hpp`](../ecmcNativeLogic.hpp)
- [`native_logic_control`](./native_logic_control/): C++ example using [`ecmcNativeControl.hpp`](../ecmcNativeControl.hpp) and [`ecmcNativeUtils.hpp`](../ecmcNativeUtils.hpp)
- [`native_logic_motion`](./native_logic_motion/): C++ example using [`ecmcNativeMotion.hpp`](../ecmcNativeMotion.hpp) with `MC_Power`, `MC_MoveAbsolute`, and `MC_ReadStatus` style wrappers
- [`native_logic_scope`](./native_logic_scope/): EL3702/EL1252-style scope example using oversampling memmaps, trigger timestamps, and a digital trigger

The native-logic helper also supports explicit buffer bindings for arrays and
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

That form lets the native-logic loader resize the vector before realtime
starts, based on the bound `ecmc` item byte size.

Native logic can now be loaded directly from `ecmc` with:

```iocsh
ecmcConfigOrDie "Cfg.LoadNativeLogic(0,/path/to/native_logic.so)"
ecmcConfigOrDie "Cfg.LoadNativeLogic(0,/path/to/native_logic.so,asyn_port=NATIVE.LOGIC0;sample_rate_ms=2)"
```

For IOC startup scripts, the recommended wrapper is:

```iocsh
iocshLoad("$(ecmccfg_DIR)loadNativeLogic.cmd",
          "LOGIC_ID=0,FILE=/path/to/native_logic.so,ASYN_PORT=NATIVE.LOGIC0")
```

The built-in native-logic control/status PVs load by default through that
wrapper. Custom `epics.*` substitutions are optional and can be enabled with
`LOAD_APP_PVS=1,EPICS_SUBST=...`.

Each loaded native logic instance gets:

- its own dedicated asyn port
- built-in control, timing, and debug PVs on that port
- all user-defined `epics.*` exports on that same port

That wrapper is intended to load the built-in core substitutions from:

- `../ecmccfg/db/generic/ecmcNativeLogicCore.substitutions`

For EPICS exports declared through the native `epics` builder, substitutions can
be generated offline from a compiled native logic module with:

```sh
python3 tools/ecmcNativeLogicSubstGen.py \
  --logic-lib path/to/native_logic.so \
  --output native_logic.subs
```

Load both:

- `../ecmccfg/db/generic/ecmcNativeLogicCore.substitutions`
- the generated custom substitutions file for the user exports
