cmake_minimum_required(VERSION 3.16)
project(ecmcMotorSoftLimitTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SUP "${CMAKE_CURRENT_SOURCE_DIR}/../../devEcmcSup")
add_executable(motorSoftLimitTests propagation_checks.cpp ../monitor/support.cpp
  "${SUP}/motion/ecmcMonitor.cpp"
  "${SUP}/motion/ecmcEncoder.cpp"
  "${SUP}/motion/ecmcFilter.cpp"
  "${SUP}/motion/ecmcMotionUtils.cpp"
  "${SUP}/motion/ecmcAxisData.cpp"
  "${SUP}/main/ecmcError.cpp"
  "${SUP}/com/ecmcDataLink.cpp"
  "${SUP}/com/ecmcDataAccess.cpp"
  "${SUP}/com/ecmcDataItem.cpp")
target_include_directories(motorSoftLimitTests PRIVATE "${CMAKE_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/plc-test/offline"
  "${SUP}/motion" "${SUP}/main" "${SUP}/com" "${SUP}/misc" "${SUP}/ethercat")
target_compile_options(motorSoftLimitTests PRIVATE -Wall -Wextra
  -fsanitize=address,undefined,vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(motorSoftLimitTests PRIVATE -fsanitize=address,undefined,vptr)
