cmake_minimum_required(VERSION 3.20)
project(apsis_shipyard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
add_library(shipyard-utf8proc STATIC vendor/utf8proc/utf8proc.c)
set_target_properties(shipyard-utf8proc PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_definitions(shipyard-utf8proc PUBLIC UTF8PROC_STATIC)
target_include_directories(shipyard-utf8proc SYSTEM PUBLIC vendor/utf8proc)
add_library(apsis-shipyard src/json.cpp src/schema.cpp src/rules.cpp src/import.cpp
  src/structure.cpp src/validate.cpp src/dock.cpp src/perf.cpp src/perf_calculation.cpp
  src/authoring.cpp src/name_text.cpp)
add_library(apsis::shipyard ALIAS apsis-shipyard)
target_include_directories(apsis-shipyard PUBLIC include)
target_include_directories(apsis-shipyard SYSTEM PUBLIC vendor)
target_link_libraries(apsis-shipyard PRIVATE OpenSSL::Crypto shipyard-utf8proc)
target_compile_features(apsis-shipyard PUBLIC cxx_std_23)
target_compile_options(apsis-shipyard PRIVATE -Wall -Wextra -Wpedantic)

option(BUILD_TESTING "Build shipyard tests" OFF)
include(CTest)
if(BUILD_TESTING)
  foreach(suite IN ITEMS rules migration input json_numeric validator dock pending
      perf perf_pending perf_scenario perf_issue_cap perf_review
      issue_cap loader_regression schema_boundary structure_regression
      dock_regression validator_cap authoring nits_owner
      authoring_argus
      loader_r2 structure_r2 hangar_r2 validator_r2_cap)
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
endif()
