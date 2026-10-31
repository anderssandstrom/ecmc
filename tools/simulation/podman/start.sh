#!/bin/bash
set -euo pipefail
source /opt/slit/runtime.env
unset LD_PRELOAD
cd /opt/slit/sources/ecmccfg/examples/PSI/lab_setup/master_slave/slit_epics_2026/slit_matrix_sim_test
export TEST_IOC=SIMSLIT
export ECmasterECMC_DIR=/opt/etherlab/
# Keep the IOC shell available without attaching a terminal.
[[ -p /opt/slit/ioc.in ]] || mkfifo /opt/slit/ioc.in
exec 3<>/opt/slit/ioc.in
exec /opt/slit/iocsh --nopva -n SIMSLIT startup.cmd <&3 >> /opt/slit/ioc.log 2>&1
