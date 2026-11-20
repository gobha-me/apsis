cmake_minimum_required(VERSION 3.20)
project(apsis_galaxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(apsis-galaxy src/map.cpp)
add_library(apsis::galaxy ALIAS apsis-galaxy)
target_include_directories(apsis-galaxy PUBLIC include)
target_compile_features(apsis-galaxy PUBLIC cxx_std_23)
target_compile_options(apsis-galaxy PRIVATE -Wall -Wextra -Wpedantic)

add_executable(galaxy-cli src/cli.cpp)
target_link_libraries(galaxy-cli PRIVATE apsis::galaxy)
target_compile_options(galaxy-cli PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

  add_executable(galaxy-invariance-tests tests/invariance.cpp)
  target_link_libraries(galaxy-invariance-tests PRIVATE apsis::galaxy Threads::Threads)
  target_compile_options(galaxy-invariance-tests PRIVATE -Wall -Wextra -Wpedantic)

  add_executable(galaxy-map-probe tests/map_probe.cpp)
  target_link_libraries(galaxy-map-probe PRIVATE apsis::galaxy)
  target_compile_options(galaxy-map-probe PRIVATE -Wall -Wextra -Wpedantic)

  add_test(NAME galaxy-invariance COMMAND galaxy-invariance-tests)
  add_test(NAME galaxy-cli-parity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
      --cli "$<TARGET_FILE:galaxy-cli>" --probe "$<TARGET_FILE:galaxy-map-probe>")
  set_tests_properties(galaxy-invariance galaxy-cli-parity PROPERTIES TIMEOUT 120)
endif()
