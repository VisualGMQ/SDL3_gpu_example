add_executable(05_misc main.cpp shader.vert shader.frag)
target_link_libraries(05_misc PRIVATE SDL3::SDL3 stb_image glm::glm)
set_target_properties(05_misc
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
compile_shader(shader.vert vert.spv)
compile_shader(shader.frag frag.spv)
copy_sdl_dll(05_misc)