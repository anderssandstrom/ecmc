# Reusable slit simulation environment — NOT YET VERIFIED

## Current status (2026-10-31)

The setup is incomplete. Do not assume that the IOC or live slit tests run.
No live slit test has passed in this environment yet.

- Existing container: `ecmc-slit-test`; localhost TCP/UDP 5064 and UDP 5065 published.
- The previous standalone-loader experiments under `/tmp` are obsolete.
- New work is isolated under `/opt/slit` in that container.
- Real PSI `require` built successfully and was installed in `/ioc/modules`.
- PSI `misc` build stops in `cal.c`: missing `server.h`. The header is a private
  EPICS Base source header under `modules/database/src/ioc/rsrv`. Check compatibility
  and the proper include path before attempting another build. Do not replace
  the loader with custom command shims.
- RC1 ECMC/ecmccfg module builds have not yet been reached by this new script.
- The container may still contain the failed old IOC process. Inspect it before
  starting another IOC; do not run two IOCs on the same CA ports.

## Version selection and reproducibility

`prepare.py` exports committed sources, excluding all uncommitted refactor edits.
Run with Python 3.12 or newer. Local ECMC/ecmccfg checkouts must contain the refs.
The ExprTk submodule checkout must contain the commit pinned by the selected ECMC.

```sh
ECMC_REF=v11.2.0_RC1 ECMCCFG_REF=v11.2.0_RC1 \
  python3 tools/simulation/podman/prepare.py
```

Each ref may be a branch, tag or commit. Other inputs:
`ECMC_SOURCE`, `ECMCCFG_SOURCE`, `ECMCCOMP_SOURCE`, `ECMCCOMP_REF`,
`REQUIRE_SOURCE`, `MISC_SOURCE`, `SLIT_BUNDLE`.
Without the last two source overrides, PSI require/misc are cloned from GitHub.
Their commits are pinned in the script. The default bundle is
`/tmp/ecmc-slit-bundle.tar.gz`; it includes `versions.json` with resolved hashes.
For exact reproduction, use the recorded hashes rather than moving branch names.

Current selected commits:

- ecmc: `6028f0d06f1019755c25ceab81d14d3244402043`
- ecmccfg: `aecae59837063ffe892b7e610430d42ca4e7243f`
- require: `0856f1c9f5c8e14f1fd1ae0d0e7d59be2ac89afb`
- misc: `cb1a0c758f7df59b7483c3c57e5946ae9a8117b2`

The build currently assumes the existing classic dependency image layout:
EPICS Base in `/opt/epics/base`, asyn/motor/ruckig in `/opt/epics/support`,
EtherLab in `/opt/etherlab`. It is not yet a standalone portable Containerfile.
The existing container has Python packages installed in
`/tmp/SIMSLIT/EcMaster_0/.venv`; the new scripts expect `/opt/slit/venv`, which
still needs provisioning from `requirements.txt` after the loader build works.
The YAML scripts also expect their TMP_DIR virtual environment to be available.

## Inspect or resume without recreating the environment

```sh
podman start ecmc-slit-test
podman exec -it ecmc-slit-test bash
cat /opt/slit/versions.json
cat /opt/slit/build.log
tail -40 /opt/slit/misc-build.log
```

`build.py` and the host copy in this directory are the same at the stopping point.
After correcting a build-script issue, copy only that file into
`/opt/slit/scripts/build.py`. A bounded build attempt is:

```sh
timeout 240 python3 /opt/slit/scripts/build.py > /opt/slit/build.log 2>&1
```

Do not repeatedly rerun the entire build. The script still needs a rerun-safety
review: it modifies the exported ECMC Makefile to add dynamic registration.
It does not modify the user's source checkouts.

## Intended final checks (not yet executed successfully)

`start.sh` should run the original dedicated slit startup through PSI iocsh;
`test.sh` should run the live simulation test runner against it. Before using
these scripts, finish the build and virtual-environment setup, verify the runtime
module layout and stop the obsolete failed IOC process.

From the Mac, once the IOC is running:

```sh
export EPICS_CA_AUTO_ADDR_LIST=NO
export EPICS_CA_ADDR_LIST=127.0.0.1
caget SIMSLIT:MCU-Cfg-Sim-Mode
caget SIMSLIT:SIM:SlitTest:Schema
```

Require simulation mode 1 and the schema expected by the selected configuration.
Run one selected live case first; do not launch the full suite automatically.
The plant/test runner and a separate simulator must not both drive the same PVs.
