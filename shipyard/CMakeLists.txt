cmake_minimum_required(VERSION 3.20)
project(apsis_shipyard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
add_library(apsis-shipyard-miniz STATIC
  vendor/miniz/miniz.c vendor/miniz/miniz_tdef.c
  vendor/miniz/miniz_tinfl.c vendor/miniz/miniz_zip.c)
target_include_directories(apsis-shipyard-miniz SYSTEM PUBLIC vendor/miniz)
target_compile_definitions(apsis-shipyard-miniz PUBLIC
  MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ZLIB_APIS
  MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0)
set_target_properties(apsis-shipyard-miniz PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_library(apsis-shipyard src/json.cpp src/schema.cpp src/rules.cpp src/import.cpp
  src/structure.cpp src/validate.cpp src/dock.cpp src/perf.cpp src/perf_calculation.cpp
  src/render.cpp)
add_library(apsis::shipyard ALIAS apsis-shipyard)
target_include_directories(apsis-shipyard PUBLIC include)
target_include_directories(apsis-shipyard SYSTEM PUBLIC vendor)
target_link_libraries(apsis-shipyard PRIVATE OpenSSL::Crypto apsis-shipyard-miniz)
target_compile_features(apsis-shipyard PUBLIC cxx_std_23)
target_compile_options(apsis-shipyard PRIVATE -Wall -Wextra -Wpedantic)

option(BUILD_TESTING "Build shipyard tests" OFF)
include(CTest)
if(BUILD_TESTING)
  foreach(suite IN ITEMS rules migration input json_numeric validator dock pending
      perf perf_pending perf_scenario perf_issue_cap perf_review
      issue_cap loader_regression schema_boundary structure_regression
      dock_regression validator_cap loader_r2 structure_r2 hangar_r2 validator_r2_cap)
    add_executable(shipyard-${suite}-tests tests/${suite}_tests.cpp)
    target_link_libraries(shipyard-${suite}-tests PRIVATE apsis::shipyard)
    target_compile_definitions(shipyard-${suite}-tests PRIVATE
      SHIPYARD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(shipyard-${suite}-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME shipyard-${suite} COMMAND shipyard-${suite}-tests)
    set_tests_properties(shipyard-${suite} PROPERTIES TIMEOUT 120)
  endforeach()
  target_link_libraries(shipyard-migration-tests PRIVATE OpenSSL::Crypto)
  add_test(NAME shipyard-formula-nesting
    COMMAND shipyard-rules-tests --formula-nesting)
  set_tests_properties(shipyard-formula-nesting PROPERTIES TIMEOUT 120)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  add_executable(shipyard-render-driver tests/render_driver.cpp)
  target_link_libraries(shipyard-render-driver PRIVATE apsis::shipyard)
  target_compile_definitions(shipyard-render-driver PRIVATE
    SHIPYARD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(shipyard-render-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
  foreach(suite IN ITEMS Artifacts Geometry Bounds)
    add_test(NAME shipyard-render-${suite}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_tests.py
        --driver $<TARGET_FILE:shipyard-render-driver>
        --source ${CMAKE_CURRENT_SOURCE_DIR} --suite ${suite})
    set_tests_properties(shipyard-render-${suite} PROPERTIES TIMEOUT 120)
  endforeach()
  add_executable(shipyard-render-argus-driver tests/render_argus_driver.cpp)
  target_link_libraries(shipyard-render-argus-driver PRIVATE apsis::shipyard)
  target_compile_definitions(shipyard-render-argus-driver PRIVATE
    SHIPYARD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(shipyard-render-argus-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME shipyard-render-Argus
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_argus_tests.py
      --driver $<TARGET_FILE:shipyard-render-argus-driver>
      --source ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(shipyard-render-Argus PROPERTIES TIMEOUT 120)
  add_test(NAME shipyard-render-MullBoundaries
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_mull_tests.py
      --driver $<TARGET_FILE:shipyard-render-argus-driver>
      --source ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(shipyard-render-MullBoundaries PROPERTIES TIMEOUT 120)
endif()
