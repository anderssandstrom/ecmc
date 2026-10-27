# Classic EPICS IOC example

This application builds a standalone `ecmcIoc` executable using the standard
EPICS build system. It runs a masterless PLC counter with three Channel Access
PVs. It does not use PSI `require`, `ecmccfg`, `ecmccomp`, `msi`, or EtherCAT
hardware. The ecmc library still links against EtherLab in masterless mode.

## Build on Linux

Build EPICS Base, asyn and motor first, using compatible versions and the same
target architecture. Install the EtherLab headers and user library and build
Ruckig. The makefiles expect headers under `RUCKIG/include/ruckig` and the
library under `RUCKIG/build`.
Keep the motor source tree available: ESS motor releases may leave the required
`motorDevSup.h` in `motorApp/MotorSrc` rather than installing it under `include`.

From the **ecmc repository root**, initialize the bundled exprtk support:

```sh
git submodule update --init --recursive exprtkSupport
```

Create `configure/RELEASE.local` with absolute paths for your installation:

```make
SUPPORT = /opt/epics/support
ASYN = /opt/epics/support/asyn
MOTOR = /opt/epics/support/motor
RUCKIG = /opt/ruckig
ETHERLAB = /opt/etherlab
EPICS_BASE = /opt/epics/base
```

The library and example IOC are separate EPICS application tops. Give both the
same dependency paths, then build from the repository root:

```sh
cp configure/RELEASE.local ecmcExampleTop/configure/RELEASE.local
make -j4
```

Leave `ECMC` and `EXPRTK` at their checked-in defaults: they point to this
checkout and its bundled exprtk support. Dependency paths must agree with those
used to build asyn and motor. The root build orders exprtk support, ecmc and the
example IOC automatically. Rebuild both tops after changing dependency versions
or paths.

## Run

Start from the boot directory so `envPaths` can be found. On x86-64 Linux:

```sh
cd ecmcExampleTop/iocBoot/ecmcIoc
../../bin/linux-x86_64/ecmcIoc st.cmd
```

Substitute your EPICS architecture, for example `linux-aarch64`, as needed.
Keep `counter.plc` alongside `st.cmd` when deploying. The database is installed
into `ecmcExampleTop/db` by the build.

The runtime uses a 1 kHz cycle; PLC 0 executes every 100 ms. No EtherCAT master
is selected or activated. Normal Linux ecmc runtime requirements still apply,
including realtime scheduling and memory-lock permissions required by your
installation. Check the startup log for scheduling failures.

`st.cmd` registers support, creates the asyn port and PLC, compiles the PLC
before binding records, loads the database, starts ecmc, and calls `iocInit`.
The ecmc startup gate holds PLC execution until IOC running. No PSI startup
callbacks are used. Motor support is linked but no axes or motor records are
created.

## Try the PVs

In another terminal with EPICS CA tools available:

```sh
camonitor ECMC:CLASSIC:Counter ECMC:CLASSIC:Seconds
caput ECMC:CLASSIC:Hold 1
caget ECMC:CLASSIC:Counter ECMC:CLASSIC:Seconds
caput ECMC:CLASSIC:Hold 0
```

| PV | Behavior |
| --- | --- |
| `ECMC:CLASSIC:Hold` | Write `1` to pause counting, `0` to resume |
| `ECMC:CLASSIC:Counter` | Readback increases by one per active PLC scan |
| `ECMC:CLASSIC:Seconds` | Readback is `Counter / 10`, excluding paused time |

Counting is enabled initially. Expect approximately ten counts per second after
startup. Values should stop changing while held and resume afterward. Counter
and Seconds are input records. Restarting the IOC resets the counter. Change
`PREFIX` in `st.cmd` to change the PV prefix.

For a local check without CA clients, use the IOC shell:

```iocsh
dbpr "ECMC:CLASSIC:Counter", 1
dbpf "ECMC:CLASSIC:Hold", "1"
dbpr "ECMC:CLASSIC:Counter", 1
dbpf "ECMC:CLASSIC:Hold", "0"
```

Repeat the counter read after a second to verify hold/resume. Use `exit` to stop.

## macOS through Podman

The IOC needs Linux; run it inside Podman's Linux VM on macOS. EtherLab headers
and the user library belong inside the image, not on the Mac. Masterless mode
does not require an EtherCAT kernel driver or hardware.

This example was built and tested in the local image
`localhost/ecmc-classic:etherlab-1.6.3` on Linux aarch64, using EPICS 7.0.10,
asyn R4-45, motor v7.2.8.1-ESS and Ruckig v0.15.3. This is a local image name,
not a publicly downloadable image. The test container is
`ecmc-classic-example-test`; its `/workspace` contains a copy of the sources and
build, so subsequent edits on the Mac are not automatically synchronized.

To run the prepared example interactively from the Mac:

```sh
podman start ecmc-classic-example-test
podman exec -it -w /workspace/ecmcExampleTop/iocBoot/ecmcIoc \
  ecmc-classic-example-test ../../bin/linux-aarch64/ecmcIoc st.cmd
```

Use the IOC shell commands above, or run CA clients in another terminal:

```sh
podman exec ecmc-classic-example-test \
  /opt/epics/base/bin/linux-aarch64/caget ECMC:CLASSIC:Counter
podman exec ecmc-classic-example-test \
  /opt/epics/base/bin/linux-aarch64/caput ECMC:CLASSIC:Hold 1
```

The container was created with `--cap-add=sys_nice --ulimit rtprio=99
--ulimit memlock=-1`. Counting, hold, seconds readback, resume and all three
record alarm severities passed a Channel Access smoke test. The image emitted
two `pthread_attr_setstacksize` warnings at startup; these did not prevent the
functional test from passing. VM operation does not validate realtime timing.
Exit the IOC before launching another instance. To stop the test container:

```sh
podman stop ecmc-classic-example-test
```

## Extending to hardware and ecmccfg

Hardware configuration belongs before `Cfg.SetAppMode(1)`, with corresponding
records loaded before `iocInit`.

Reusing higher-level `ecmccfg` scripts is a separate integration step: it needs
installed script/database paths and explicit replacements for PSI startup hooks.
`ECMC_REQUIRE_ECMC=#-` skips loading the already-linked module in versions that
support it, but does not replace those hooks. Do not insert the PSI startup
script into this example unchanged.


