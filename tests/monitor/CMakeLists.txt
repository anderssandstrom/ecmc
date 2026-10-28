cmake_minimum_required(VERSION 3.16)
project(ecmcMonitorTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SUP "${CMAKE_CURRENT_SOURCE_DIR}/../../devEcmcSup")
add_executable(monitorTests monitor_checks.cpp support.cpp
  "${SUP}/motion/ecmcMonitor.cpp"
  "${SUP}/motion/ecmcEncoder.cpp"
  "${SUP}/motion/ecmcFilter.cpp"
  "${SUP}/motion/ecmcMotionUtils.cpp"
  "${SUP}/motion/ecmcAxisData.cpp"
  "${SUP}/main/ecmcError.cpp"
  "${SUP}/com/ecmcDataLink.cpp"
  "${SUP}/com/ecmcDataAccess.cpp"
  "${SUP}/com/ecmcDataItem.cpp")
target_include_directories(monitorTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/plc-test/offline"
  "${SUP}/motion" "${SUP}/main" "${SUP}/com" "${SUP}/misc" "${SUP}/ethercat")
target_compile_options(monitorTests PRIVATE -Wall -Wextra
  -fsanitize=address,undefined,vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(monitorTests PRIVATE -fsanitize=address,undefined,vptr)
enable_testing()
add_test(NAME monitor COMMAND monitorTests)
set_tests_properties(monitor PROPERTIES TIMEOUT 60)
