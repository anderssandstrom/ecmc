# On-demand PLC metadata diagnostics

The auxiliary ecmcRTLog port exposes compile-time source and registered variable
names. There is no arbitrary live-variable sampler. The unused ecmcPlcWatch.h
prototype has been removed. Cyclic PLC execution is unchanged.

All PLC addresses in one IOC share a steady-clock admission limit of one request
per second. A valid admitted request consumes the interval even when metadata is
unavailable. Rejected requests return asynError and preserve the accepted
response/status. Compilation and invalidation do not reset this IOC-wide limit.
The clock check uses the existing metadata mutex only in the non-RT request path.

Protocol (ECMCCFG ecmcPlcDiag.db):

1. Write a positive signed-32-bit token to PLCnn-DiagCmd; require put completion.
2. Process PLCnn-DiagStat and read its value. A matching positive token is success;
   a matching negative token means metadata is unavailable. Any other token is
   another client's response or an invalidated compile snapshot.
3. Process PLCnn-DiagData and read its CHAR waveform. Validate the header token too.
   Another client may replace a response between operations; retry manually.

Payload bytes: `ECMCPLC1 <token>\n<source byte length>\n<source>\n`, followed by
`L\t<name>\n` or `G\t<name>\n`. Capacity is 65536 bytes. The existing readInt8Array
implementation copies/returns only the actual response length, without padding or
a NUL terminator; waveform NORD carries that count. Oversized metadata is unavailable.
There are no waveform monitor callbacks or periodic record processing.

Validation: the production-header regression exercises IOC-wide throttling,
response preservation, exact length, short destination buffers, invalid inputs,
missing metadata, and invalidation. It compiled and passed locally before the
request to stop ECMC builds. The common-data-links fixture was subsequently
updated to include the production metadata header and PLC index used by clearExpr;
that fixture correction has only been statically reviewed locally. No full IOC
build or hardware RT latency measurement was completed. Metadata traffic can still
cause indirect CPU/cache contention; admission control is not a latency guarantee.
