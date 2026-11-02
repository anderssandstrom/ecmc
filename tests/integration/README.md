# Live IOC integration tests

The [slit suite](slit/README.md) requires a running dedicated simulation IOC.
Its plant and test cases live under `slit/`; reusable simulation terminal models
remain under `tools/simulation/` at the repository root.

From the repository root, list the plan without connecting to EPICS:

```sh
python3 tests/integration/slit/run.py
```

Run against the dedicated IOC:

```sh
python3 tests/integration/slit/run.py --prefix SIMSLIT: --run
```

The offline runner `python3 tests/run_tests.py` does not discover or run this
suite. The live runner connects only with `--run` or `--plant-only`.
