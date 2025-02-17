macro(copy_sdl_dll target_name)
    add_custom_command(TARGET ${target_name}
     POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${target_name}>
     COMMENT "copying SDL3 dll to target ${target_name}"
     VERBATIM)
endmacro()