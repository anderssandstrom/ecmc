# Cpp Logic Starter

This is the smallest practical checked-in example for a new `cpp_logic`
shared library.

It shows only:

- one input binding
- two output bindings
- one read-only `epics` export
- one writable `epics` export
- the `ECMC_CPP_LOGIC_REGISTER_DEFAULT(...)` registration macro

Main source:

- [`main.cpp`](./main.cpp)

Minimal syntax-only check:

```sh
c++ -std=c++17 -fsyntax-only main.cpp -I../..
```
