cmake_minimum_required(VERSION 3.25)
project(apsis_sim_tests LANGUAGES CXX)

# Independent entry point: keep the imported simulation tree replaceable.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../sim" sim EXCLUDE_FROM_ALL)
foreach(adapter IN ITEMS sim_replay_driver sim_input_properties)
  add_executable(${adapter} "../${adapter}.cpp")
  target_compile_features(${adapter} PRIVATE cxx_std_23)
  target_compile_options(${adapter} PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(${adapter} PRIVATE apsis_pilot)
endforeach()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME sim-replay-corpus
    COMMAND "${CMAKE_COMMAND}" -E env
      "APSIS_SIM_REPLAY_DRIVER=$<TARGET_FILE:sim_replay_driver>"
      "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/.." -p test_sim_replay.py -v)
  set_tests_properties(sim-replay-corpus PROPERTIES TIMEOUT 45)
  add_test(NAME sim-input-properties
    COMMAND "${CMAKE_COMMAND}" -E env
      "APSIS_SIM_INPUT_PROPERTIES=$<TARGET_FILE:sim_input_properties>"
      "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/.." -p test_sim_input_properties.py -v)
  set_tests_properties(sim-input-properties PROPERTIES TIMEOUT 20)
endif()
