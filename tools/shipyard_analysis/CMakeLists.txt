cmake_minimum_required(VERSION 3.20)
project(apsis_shipyard_analysis LANGUAGES CXX)

# Analysis-only entry point; do not alter the independently maintained library.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shipyard" shipyard EXCLUDE_FROM_ALL)
add_executable(shipyard-validate validate.cpp)
target_link_libraries(shipyard-validate PRIVATE apsis::shipyard)
target_compile_features(shipyard-validate PRIVATE cxx_std_23)
target_compile_options(shipyard-validate PRIVATE -Wall -Wextra -Wpedantic -Werror)
