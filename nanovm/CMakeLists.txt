cmake_minimum_required(VERSION 3.16)

project(nanovm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)




add_library(nanovm
    vmop.hpp
    exec.cpp
    Readme.md)

# target_compile_options(nanovm PUBLIC "-fsanitize=address")
# target_link_options(nanovm PUBLIC "-fsanitize=address")



add_library(utils
    runtime_compiler.hpp
    utils.hpp utils.cpp)



add_executable(dbg
    dbg.cpp
    factorial.nvma)

configure_file("${CMAKE_SOURCE_DIR}/factorial.nvma"
               "${CMAKE_BINARY_DIR}/factorial.nvma")

target_link_libraries(dbg PUBLIC nanovm utils)



add_executable(tests
    tests.cpp
    isatest.nvma
    isatest_input.json)

configure_file("${CMAKE_SOURCE_DIR}/isatest.nvma"
               "${CMAKE_BINARY_DIR}/isatest.nvma")

configure_file("${CMAKE_SOURCE_DIR}/isatest_input.json"
               "${CMAKE_BINARY_DIR}/isatest_input.json")

target_link_libraries(tests PUBLIC nanovm utils)



add_executable(compile
    compile.cpp)

target_link_libraries(compile PUBLIC nanovm utils)
