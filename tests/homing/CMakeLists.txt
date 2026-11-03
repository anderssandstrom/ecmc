cmake_minimum_required(VERSION 3.16)
project(ecmcHomingTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SUP "${ROOT}/devEcmcSup")
add_executable(homingTests homing_checks.cpp "${ROOT}/tools/plc-test/offline/motion_support.cpp"
  "${SUP}/main/ecmcError.cpp" "${SUP}/com/ecmcDataLink.cpp"
  "${SUP}/com/ecmcDataAccess.cpp" "${SUP}/com/ecmcDataItem.cpp")
foreach(source ecmcAxisBase ecmcAxisVirt ecmcAxisData ecmcAxisSequencer ecmcEncoder
               ecmcMonitor ecmcTrajectoryBase ecmcTrajectoryTrapetz ecmcFilter
               ecmcMotionUtils ecmcPositionCompare ecmcPIDController ecmcAxisPVTSequence)
  target_sources(homingTests PRIVATE "${SUP}/motion/${source}.cpp")
endforeach()
target_include_directories(homingTests PRIVATE "${ROOT}/tools/plc-test/offline"
  "${SUP}/motion" "${SUP}/main" "${SUP}/misc" "${SUP}/com" "${SUP}/ethercat" "${SUP}/sequence")
target_compile_options(homingTests PRIVATE -Wall -Wextra -UNDEBUG
  -fsanitize=address,undefined,vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(homingTests PRIVATE -fsanitize=address,undefined,vptr)
