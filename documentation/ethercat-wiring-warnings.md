# EtherCAT startup wiring warnings

Before activating the master, ECMC checks the completed IgH bus scan for
Ethernet port wiring anomalies. It checks all discovered slaves, including
couplers without configured PDOs, independently of whether DC is enabled.
These diagnostics only warn: they do not set an ECMC error, block startup,
write ESC registers, or change DC configuration. They run once per activation,
not in the cyclic task and not continuously after a cable is reconnected.

Warnings are stored in the configuration log buffer (`ecmcLogBufferPrint`).
They also use the EtherCAT warning logger; console output follows the logger
control word. The buffer entry is retained even with console warnings disabled.

## Detected cases

- **IN disconnected:** ESC port 0 is an Ethernet port with no link, while
  another Ethernet port has an active, open link. The warning asks the operator
  to check whether the upstream cable was plugged into OUT.
- **Suspected swapped IN/OUT:** with both links active and open, another Ethernet
  port's receive timestamp precedes port 0's. The warning includes the slave
  position/name, port, timestamps, and time difference. On controllers with
  ET1100-style timestamp latching this indicates arrival through a nonzero port.

For an EK1100, port 0 is IN, port 1 is E-bus, and port 2 is OUT. The checks use
the ESC port descriptors, not product names or an assumption that port 1 is OUT.
Internal E-bus ports are excluded. Redundant or intentionally reversed networks
can also produce these warnings; inspect the intended topology.

## Limits

The swapped-cable check is a diagnostic indication, not a universal topology
verification. IgH's public slave information does not provide a timestamp-valid
flag or a verified upstream-port field. Zero/equal timestamps are inconclusive;
older ESC latch modes (notably ESC20/ET1200) may fail to capture the initial
arrival at a nonzero port. A slave without usable receive timestamps cannot be
checked for a two-cable swap by this method. Timestamp subtraction handles
32-bit wrap and assumes traversal takes less than 2^31 ns. No warning does not
prove correct wiring or healthy DC synchronization.

A busy scan or failed information query produces a check-unavailable warning.
Slaves flagged with scan/configuration errors are excluded from wiring inference.
The scan is a snapshot; complete a fresh master scan after servicing the bus.

Validate on hardware with correct wiring, IN empty/OUT connected, and both
cables swapped, on both DC-enabled and non-DC configurations. Confirm the
warning in the configuration buffer and that the diagnostic itself does not
prevent startup. Hardware faults may still cause the existing EtherCAT checks
to prevent operation.

References:

- [Beckhoff coupler port allocation](https://infosys.beckhoff.com/content/1033/ek18xx/2025156747.html)
- [Beckhoff ESC registers, receive times 0x0900–0x090F](https://download.beckhoff.com/download/document/io/ethercat-development-products/ethercat_esc_datasheet_sec2_registers_3i0.pdf)
- [IgH slave information API](https://docs.etherlab.org/ethercat/1.5/doxygen/structec__slave__info__t.html)
