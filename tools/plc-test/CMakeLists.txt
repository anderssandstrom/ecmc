cmake_minimum_required(VERSION 3.16)
project(ecmcPlcTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ECMC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SUP "${ECMC_ROOT}/devEcmcSup")
foreach(source exprtkIF.cpp exprtkWrap.cpp exprtkImp.cpp exprtk/exprtk.hpp)
  if(NOT EXISTS "${ECMC_ROOT}/exprtkSupport/${source}")
    message(FATAL_ERROR
      "Missing exprtkSupport/${source}. From the ECMC repository root, run: "
      "git submodule update --init -- exprtkSupport")
  endif()
endforeach()
add_executable(ecmcPlcTest
  main.cpp
  offline/motion_support.cpp
  "${ECMC_ROOT}/exprtkSupport/exprtkIF.cpp"
  "${ECMC_ROOT}/exprtkSupport/exprtkWrap.cpp"
  "${ECMC_ROOT}/exprtkSupport/exprtkImp.cpp"
  "${SUP}/main/ecmcError.cpp"
  "${SUP}/misc/ecmcDataStorage.cpp")
foreach(source ecmcAxisBase ecmcAxisVirt ecmcAxisData ecmcAxisSequencer
               ecmcEncoder ecmcMonitor ecmcTrajectoryBase ecmcTrajectoryTrapetz
               ecmcFilter ecmcMotionUtils ecmcPositionCompare ecmcPIDController
               ecmcAxisPVTSequence)
  target_sources(ecmcPlcTest PRIVATE "${SUP}/motion/${source}.cpp")
endforeach()
# Keep these adapters private: IOC builds must always use the real asyn headers.
target_include_directories(ecmcPlcTest PRIVATE
  offline "${ECMC_ROOT}/exprtkSupport"
  "${SUP}/main" "${SUP}/misc" "${SUP}/plc" "${SUP}/com" "${SUP}/ethercat"
  "${SUP}/motion" "${SUP}/sequence")
target_compile_options(ecmcPlcTest PRIVATE -Wall -Wextra)
option(ECMC_PLC_TEST_SANITIZERS "Check the standalone executable with ASan/UBSan" OFF)
if(ECMC_PLC_TEST_SANITIZERS)
  target_compile_options(ecmcPlcTest PRIVATE -fsanitize=address,undefined,vptr
    -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(ecmcPlcTest PRIVATE -fsanitize=address,undefined,vptr)
endif()
include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    foreach(group runner storage ethercat lut motion motion_safety)
      add_test(NAME plc-${group} COMMAND "${Python3_EXECUTABLE}"
        "${ECMC_ROOT}/tests/plc/${group}_checks.py" $<TARGET_FILE:ecmcPlcTest>)
      set_tests_properties(plc-${group} PROPERTIES TIMEOUT 60)
    endforeach()
  endif()
endif()
