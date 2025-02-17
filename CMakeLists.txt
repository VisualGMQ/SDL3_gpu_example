cmake_minimum_required(VERSION 3.25)

project(SDL3_gpu_example)

find_program(GLSLC_PROG glslc)

if (NOT GLSLC_PROG)
    message(ERROR "Can't find glslc! Please pass glslc path to GLSLC_PROG variable")
endif()

include(cmake/copy_dll.cmake)
include(cmake/compile_shader.cmake)

add_subdirectory(SDL3)
add_subdirectory(examples)