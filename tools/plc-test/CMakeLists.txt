cmake_minimum_required(VERSION 3.16)
project(ecmcPlcTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ECMC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SUP "${ECMC_ROOT}/devEcmcSup")
add_executable(ecmcPlcTest
  main.cpp
  "${ECMC_ROOT}/exprtkSupport/exprtkIF.cpp"
  "${ECMC_ROOT}/exprtkSupport/exprtkWrap.cpp"
  "${ECMC_ROOT}/exprtkSupport/exprtkImp.cpp"
  "${SUP}/main/ecmcError.cpp"
  "${SUP}/misc/ecmcDataStorage.cpp")
# Keep these adapters private: IOC builds must always use the real asyn headers.
target_include_directories(ecmcPlcTest PRIVATE
  offline "${ECMC_ROOT}/exprtkSupport"
  "${SUP}/main" "${SUP}/misc" "${SUP}/plc" "${SUP}/com" "${SUP}/ethercat")
target_compile_options(ecmcPlcTest PRIVATE -Wall -Wextra)
include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME plc-runner COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.py" $<TARGET_FILE:ecmcPlcTest>)
    add_test(NAME plc-libraries COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_libraries.py" $<TARGET_FILE:ecmcPlcTest>)
    set_tests_properties(plc-runner plc-libraries PROPERTIES TIMEOUT 60)
  endif()
endif()
