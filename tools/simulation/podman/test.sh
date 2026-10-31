#!/bin/bash
set -euo pipefail
source /opt/slit/runtime.env
unset LD_PRELOAD
export PYEPICS_LIBCA=/opt/epics/base/lib/${EPICS_HOST_ARCH}/libca.so
cd /opt/slit/sources/ecmc
exec /opt/slit/venv/bin/python tools/simulation/slit_test/run.py \
  --config /opt/slit/sources/ecmccfg/examples/PSI/lab_setup/master_slave/slit_epics_2026/slit_matrix_sim_test \
  --prefix SIMSLIT: "$@"
