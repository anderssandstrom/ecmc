# Cpp Logic Retained

This example shows the retained-value helper in:

- [`ecmcCppPersist.hpp`](../../ecmcCppPersist.hpp)

It demonstrates:

- one retained `double` value
- automatic restore on startup
- explicit load/save trigger PVs

The helper stores a trivially-copyable value in a small binary file. This is
intended for infrequent actions such as startup restore or a manual save
request, not for continuous per-cycle file I/O.

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
