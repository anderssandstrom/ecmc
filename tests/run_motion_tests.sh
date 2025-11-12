#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/.tmp_motion_tests"
mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(-std=c++17 -DECMC_TEST_STUBS -I"${ROOT_DIR}"
              -I"${ROOT_DIR}/tests/mocks"
              -I"${ROOT_DIR}/devEcmcSup/main"
              -I"${ROOT_DIR}/devEcmcSup/com")

build_and_run() {
  local target="$1"
  local output="$2"
  shift 2
  echo "Building ${output}"
  g++ "${COMMON_FLAGS[@]}" "$@" -o "${BUILD_DIR}/${output}"
  echo "Running ${output}"
  "${BUILD_DIR}/${output}"
}

build_and_run axis_group_tests axis_group_tests \
  "${ROOT_DIR}/tests/axis_group_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcAxisGroup.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcMasterSlaveStateMachine.cpp"

build_and_run trajectory_tests trajectory_tests \
  "${ROOT_DIR}/tests/trajectory_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcTrajectoryBase.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcTrajectoryTrapetz.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcAxisData.cpp" \
  "${ROOT_DIR}/devEcmcSup/main/ecmcError.cpp"

echo "All motion tests passed."
