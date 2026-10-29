# Python simulation models

These tools run outside the IOC and use the generated simulation **I/O field**
records. They do not write axis commands or change the EtherCAT driver. The IOC
continues to use one cyclic RT thread. Python feedback is asynchronous and is
intended for functional configuration/motion checks, not RT timing validation.

Install the client dependency in your Python environment:

```sh
python3 -m pip install pyepics
```

The client uses [PyEpics PV reads and completed writes](https://pyepics.github.io/pyepics/pv.html).
It respects the usual Channel Access environment, including
`EPICS_CA_ADDR_LIST`. Start the updated IOC/ecmccfg with `SIM=1` first.

## Discover supported slaves in an IOC

Only one prefix is needed:

```sh
# Read-only discovery: report supported and unsupported hardware.
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM: --list

# Start a model for each supported discovered slave.
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM:
```

Discovery reuses normal ecmccfg metadata. Common ecmccfg hooks expose record
aliases under the simulation prefix:

| Simulation metadata PV | Existing source |
| --- | --- |
| `PREFIXFirstSlave` | `ECMC_PREFIX` + `MCU-Cfg-EC-FrstObjId` |
| `PREFIXecN:sM:HWType` | That slave's normal `HWType` record |
| `PREFIXecN:sM:NxtObjId` | That slave's normal next-slave pointer |

Aliases share the existing records; hardware information is not independently
maintained. Discovery follows sparse slave IDs and rejects broken/cyclic chains.
It does not guess numeric PV names over a range or require PVA. Normal default
slave metadata must be enabled (`DEFAULT_SLAVE_PVS=true`, the default). If you
override `ECMC_SIM_PREFIX`, set it **before adding slaves** so metadata aliases
and generated data records use the same prefix.

The default master is 0. Use `--master N` for a different master and optionally
`--first-slave M` to select a master-specific starting point. The existing
first/next chain stores slave IDs, not master IDs; automatically following chains
across multiple masters is not implemented. Scan a separate chain per master.

Currently supported velocity-mode hardware descriptors:

- `EL7041`
- `EL7041-0052`
- `EL7031` and `EL7031-…` variants
- `EL7047`

Other types are listed as unsupported and left untouched. Supporting a drive
requires a supported descriptor/family in `model_module()` and compatible generated fields;
a similar hardware name alone is not sufficient. The shared model uses channel 01. EL7047 CSP/position-interface variants are not
selected: they need a different command model. EL7031 variants must expose the
same native velocity-mode PVs; record types are checked before feedback writes.
These types share the supplied default scaling; use per-slave overrides when a
configuration has different scaling. EL7047 motor-load feedback is not modeled.
Each supported slave connects to its own module/worker. A connection failure or
runtime error stops that model and is reported; other model workers continue.
These are external [CA-aware Python threads](https://pyepics.github.io/pyepics/advanced.html#using-python-threads),
not additional IOC RT threads.

Optional selection and settings:

```sh
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM: --slave 14
python3 tools/simulation/ioc.py --prefix c6025a-08:SIM: --settings models.json
```

A settings file maps the normal data-path identity to model settings:

```json
{
  "ec0.s14": {
    "full_steps_per_second": 2000,
    "motor_steps_per_rev": 200,
    "encoder_counts_per_rev": 12800,
    "enable_delay": 0.05
  }
}
```

Other optional keys are `counter_bits`, `lower_limit_revs`, `upper_limit_revs`
and `initial_position_revs`. Without an initial-position override, the model
preserves the current raw counter readback. That counter does not identify
previous revolutions after rollover; supply an explicit initial physical
coordinate if model limits need that history. Counter width must match the
configured unsigned position field.

## EL7041 scaling and behavior

Defaults use the supplied scaling:

```text
full_steps_per_second = signed_velocity / 32768 × 2000
encoder_counts_per_full_step = 12800 / 200 = 64
encoder_counts_per_second = signed_velocity × 3.90625
```

Velocity is a signed 16-bit word. `-32768` corresponds to -2000 full steps/s;
`32767` is one raw LSB below +2000. Half scale (`16384`) gives 1000 full steps/s,
64000 encoder counts/s, or 5 rev/s. No axis millimeter/degree scaling is added.
The default 16-bit encoder counter wraps modulo 65536 while the model keeps an
unwrapped physical position and fractional counts internally.

The model reads:

- `PREFIXecN:sM:driveControl01`
- `PREFIXecN:sM:velocitySetpoint01`

It writes only:

- `PREFIXecN:sM:driveStatus01`
- `PREFIXecN:sM:encoderStatus01`
- `PREFIXecN:sM:positionActual01`

Control bit 0 requests enable; enabled feedback follows after 50 ms by default.
Bit 1 clears a model fault. Control bit 2 is reflected as reduced-torque status
bit 6. Status bits 0/1/3 represent ready/enabled/fault. The model uses status
bits 11/12 as normally-closed lower/upper limit inputs. They default to 1 and
clear at configured limits; reverse travel remains possible. Limits are optional
and specified in motor revolutions. Encoder warning/error/latch flags remain zero.
The mappings follow `ecmcEX70XX.cmd` and `ecmcEL7031-chX.template` in ecmccfg.

Only basic enable, velocity, counter feedback and optional limits are modeled.
No acceleration/electrical dynamics, latch/homing sensor model, hardware counter
set handshake, BiSS feedback, SDO behavior or DS402 state machine is supplied.
An axis using a different primary encoder needs feedback for that encoder too.

The default requested loop rate is 100 Hz. Reads bypass the PyEpics monitor cache;
completed feedback writes are checked. Actual elapsed monotonic time is used for
integration. A stalled loop stops instead of integrating a large stale-command
jump. On Ctrl+C, SIGTERM or a runtime error, connected models make a best-effort
fault-status write with enabled feedback cleared, retaining encoder position.
A forced kill or disconnected network cannot guarantee delivery of that write.

## Run one drive directly

```sh
python3 tools/simulation/el7041.py \
  --prefix c6025a-08:SIM: --master 0 --slave 14
```

The standalone model has the same defaults. Use `--help` for scaling, enable
latency, limits, counter width and initial-position options. `--duration 10`
runs a finite ten-second check. It validates generated record types/directions
before writing feedback. Existing hardware command records are not changed into
readback records; observe the generated simulation output PVs.

## Tests

`tests/test_el7041_model.py` checks physical scaling, reverse velocity,
fractional-count accumulation, rollover, enable delay, fault/reset, optional
limits and model/client failure cleanup. `tests/test_simulation_ioc.py` checks
sparse discovery, invalid metadata, support selection, per-slave workers and
read-only listing with controlled CA endpoints and deterministic worker clocks.
Neither test contacts an IOC or requires PyEpics. Live multi-drive CA operation
and actual achieved update rates still need testing on the target system.
