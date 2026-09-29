file(GLOB_RECURSE ZE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

add_library(ZEngine STATIC ${ZE_SOURCES})
target_include_directories(ZEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(ZEngine PUBLIC volk vma glm_headers glfw imgui json_headers stb_headers Jolt)
target_compile_definitions(ZEngine PUBLIC
    GLFW_INCLUDE_NONE
    $<$<CONFIG:Debug>:ZE_DEBUG>)
if(WIN32)
    target_compile_definitions(ZEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    target_compile_options(ZEngine PUBLIC /utf-8 /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(ZEngine PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${ZE_SOURCES})
set_target_properties(ZEngine PROPERTIES FOLDER "Engine")

# Engine shaders -> build/shaders/*.spv
file(GLOB ZE_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.frag)
file(GLOB ZE_SHADER_INCLUDES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.glsl)
set(ZE_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders CACHE INTERNAL "")
ze_compile_shaders(ZEngineShaders ${ZE_SHADER_OUTPUT_DIR}
    SOURCES ${ZE_SHADER_SOURCES}
    INCLUDES ${ZE_SHADER_INCLUDES})
add_dependencies(ZEngine ZEngineShaders)
