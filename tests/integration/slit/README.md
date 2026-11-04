# Moved to ecmccfg

The Python simulator tools, slit integration tests and dedicated test IOC now
live in the **ecmccfg** repository:

- `tools/simulation/`
- `tests/integration/slit/` (runner, plant and cases)
- `tests/integration/slit/ioc/` (dedicated simulation IOC)

From the ecmccfg repository root:

```sh
python3 tests/run_simulation_checks.py
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run
```

Standalone C++ controller, simulation-backend and native-interface tests remain
in ECMC. Reports are written under ecmccfg's `test-results/slit/`.
