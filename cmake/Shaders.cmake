# GLSL -> SPIR-V compilation. Uses glslc from the Vulkan SDK, falls back to glslangValidator.
find_program(IE_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
find_program(IE_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT IE_GLSLC AND NOT IE_GLSLANG)
    message(FATAL_ERROR "No shader compiler found. Install the Vulkan SDK: https://vulkan.lunarg.com/sdk/home")
endif()

# ie_compile_shaders(<target> <output dir> SOURCES <shader files...> [INCLUDES <included files...>])
function(ie_compile_shaders TARGET OUT_DIR)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES" ${ARGN})
    set(SPV_FILES)
    foreach(SRC ${ARG_SOURCES})
        get_filename_component(NAME ${SRC} NAME)
        set(SPV ${OUT_DIR}/${NAME}.spv)
        if(IE_GLSLC)
            set(CMD ${IE_GLSLC} --target-env=vulkan1.3 -O -o ${SPV} ${SRC})
        else()
            set(CMD ${IE_GLSLANG} -V --target-env vulkan1.3 -o ${SPV} ${SRC})
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
function(ie_deploy_shaders EXE_TARGET)
    add_dependencies(${EXE_TARGET} IndeetsEngineShaders)
    add_custom_command(TARGET ${EXE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${IE_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:${EXE_TARGET}>/shaders
        VERBATIM)
endfunction()
