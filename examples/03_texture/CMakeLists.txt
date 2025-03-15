add_executable(03_texture main.cpp shader.vert shader.frag)
target_link_libraries(03_texture PRIVATE SDL3::SDL3 stb_image)
set_target_properties(03_texture
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
compile_shader(shader.vert vert.spv)
compile_shader(shader.frag frag.spv)
copy_sdl_dll(03_texture)