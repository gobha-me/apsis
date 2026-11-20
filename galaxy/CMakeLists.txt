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
