# Memory simulation I/O

`ecmcSimulationDriver` provides typed memory for the common data layer. It does
not simulate drive behavior. A PLC, Python PV client or later C++ model supplies
feedback separately. There is no automatic relationship between an output and
an input, and no additional simulation thread.

## Backend selection and names

Select simulation before the corresponding master/device declarations:

```iocsh
ecmcConfigOrDie "Cfg.SetSampleRate(500)"
ecmcConfigOrDie "Cfg.SetIoBackend(0,1)"
ecmcConfigOrDie "Cfg.EcSetMaster(0)"
# Load the existing slave, entry and motion configuration here.
```

Backend `0` means the existing EtherCAT implementation (the default); backend
`1` means memory simulation. Selection is configuration-only and locked after
an instance is selected or created. Set the sample rate before creating objects.
`MASTER_ID=-1` retains its existing masterless meaning and does not implicitly
select this backend.

Names stay unchanged: `ec0.s2.positionActual01` resolves to the selected instance's
storage. There is no `sim.` prefix. For mixed operation, assign distinct IDs,
for example simulation instance 0 and physical EtherCAT master 1. Select the
appropriate instance with `Cfg.EcSetMaster(id)` before its configuration commands.
Full signal paths identify their instance independently of the current selection.
This does not add support for multiple physical masters to the existing driver.

With ecmccfg, pass `SIM=1` to `startup.cmd` (or its `require` arguments):

```iocsh
require ecmccfg "MASTER_ID=0,SIM=1,EC_RATE=500"
```

`SIM` defaults to `0`. Startup selects the backend before `addMaster.cmd` claims
the instance and skips MX-System hardware discovery in simulation mode.
`MASTER_ID=-1` retains masterless behavior even when `SIM=1`.

## Shared values and external models

Slave/entry declarations create stable scalar storage with native type and
width. Common motion and PLC links bind that storage before runtime. The normal
encoder, drive and monitor logic continues to consume these links. Existing
numeric encoder/drive/monitor link commands resolve the selected backend to the
same `ecmcDataAccess` descriptor, then use the same attachment and feature setup
code. There is no simulation-specific motion binding helper. Brake/reduce-torque
enabling, home-switch/interlock enabling and axis checks are shared; backend
resolution and motion-use metadata are configuration-time operations only.
Simulation output resources may be connected with `Cfg.LinkDataToObject` using
native conversion (`0`); an integer resource can also select a bit. Floating
point bit selections are rejected.

Realtime entries register asyn parameters with their existing names. Both input
and output storage permit writes so an external model can inject inputs and
observe outputs. Existing EPICS database records still have to be loaded; the
driver does not create records. Publication uses the existing slow-work phase.
Entries declared without realtime publication have storage but no newly
registered scalar asyn parameter.

Values may also be injected through serialized ECMC commands:

```iocsh
ecmcConfigOrDie "Cfg.SimSetValue(ec0.s2.positionActual01,123)"
ecmcConfigOrDie "Cfg.SimSetHealthy(0,0)"
ecmcConfigOrDie "Cfg.SimSetHealthy(0,1)"
```

The value command accepts a whole scalar with range/type checks, including exact
64-bit integers and finite floating point values. It requires an existing entry;
it does not create misspelled names. Numeric values default to zero. `ZERO` and
`ONE` retain their existing constant conventions (`ONE` is `0xffffffff`).

`ec0.simulation.healthy` is a writable Int32 asyn parameter (`0` or `1`); substitute
the selected ID for `0`. Health gates the instance's signal bindings and startup
readiness. This is explicit fault injection, not an emulation of missed-frame
filtering. The physical EtherCAT domain-failure tolerance remains unchanged.
The simulation startup reset does not clear an injected fault.

Python PV writes are asynchronous and suitable for configuration and functional
checks. They are not cycle-synchronous feedback. A future RT model needs explicit
phase scheduling and ownership of its signals. Models must use the existing
serialized access paths; stable pointers alone do not make concurrent writes safe.

## Configuration coverage

| Surface | Memory backend behavior |
| --- | --- |
| Slave and scalar PDO-entry declarations, aliases, constants | Create typed memory; retain the normal signal names and declaration metadata |
| Encoder, drive, monitor and ordinary PLC scalar links | Bind through the common data layer |
| Immediate scalar (`EcWriteSdo`) and configured scalar/typed/buffer/complete SDO writes | Store bytes in a per-device object dictionary; no mailbox transfer |
| Scalar SDO read/verify | Read the stored object with matching size; missing objects fail |
| DC, watchdog, timing overrides, domain/offline and diagnostic settings | Retain configuration metadata; no physical timing, timeout or bus behavior is synthesized |
| Device identity checks | Use declared vendor/product identity; revision/serial are stored dictionary values or zero when unspecified |
| Cyclic phases | Run on the existing RT thread, before control and after safety/publication; do not generate device feedback |

Immediate scalar SDO writes accept 1–4 bytes, matching the command's 32-bit
payload. For example, the `0x1011:1` reset write in `slaveVerify.cmd` stores the
request for later readback; it does not erase configured signals or run a reset
model.

This is currently scalar buffer simulation, not a complete EtherLab emulator.
Memory maps, derived process-image items, asynchronous SDO requests, SoE,
entry-copy services and several specialized timing services return
`ERROR_MAIN_IO_CONFIG_UNSUPPORTED` for a selected simulated instance. Some
specialized axis status/sequencer/PVT link commands still require migration.
The standard master/slave status and count parameters and default-domain
parameters are registered with their existing asyn names:

- `ecN.masterstatus`, `ecN.slavecounter`, `ecN.entrycounter`, `ecN.memmapcounter`
- `ecN.sM.slavestatus` for each nonnegative configured device position
- `ecN.dom0.domainstatus`, `ecN.dom0.domainfailcountertotal`

These parameters are read-only. Existing record bit layouts are retained:
a validated healthy instance reports synthetic online/OP and domain-complete
flags; an unhealthy instance clears those flags and reports domain-zero.
Status publication occurs in the existing slow phase. Counts describe configured
nonnegative devices and declared entries, excluding built-in `ZERO`/`ONE` and
aliases. Memory-map count is zero because memory maps are unsupported. Master
status embeds the healthy simulated device count in its high 16 bits, and slave
status embeds that device's entry count. The domain working-counter field is
zero; no measured EtherCAT counter is available. The failure total counts cyclic
status checks with injected unhealthy state, saturates at UINT32_MAX, and is
retained on recovery and startup reset.

Additional domain and hardware timing diagnostic collections are not reproduced.
Loading every hardware diagnostic template is not yet verified, so an arbitrary
complete ecmccfg startup is not yet guaranteed to work unchanged.

The driver does not exercise PDO byte offsets, bit packing, endianness conversion,
distributed clock discovery or physical bus state. Stored configuration settings
must not be interpreted as proof that those hardware behaviors were tested.

## Validation

`test_simulation_driver.py` compiles the real memory driver with controlled asyn
publication. It covers diagnostic names/bit layouts/counts and fault recovery
with 31 configured devices, plus shared storage, typed writes, aliases, object dictionaries,
faults and invalidation. `test_simulation_configuration.py` exercises the actual
registry and extracted configuration/parser routes with controlled hardware and
axis endpoints. It also checks that the parser's API header compiles as C.

Common-link tests cover mixed sources and simulated outputs; cyclic/startup tests
cover mixed ordering and unhealthy simulation startup. These standalone tests do
not load an IOC, perform network CA/PVA access or measure RT deadlines. The
EtherCAT forwarding code-generation probe does not establish full-loop timing
parity after adding simulation iteration. Production performance remains to be
measured on the target system.
