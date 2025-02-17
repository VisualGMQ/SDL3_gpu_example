add_executable(01_triangle main.cpp shader.vert shader.frag)
target_link_libraries(01_triangle PRIVATE SDL3::SDL3)
set_target_properties(01_triangle
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
compile_shader(shader.vert vert.spv)
compile_shader(shader.frag frag.spv)
copy_sdl_dll(01_triangle)