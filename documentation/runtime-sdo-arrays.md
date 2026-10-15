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
