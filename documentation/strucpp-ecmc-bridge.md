# STruCpp and ecmc Bridge

This note describes the current intended structure for running
`STruCpp`-generated Structured Text inside `ecmc`.

The bridge is now split into two sibling repositories:

- generic host plugin:
  [`../ecmc_plugin_strucpp`](../../ecmc_plugin_strucpp)
- curated source examples:
  [`../ecmc_plugin_strucpp/examples/psi_ioc_examples`](../../ecmc_plugin_strucpp/examples/psi_ioc_examples)

The old in-repo PoC under `plugins/strucpp_bridge_poc` has been retired.

## Core Idea

The integration works because both sides already expose the right primitives:

- `ecmc` owns the realtime EtherCAT process image and already lets plugins bind
  to `ecmcDataItem` buffers.
- `STruCpp` generates a `locatedVars[]` table for `%IX/%QX/%MW` variables and
  exposes raw variable storage through `IECVar<T>::raw_ptr()`.

So the bridge does not need a full IEC runtime inside `ecmc`. It only needs to:

1. bind `STruCpp` located variables to `%I`, `%Q`, and `%M` byte images,
2. copy `%I` bytes into ST variable storage before `run()`,
3. call the generated `Program_*::run()`,
4. copy `%Q` bytes back into the output image after `run()`.

## Runtime Split

### Host plugin

The generic host lives in:

- [`../ecmc_plugin_strucpp/src/ecmcPluginStrucpp.cpp`](../../ecmc_plugin_strucpp/src/ecmcPluginStrucpp.cpp)
- [`../ecmc_plugin_strucpp/src/ecmcStrucppBridge.cpp`](../../ecmc_plugin_strucpp/src/ecmcStrucppBridge.cpp)
- [`../ecmc_plugin_strucpp/src/ecmcStrucppBridge.hpp`](../../ecmc_plugin_strucpp/src/ecmcStrucppBridge.hpp)

It uses the normal `ecmc` plugin interface and is loaded through
`Cfg.LoadPlugin(...)`.

At runtime it:

1. parses `logic_lib=...;input_item=...;output_item=...;memory_bytes=...`
2. `dlopen()`s the logic library once
3. builds a copy plan from the logic library's `locatedVars[]` table at
   realtime entry
4. executes only that compiled plan in the realtime loop

### Logic library

The application-specific logic is kept as source examples under the plugin
repo.

The sample app lives in:

- [`../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/generated/machine.hpp`](../../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/generated/machine.hpp)
- [`../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/generated/machine.cpp`](../../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/generated/machine.cpp)
- [`../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/machine_logic.cpp`](../../ecmc_plugin_strucpp/examples/psi_ioc_examples/sample_app/src/machine_logic.cpp)

That split is the important boundary:

- `ecmc_plugin_strucpp`
  stays generic and reusable
- example app subtree
  owns generated code and the tiny wrapper that exposes the logic ABI

## Logic ABI

The shared contract between the host and each logic library is:

- [`../ecmc_plugin_strucpp/src/ecmcStrucppLogicIface.hpp`](../../ecmc_plugin_strucpp/src/ecmcStrucppLogicIface.hpp)
- [`../ecmc_plugin_strucpp/src/ecmcStrucppLogicWrapper.hpp`](../../ecmc_plugin_strucpp/src/ecmcStrucppLogicWrapper.hpp)

Each logic library exports:

```cpp
extern "C" const ecmcStrucppLogicApi* ecmc_strucpp_logic_get_api();
```

And usually uses the wrapper macro:

```cpp
#include "ecmcStrucppLogicWrapper.hpp"
#include "my_program.hpp"

ECMC_STRUCPP_DECLARE_LOGIC_API("my_logic",
                               strucpp::Program_MYPROGRAM,
                               strucpp::locatedVars);
```

That keeps the application-side handwritten code to one short wrapper file.

## Data Model

The bridge uses byte images, not packed struct overlays:

```cpp
struct ecmcStrucppIoImageSpan {
  uint8_t* data {};
  size_t size {};
};

struct ecmcStrucppIoImages {
  ecmcStrucppIoImageSpan input;
  ecmcStrucppIoImageSpan output;
  ecmcStrucppIoImageSpan memory;
};
```

This avoids alignment, packing, and bitfield ABI problems.

The current host supports three binding modes:

- startup-linked mapping mode
  a small manifest maps exact STruCpp addresses like `%IW0` or `%QW2` to
  literal `ecmcDataItem` names, and the plugin links those once before RT
- contiguous image mode
  one `ecmcDataItem` buffer for `%I` and one for `%Q`, typically memmaps
- direct binding mode
  a list of `<offset>:<ecmcDataItem>[@bytes]` mappings for `%I` and `%Q`
- `%M`
  plugin-owned byte array

The mapping mode is the better production direction because it lets the plugin
inspect the actual ST located-variable table, verify coverage at startup, and
compile direct pointer ops for the RT loop. The explicit direct-binding mode
still uses a virtual `%I/%Q` image around the logic cycle, which is useful as a
fallback but not as clean.

## Efficiency Model

The generic host is designed to stay efficient enough for normal PLC-sized
logic:

- `dlopen()` and symbol lookup happen once
- located-variable validation happens once
- the realtime loop executes a precompiled copy plan
- bit variables use a small bit path
- scalar variables use direct `memcpy()` operations

The current production direction is:

1. generic host plugin
2. compiled copy plan
3. application-specific loadable logic library

That keeps the design generic without decoding descriptors in the realtime loop.

## IOC Integration

The generic host plugin repo includes:

- startup helper:
  [`../ecmc_plugin_strucpp/startup.cmd`](../../ecmc_plugin_strucpp/startup.cmd)
- IOC example:
  [`../ecmc_plugin_strucpp/examples/iocsh_examples/loadPluginExample.cmd`](../../ecmc_plugin_strucpp/examples/iocsh_examples/loadPluginExample.cmd)

Typical flow:

1. decide whether `%I/%Q` are easier to expose as a mapping file, memmaps, or
   direct bindings
3. build one logic library
4. load `ecmc_plugin_strucpp`
5. pass the logic library path and either a mapping file, memmap names, or
   binding lists in the plugin config string

## Local References

- `ecmc` plugin execution order:
  [`devEcmcSup/main/ecmcMainThread.cpp`](../devEcmcSup/main/ecmcMainThread.cpp)
- `ecmc` domain raw pointer:
  [`devEcmcSup/ethercat/ecmcEcDomain.cpp`](../devEcmcSup/ethercat/ecmcEcDomain.cpp)
- `ecmc` entry offsets:
  [`devEcmcSup/ethercat/ecmcEcEntry.cpp`](../devEcmcSup/ethercat/ecmcEcEntry.cpp)
- `STruCpp` located runtime model:
  [`../../strucpp/src/runtime/include/iec_located.hpp`](../../strucpp/src/runtime/include/iec_located.hpp)
- `STruCpp` code generation:
  [`../../strucpp/src/backend/codegen.ts`](../../strucpp/src/backend/codegen.ts)

## Recommended Next Step

For real use, the next practical step is:

1. bind the host to one real `%I/%Q` source in an IOC, preferably through a
   startup-linked mapping file
2. build the sample logic library from
   [`../ecmc_plugin_strucpp/examples/psi_ioc_examples`](../../ecmc_plugin_strucpp/examples/psi_ioc_examples)
3. run one end-to-end cycle test

After that, if needed, the next extension is direct domain-image binding instead
of `ecmcDataItem`-backed `%I/%Q` images.
