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
and output storage retain the existing raw write capability. Cyclic input/output
publication runs once per corresponding RT phase, with asyn applying the record's
requested sampling divider. Diagnostic publication also advances once per cycle;
slow work does not apply a second sampling divider. Repeated scalar writes do not
advance the simulation sampling clock. Entries declared without realtime
publication have storage but no newly registered scalar asyn parameter.

## Generated simulation field records

The common ecmccfg `setAppMode.cmd` hook loads these records automatically when
startup used `SIM=1`. This includes the normal finalize path, which calls
`setAppMode.cmd`. No per-device hardware template or axis record is changed.
For manual setup, call this after declaring the simulated entries and before
runtime or `iocInit`:

```iocsh
ecmcConfigOrDie "Cfg.SimLoadRecords(c6025a-08:SIM:)"
```

`SIM:` belongs to the generated record names. Existing ECMC signal paths such
as `ec0.s14.driveStatus01` and all axis links retain their names. Record names
use `:` between the instance, device and field because EPICS reserves `.` for
field access.

| Declared field direction | Generated records |
| --- | --- |
| Input to ECMC (status, encoder position, switches) | Writable `PREFIXecN:sM:field` plus readback `PREFIXecN:sM:field-RB` |
| Output from ECMC (control word, setpoint) | Readback `PREFIXecN:sM:field` plus writable `PREFIXecN:sM:field-Set` |

For example, with `ECMC_PREFIX=c6025a-08:` and simulated slave 14:

```sh
# Observe the control word produced by the controller.
caget c6025a-08:SIM:ec0:s14:driveControl01
# Write a simulated output once; its cyclic owner may overwrite it.
caput c6025a-08:SIM:ec0:s14:driveControl01-Set 1
# Inject status: bit 0 ready, bit 1 enabled. This is a test value, not a model.
caput c6025a-08:SIM:ec0:s14:driveStatus01 3
caget c6025a-08:SIM:ec0:s14:driveStatus01-RB
```

These records represent native I/O fields, not axis engineering values. An encoder
position write supplies raw counts unless the declared field itself is floating
point. The loader chooses `longin/longout` for signed values up to 32 bits,
`mbbiDirect/mbboDirect` for unsigned values/bit fields, `ai/ao` for floating point,
and `int64in/int64out` for 64-bit integers. Unsigned 64-bit fields use signed
Int64 bit-pattern representation (the upper half appears negative); no conversion
through double occurs. Exact unsigned decimal injection also remains available
with `Cfg.SimSetValue`. Builds without asyn Int64 support reject 64-bit record
creation instead of losing precision.

The generated records use private native asyn views (`sim.ecN.sM.field`) sharing
the original descriptor's buffer. These views have independent asyn metadata so
legacy scaled/converted records cannot change the model interface's datatype.
There is no second I/O image. Private simulation views permit input injection
and manual output writes through their generated records. Output `-Set` writes
are single writes to the same buffer, with no cyclic forcing or ownership change.
Injection records do not process at initialization, preserving preloaded values.

Only declared realtime scalar fields at nonnegative device positions are included;
built-in ZERO/ONE and aliases do not create duplicate records. Generated names use
the canonical declared field names. Repeated loading with the same prefix adds
only newly declared fields; changing the prefix for existing records is rejected.
Name/type collisions are preflighted. A subsequent EPICS database-load failure
is reported; restart the IOC after resolving it because EPICS may have loaded
part of that batch. Creation is refused after runtime or the IOC-running hook.
The ecmccfg default prefix is `${ECMC_PREFIX}SIM:` and can be overridden with
`ECMC_SIM_PREFIX` before `setAppMode.cmd`. Raw asyn views require additional
parameter slots; increase `MAX_PARAM_COUNT` at startup if the table is too small.

The loader uses the standard [EPICS static database API](https://docs.epics-controls.org/projects/base/en/b7.0.10/dbStaticLib_h.html)
from configuration code. It allocates records before RT, without device models
or another worker thread.

Entry aliases created by `Cfg.EcAddEntryAlias` or `Cfg.EcAddEntryAliasByPath`
resolve through the same data descriptor. For published entries they are also
registered with the existing asyn parameter-alias service: original and alias
names share the same parameter, conversions, publication and value storage.
For example, the legacy `encoderLatchPostion01` name and its corrected
`encoderLatchPosition01` alias both work. Aliases do not increase entry counts;
repeating an alias for the same entry is accepted, while conflicts are rejected.

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

- `ecN.masterstatus`, `ecN.ok`, `ecN.slavecounter`, `ecN.entrycounter`, `ecN.memmapcounter`
- `ecN.sM.slavestatus` for each nonnegative configured device position
- `ecN.dom0.domainstatus`, `ecN.dom0.domainfailcountertotal`

These parameters are read-only. Existing record bit layouts are retained:
`ecN.ok` is 1 for an active healthy instance and 0 otherwise.
A validated healthy instance reports synthetic online/OP and domain-complete
flags; an unhealthy instance clears those flags and reports domain-zero.
Status publication follows the cyclic asyn sampling clock. Counts describe configured
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
with 31 configured devices, cyclic publication cadence including a short enable
pulse, plus shared storage, typed writes, aliases, object dictionaries,
faults and invalidation. `test_simulation_configuration.py` exercises the actual
registry and extracted configuration/parser routes with controlled hardware and
axis endpoints. It also checks that the parser's API header compiles as C.

Common-link tests cover mixed sources and simulated outputs; cyclic/startup tests
cover mixed ordering and unhealthy simulation startup. These standalone tests do
not load an IOC, perform network CA/PVA access or measure RT deadlines. The
EtherCAT forwarding code-generation probe does not establish full-loop timing
parity after adding simulation iteration. Production performance remains to be
measured on the target system.

`test_simulation_records.py` compiles the real generator and driver with controlled
EPICS static-database calls. It verifies native record types, names, startup
write behavior, raw buffer identity, read/write permissions, collision checks,
64-bit storage and cleanup. It does not run IOC record processing or network
clients. The generated records still need an IOC integration check.

## External Python models and discovery

A separate [modular slit acceptance program](../tools/simulation/slit_test/README.md)
uses the sibling ecmccfg `slit_matrix_sim_test` configuration. Modules cover
hardware/software limits, missing/lost enable, EL5042 readiness/frozen feedback,
brake outputs, open/closed-loop motion and master/slave synchronization. It runs its own coupled
plant and writes JSON/JUnit reports. Offline infrastructure checks do not imply
that these live IOC acceptance tests have passed.

The ecmccfg `ecmcMain.ui` panel shows **REAL** (green) or **SIMULATED** (amber)
below the tabs using `$(P)MCU-Cfg-Sim-Mode`. This PV reports the IOC startup
`SIM` setting (default 0); it does not select a backend or inspect later manual
backend changes. Reinstall ecmccfg and restart the IOC to load the new metadata
record. No ECMC rebuild is needed for this panel indicator.

The [Python simulation tools](../tools/simulation/README.md) provide stepper and
DS402 CSV/CSP models with a discovery runner for supported slaves. They write the generated I/O
field records, independently of axis commands and the memory driver.

```sh
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM: --list
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM:
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM: --gui
```

Discovery uses `FirstSlave`, `ecN:sM:HWType` and `ecN:sM:NxtObjId` aliases under
that prefix. Common ecmccfg hooks alias the existing normal metadata records;
no second hardware inventory is maintained. Set a custom `ECMC_SIM_PREFIX`
before adding slaves, and leave normal default slave metadata enabled. The
current discovery follows a single master's existing slave chain; see the tool
README for master selection, supported descriptors and per-slave settings.

The EL7041 defaults use 2000 full steps/s, 200 full steps/rev and 12800 encoder
counts/rev. Unsupported types are listed and left untouched. A failed model
connection stops only that external worker; no global simulated bus fault is
injected. CA feedback and Python worker timing are not cycle-synchronous.

E*72xx hardware selects the separate DS402 model. It infers CSV/CSP from the
native command fields and implements the drive enable/reset sequence. If both
command fields exist, select the mode explicitly in the per-slave settings.
Default position scaling is 2^20 counts/rev and velocity scaling is
2^31/8000 units per rev/s, matching the repository servo CSV example; override
these for other motor configurations. See the tool README for field types,
channel selection and model limitations. No additional IOC records are needed.

The optional Tk window displays live drive values and model status. Basic
EL1004/1008/1808/1809 digital inputs can be set manually; EL2004/2008/2808
outputs can also be written through generated `-Set` records. Input display uses generated `-RB`
records, and existing input values are retained until explicitly changed.
See the tool README for supported channel layouts and UI requirements.

The GUI also exposes manual scalar fields for EL5042, EL5102 and EL5101 variants,
including exact 64-bit encoder positions. Every displayed writable simulation
field can be edited numerically, including drive commands and feedback; active
models/PLCs may overwrite manual values. Native widths/ranges are validated.
The 7062 family reuses the DS402 CSV/CSP model with independent configured
channels 01/02. Encoder/drive coupling, oversampling arrays and real DC timing
are not inferred or emulated by these Python tools.
