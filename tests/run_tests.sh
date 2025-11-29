#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/.tmp_motion_tests"
mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(-std=c++17 -DECMC_TEST_STUBS -I"${ROOT_DIR}"
              -I"${ROOT_DIR}/tests/mocks"
              -I"${ROOT_DIR}/devEcmcSup/main"
              -I"${ROOT_DIR}/devEcmcSup/com"
              -I"${ROOT_DIR}/devEcmcSup/misc"
              -I"${ROOT_DIR}/devEcmcSup/ethercat"
              -I"${ROOT_DIR}/devEcmcSup/motion")

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
  "${ROOT_DIR}/devEcmcSup/motion/ecmcMasterSlaveStateMachine.cpp" \
  "${ROOT_DIR}/devEcmcSup/main/ecmcError.cpp"

build_and_run trajectory_tests trajectory_tests \
  "${ROOT_DIR}/tests/trajectory_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcTrajectoryBase.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcTrajectoryTrapetz.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcAxisData.cpp" \
  "${ROOT_DIR}/devEcmcSup/main/ecmcError.cpp"

build_and_run encoder_tests encoder_tests \
  "${ROOT_DIR}/tests/encoder_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcEncoder.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcAxisData.cpp" \
  "${ROOT_DIR}/devEcmcSup/motion/ecmcFilter.cpp" \
  "${ROOT_DIR}/devEcmcSup/main/ecmcError.cpp"

build_and_run ethercat_utils_tests ethercat_utils_tests \
  "${ROOT_DIR}/tests/ethercat_utils_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/com/ecmcAsynPortDriverUtils.cpp"

build_and_run ethercat_entry_link_tests ethercat_entry_link_tests \
  "${ROOT_DIR}/tests/ethercat_entry_link_tests.cpp" \
  "${ROOT_DIR}/devEcmcSup/main/ecmcError.cpp"

echo "All tests passed."
