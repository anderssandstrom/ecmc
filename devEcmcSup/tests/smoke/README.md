# Smoke Build

Minimal build sanity check to catch obvious compile/link issues.

Usage:
```sh
# from repo root
make smoke        # calls tests/smoke/run.sh
# or directly
tests/smoke/run.sh
```

Notes:
- Respects `JOBS` to control parallelism (defaults to detected CPU count).
- Relies on your existing EPICS/EtherCAT toolchain; no extra deps installed.
- Intended as a quick CI/local hook, not a full test suite.
- If `EPICS_BASE` from `../configure/RELEASE` (or an exported `EPICS_BASE`) is missing, the script aborts with a hint to create `../RELEASE.local` setting your local EPICS base.
