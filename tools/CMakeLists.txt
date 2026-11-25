cmake_minimum_required(VERSION 3.25)
project(apsis_sampling_tools LANGUAGES CXX)

# Keep this build entry point outside the replaceable sim tree.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sim" sim EXCLUDE_FROM_ALL)
add_executable(rescue_debt_seed rescue_debt_seed.cpp)
target_compile_features(rescue_debt_seed PRIVATE cxx_std_23)
target_compile_options(rescue_debt_seed PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rescue_debt_seed PRIVATE apsis_pilot)

# Test adapters stay outside the replaceable imported simulation tree.
add_executable(sim_invariant_driver sim_invariant_driver.cpp)
target_compile_features(sim_invariant_driver PRIVATE cxx_std_23)
target_compile_options(sim_invariant_driver PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(sim_invariant_driver PRIVATE apsis_pilot)
