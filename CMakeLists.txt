cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 12)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(na-engine LANGUAGES C CXX VERSION 0.1)

set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES})
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})

# Compile Options
add_compile_options(-O -Werror -Wall -Wextra -Wconversion -std=c++14 -pedantic)

# files to compile
add_executable(driver_c ./src/PRNG.cpp ./src/driver.cpp ./src/ObjectAllocator.cpp)
