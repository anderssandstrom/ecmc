# Runtime SDO byte arrays

`Cfg.EcAddSdoAsyncArray(slave, index, subindex, byteSize, alias)` adds an
asynchronous upload/download endpoint for a byte payload at one SDO
index/subindex. Configure it before entering runtime; trigger transfers via
PVs while running. This reuses `ecmcEcAsyncSDO` and the existing slave cyclic
request processing. Existing scalar `EcAddSdoAsync` configurations and PV names/types are unchanged. The shared request handling now
clears the previous error on accepted submission, rejects another request until
completion is consumed, and rejects scalar upload sizes that do not match the
configured scalar type. Rebuild ecmc; binary compatibility of internal C++
classes with separately compiled plugins is not guaranteed.

Example (replace the slave and object address with those of your device):

```iocsh
ecmcConfigOrDie "Cfg.EcAddSdoAsyncArray(3,0x2000,0x01,4096,payload)"
dbLoadRecords("ecmcSDOAsyncArray.template", "P=IOC:,PORT=ECMC_ASYN,MASTER_ID=0,SLAVE_POS=3,NAME=payload,NELM=4096")
```

Alternatively, use the ecmccfg helper (DT is not needed for byte arrays):

```iocsh
${SCRIPTEXEC} "${ECMC_CONFIG_ROOT}addEcSdoRT.cmd" "SLAVE_ID=3,INDEX=0x2000,SUBINDEX=0x01,BYTE_SIZE=4096,NAME=payload"
```

The helper applies the existing naming-prefix convention and loads the array
PV template from ecmccfg. Omit BYTE_SIZE (or set it to 0) to retain the existing
scalar command and PV templates with DT required. Nonzero BYTE_SIZE selects
array mode; the command rejects sizes outside 1..1048576.

The template is in `devEcmcSup/ethercat/db/` and installed by the EPICS build.
Use its source path if your packaging does not install this database directory.

| PV suffix (after `IOC:SDO-payload-`) | Operation |
| --- | --- |
| `Data` | Stage exactly `byteSize` bytes for download; does not start a transfer |
| `WrtCmd` | Write 1 to download the staged data |
| `RdCmd` | Write 1 to upload from the slave |
| `Data-RB` | Last successful upload, UCHAR waveform; NORD is actual byte count |
| `Bytes-RB` | Length of last successful EtherCAT upload, including an oversized response |
| `Bsy` | Transfer pending, until completion is processed |
| `ErrId` | 0 or ecmc SDO error code |

The underlying parameters are `ec0.s3.sdo.payload.value`, `.writevalue`,
`.bytes`, `.readcmd`, `.writecmd`, `.busy`, and `.error` respectively.
The trigger records automatically reset to zero; direct parameter users must
provide a 0-to-1 edge for each request. Wait for Bsy to clear and inspect ErrId
before consuming uploaded data or issuing the next command.

Uploads can contain 0..byteSize bytes. Oversized uploads return
`ERROR_EC_SDO_ASYNC_SIZE` (0x23504) and preserve the previous good waveform.
The byte-count PV still reports the received size. Downloads require a full
waveform write first; partial/oversized staging writes are rejected without
changing the previous staged payload. A rejected staging write must not be
followed by a download trigger. Staging is rejected while a transfer is busy.
Successful request submission clears the previous error.

Downloads always send the configured byteSize, even after a shorter upload.
Separate EtherCAT requests and buffers prevent uploads from changing the
staged output or download size. This works with the existing EtherCAT request
API without requiring a variable-size write extension. For variable-size
objects, configure the required download size before runtime.

The configured limit is 1 MiB per endpoint. Buffers and requests are allocated
at configuration time. The EtherCAT master may allocate additional memory if
a slave returns a larger response; the configured capacity bounds ecmc's copy,
not the master's upload allocation. Large copies and PV callbacks still cost
cycle time; this is mailbox communication, not a deterministic cyclic PDO path.
The existing 2000 ms request timeout applies.

Payloads are raw bytes: callers encode/decode multibyte values in the device's
specified format. This feature does not iterate subindices, perform Complete
Access, or implement file transfer. The slave must support the requested object
and payload length. For large CA waveforms, configure the IOC and client
`EPICS_CA_MAX_ARRAY_BYTES` above the payload size (allow protocol overhead).

### RT SDO Object Access From PLC And C++ Logic

PVs, PLC functions and C++ logic helpers access the **same asynchronous SDO
object**. Configure it with `addEcSdoRT.cmd` before runtime. A transfer initiated
by any of these interfaces makes the object busy for all of them; overlapping
requests are rejected rather than queued.

An **RT SDO object index** is a zero-based index within a slave's configured
async SDO objects, in creation order. Scalar and array objects share that list.
It is not the EtherCAT SDO index/subindex (such as `0x2000:01`), nor an array byte
offset. Resolve it by slave position and alias instead of relying on creation
order:

```bash
ecmcConfig "EcGetSdoRTObjectIndex(3,payload)"
```

The result is printed and stored in `ECMC_CONFIG_RETURN_VAL`. After confirming
that lookup succeeded (a number, not `Error: ...`), it can be saved for PLC macro
expansion:

```bash
epicsEnvSet("PAYLOAD_SDO_OBJECT_INDEX", "${ECMC_CONFIG_RETURN_VAL}")
```

Lookup fails for a missing slave/name or an ambiguous duplicate alias. Resolve
again after changing startup configuration; do not persist object indices across
configuration changes. The lookup uses the configured ecmc EtherCAT master.

#### PLC Functions

Here `slave` is the slave position, `object` is the RT SDO object index, and
`element` has the scalar/array meaning explained below.

| Function | Result / action |
| --- | --- |
| `ec_sdo_read(slave, object)` | Submit one upload from slave to IOC |
| `ec_sdo_write(slave, object)` | Submit one download of staged data to the slave |
| `ec_sdo_get_busy(slave, object)` | `0` idle or `1` pending completion |
| `ec_sdo_get_error(slave, object)` | Stored SDO object error code; `0` means no error |
| `ec_sdo_get_size(slave, object)` | Valid cached upload byte count for arrays; `1` for scalars |
| `ec_sdo_get_capacity(slave, object)` | Configured byte count for arrays; `1` for scalars |
| `ec_sdo_get_value(slave, object, element)` | Read a cached value; does not initiate an upload |
| `ec_sdo_set_value(slave, object, element, value)` | Stage a value; does not initiate a download |

For a **scalar**, use element `0`: `get_value` returns the whole value decoded
according to `DT` (for example, a `U32` as one number, not four separate bytes).
`set_value` converts the supplied number to that configured type. Scalar access
uses the existing shared value buffer, so a staged scalar value is also visible
to `get_value` until an upload replaces it.

For an **array**, `element` is a zero-based byte offset. Values are unsigned
bytes, `0..255`, regardless of any `DT` argument. `get_value` reads only the
valid uploaded range; `set_value` changes the separate download staging buffer
within its configured capacity. Array size is `0` before the first successful
upload. An oversized upload preserves the previous valid data and size; the
`Bytes-RB` PV reports the oversized response length.

All download bytes must have been initialized at least once, either through a
full `Data` waveform write or individual `set_value` calls, before the first
array download. Afterwards individual bytes can be updated while the rest retain
their staged values. Uploading does not initialize download staging. Setters are
rejected while busy, and reject out-of-range indices, fractional integer values,
nonfinite values, and values outside the destination type's range. Staged array
waveform callbacks are published at download submission rather than per byte.

`read`, `write` and `set_value` return `0` on success or an ecmc error code.
The status/size getters return a negative error code if the call fails.
`get_value` returns `0` on failure; check `ec_get_err()` immediately to distinguish
that from a valid zero value. All these calls update `ec_get_err()` with their
API result. `get_error` separately reads the SDO object's stored transfer/PV error;
a successful `get_error` call sets `ec_get_err()` to zero even when the stored
SDO error is nonzero.

PLC numeric values are doubles. Scalar 64-bit integers outside the exact integer
range of doubles (±2^53) can lose precision. Use raw byte arrays if exact wider
integer encoding is needed and the device exposes a suitable payload.

Call transfer functions once per application request, not unconditionally every
cycle. For example, this PLC snippet submits an upload on a rising request edge:

```text
if (static.upload_request and not static.upload_request_old) {
  static.sdo_rc := ec_sdo_read(3, ${PAYLOAD_SDO_OBJECT_INDEX});
};
static.upload_request_old := static.upload_request;
```

In subsequent cycles, check `ec_sdo_get_busy(...)` and `ec_sdo_get_error(...)`
before consuming uploaded values. Check each function's result; a submitted
request is not a completed transfer. For a configured scalar, element `0` gives
the whole typed value; for an uploaded array it gives the first byte:

```text
static.value := ec_sdo_get_value(3, ${PAYLOAD_SDO_OBJECT_INDEX}, 0);
static.sdo_rc := ec_get_err();
```

#### C++ Logic Helpers

Include `ecmcCppSdo.hpp` alongside `ecmcCppLogic.hpp`. It wraps the public
`ecmcSdoApi.h` functions and requires the corresponding ecmc runtime symbols,
just like the motion wrappers. It does not extend the C++ logic host-service ABI.

Resolve the object once after SDO configuration, for example when constructing
the logic instance:

```cpp
#include "ecmcCppSdo.hpp"

ecmcCpp::SdoRef sdo;
int rc = ecmcCpp::sdoFind(3, "payload", sdo);
// Check rc before using sdo. sdo.slave == 3; sdo.objectIndex is resolved by name.
```

For the object index alone, use
`ecmcCpp::sdoFindRTObjectIndex(3, "payload", objectIndex)` with an `int` output.
On an application upload event, call `ecmcCpp::sdoRead(sdo)` once. In subsequent
cyclic callbacks, inspect the result:

```cpp
int busy = 0;
int transferError = 0;
int count = 0;
double value = 0;

rc = ecmcCpp::sdoGetBusy(sdo, busy);
if (rc == 0 && !busy) {
    rc = ecmcCpp::sdoGetError(sdo, transferError);
    if (rc == 0 && transferError == 0) {
        rc = ecmcCpp::sdoGetSize(sdo, count);
        if (rc == 0 && count > 0) {
            rc = ecmcCpp::sdoGetValue(sdo, 0, value);
        }
    }
}
```

Use `sdoSetValue(sdo, element, value)` to stage scalar values or individual array
bytes, then `sdoWrite(sdo)` once to submit the download. `sdoGetCapacity(sdo, count)`
returns the number of staging elements. All helpers return an error code (`0`
on success), with getters writing their value through the output reference.
The scalar/array conversion and initialization rules are the same as for PLCs.

Call these helpers from the serialized C++ logic cyclic callback. External
threads must use the ecmc asyn port lock. Do not bind outputs directly to
`.readcmd` / `.writecmd`: plain data-item bindings copy memory and do not invoke
the SDO command callbacks. Use the helpers to submit requests.

