# Cpp Logic DB Templates

This directory contains generic EPICS DB templates intended for the additive
`cpp_logic` C/C++ interface.

The canonical IOC-load location for these generic DB files is:

- `../ecmccfg/db/generic/`

Current scope:

- generic core substitutions for built-in C++ logic control, timing, and
  debug PVs:
  - [`ecmcCppLogicCore.substitutions`](./ecmcCppLogicCore.substitutions)
- scalar templates for `bi`, `bo`, `longin`, `longout`, `ai`, and `ao`
- string template for short debug text
- waveform templates for array-style exports
- offline generation of custom substitutions through:
  - [`../../../tools/ecmcCppLogicSubstGen.py`](../../../tools/ecmcCppLogicSubstGen.py)

The substitutions generator reads a compiled C++ logic shared library and
inspects the `epics` export declarations through `ecmc_cpp_logic_get_api()`.

The intended IOC-side pattern is:

- use `ecmccfg/scripts/loadCppLogic.cmd` to load the shared library and the
  built-in core substitutions from `ecmccfg/db/generic`
- generate and load one custom substitutions file for the user-defined
  `epics.*` exports

Both target the same dedicated `cpp_logic` asyn port.
