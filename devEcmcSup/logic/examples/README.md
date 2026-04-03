# Logic Examples

This directory contains small focused examples for the additive native-logic
interface in `ecmc`.

Current examples:

- [`native_logic_minimal`](./native_logic_minimal/): minimal C++ example using [`ecmcNativeLogic.h`](../ecmcNativeLogic.h) and [`ecmcNativeLogic.hpp`](../ecmcNativeLogic.hpp)
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

That form is meant to let a future loader resize the vector before realtime
starts, based on the bound `ecmc` item byte size.
