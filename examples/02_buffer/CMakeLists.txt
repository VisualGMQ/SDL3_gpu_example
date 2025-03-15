add_executable(02_buffer main.cpp shader.vert shader.frag)
target_link_libraries(02_buffer PRIVATE SDL3::SDL3)
set_target_properties(02_buffer
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
compile_shader(shader.vert vert.spv)
compile_shader(shader.frag frag.spv)
copy_sdl_dll(02_buffer)