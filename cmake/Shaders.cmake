# GLSL -> SPIR-V compilation. Uses glslc from the Vulkan SDK, falls back to glslangValidator.
find_program(ZE_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
find_program(ZE_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT ZE_GLSLC AND NOT ZE_GLSLANG)
    message(FATAL_ERROR "No shader compiler found. Install the Vulkan SDK: https://vulkan.lunarg.com/sdk/home")
endif()

# ze_compile_shaders(<target> <output dir> SOURCES <shader files...> [INCLUDES <included files...>])
function(ze_compile_shaders TARGET OUT_DIR)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES" ${ARGN})
    set(SPV_FILES)
    foreach(SRC ${ARG_SOURCES})
        get_filename_component(NAME ${SRC} NAME)
        set(SPV ${OUT_DIR}/${NAME}.spv)
        if(ZE_GLSLC)
            set(CMD ${ZE_GLSLC} --target-env=vulkan1.3 -O -o ${SPV} ${SRC})
        else()
            set(CMD ${ZE_GLSLANG} -V --target-env vulkan1.3 -o ${SPV} ${SRC})
        endif()
        add_custom_command(
            OUTPUT ${SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}
            COMMAND ${CMD}
            DEPENDS ${SRC} ${ARG_INCLUDES}
            COMMENT "Compiling shader ${NAME}"
            VERBATIM)
        list(APPEND SPV_FILES ${SPV})
    endforeach()
    add_custom_target(${TARGET} DEPENDS ${SPV_FILES} SOURCES ${ARG_SOURCES} ${ARG_INCLUDES})
    set_target_properties(${TARGET} PROPERTIES FOLDER "Engine")
endfunction()

# Copies compiled engine shaders next to an executable (<exe dir>/shaders).
function(ze_deploy_shaders EXE_TARGET)
    add_dependencies(${EXE_TARGET} ZEngineShaders)
    add_custom_command(TARGET ${EXE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${ZE_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:${EXE_TARGET}>/shaders
        VERBATIM)
endfunction()
