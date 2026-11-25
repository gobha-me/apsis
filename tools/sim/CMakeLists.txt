cmake_minimum_required(VERSION 3.25)
project(apsis_balance_tools LANGUAGES CXX)
# Independent entry point: never modifies the imported simulation build.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../sim" sim EXCLUDE_FROM_ALL)
add_executable(balance_seed balance_seed.cpp)
target_compile_features(balance_seed PRIVATE cxx_std_23)
target_compile_options(balance_seed PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(balance_seed PRIVATE apsis_pilot)
