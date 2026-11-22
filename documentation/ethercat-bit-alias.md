# Link-only EtherCAT bit aliases (experimental)

A bit alias maps an existing EtherCAT entry and bit number to a local name,
without creating another EtherCAT DataItem or process-image writer.

Example for an EL7041 (replace slave number as appropriate):

```text
Cfg.EcAddEntryAlias(7,driveStatus01.12,binaryInput01)
Cfg.EcAddEntryAlias(7,driveStatus01.11,binaryInput02)
Cfg.LinkEcEntryToObject(ec0.s7.binaryInput01,ax1.mon.highlim)
Cfg.LinkEcEntryToObject(ec0.s7.binaryInput02,ax1.mon.lowlim)
```

The two-argument path form is also accepted for a real EtherCAT master:
```text
Cfg.EcAddEntryAlias(ec0.s7.driveStatus01.12,binaryInput01)
```

Plain aliases continue to resolve as normal EtherCAT entry names.
Bit aliases are **link-only**: using `ec0.s7.binaryInput01` from an ECMC
PLC is rejected. Direct bit paths in PLCs remain unsupported.

## Regression checklist (requires ECMC IOC/EtherCAT build)

- Existing plain alias resolves in axis links, PLC and asyn.
- Bit alias to a valid bit maps to the same monitor input as an explicit
  `driveStatus01.12` link, while leaving the original entry intact.
- PLC compilation rejects a bit alias as an EtherCAT variable.
- PLC compilation rejects `ec0.s7.driveStatus01.12`.
- Out-of-range bit aliases, unknown target entries and name collisions fail.
- Explicit bit index appended to a bit alias is rejected.
- On real hardware, toggle the forward/backward switches and verify limit
  polarity and safe motion blocking before allowing normal operation.

This change has not yet been validated with a real EtherCAT installation.
