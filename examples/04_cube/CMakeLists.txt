add_executable(04_cube main.cpp shader.vert shader.frag)
target_link_libraries(04_cube PRIVATE SDL3::SDL3 stb_image glm::glm)
set_target_properties(04_cube
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
compile_shader(shader.vert vert.spv)
compile_shader(shader.frag frag.spv)
copy_sdl_dll(04_cube)