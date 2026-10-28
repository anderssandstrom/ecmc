cmake_minimum_required(VERSION 3.16)
project(ecmcDriveBrakeTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SUP "${CMAKE_CURRENT_SOURCE_DIR}/../../devEcmcSup")
add_executable(driveBrakeTests brake_checks.cpp support.cpp
  "${SUP}/motion/ecmcDriveBase.cpp"
  "${SUP}/motion/ecmcDriveStepper.cpp"
  "${SUP}/motion/ecmcDriveDS402.cpp"
  "${SUP}/motion/ecmcAxisData.cpp"
  "${SUP}/main/ecmcError.cpp")
target_include_directories(driveBrakeTests PRIVATE offline
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/plc-test/offline"
  "${SUP}/motion" "${SUP}/main" "${SUP}/com" "${SUP}/misc" "${SUP}/ethercat")
target_compile_options(driveBrakeTests PRIVATE -Wall -Wextra -UNDEBUG
  -fsanitize=address,undefined,vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(driveBrakeTests PRIVATE -fsanitize=address,undefined,vptr)
enable_testing()
add_test(NAME drive-brake COMMAND driveBrakeTests)
set_tests_properties(drive-brake PROPERTIES TIMEOUT 60)
