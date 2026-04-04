# Cpp Logic Arrays

This example focuses on the array and byte-buffer helper API in:

- [`ecmcCppLogic.hpp`](../../ecmcCppLogic.hpp)
- [`ecmcCppUtils.hpp`](../../ecmcCppUtils.hpp)

It demonstrates:

- `ecmc.inputAutoArray(...)` with a startup-sized `std::vector<double>`
- `ecmc.outputArray(...)` with a fixed-size `std::array<int16_t, N>`
- `ecmc.outputBytes(...)` with a raw byte/status buffer
- `epics.readOnlyArray(...)` for a published preview waveform
- `epics.readOnlyBytes(...)` for a published status blob

The example also uses:

- `ecmcCpp::MoveAverage`
- `ecmcCpp::MinMaxHold`

The item names in the example are intentionally generic and should be adapted
to the actual data-storage or memmap names in a real IOC.

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
