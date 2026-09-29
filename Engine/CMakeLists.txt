file(GLOB_RECURSE IE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

add_library(IndeetsEngine STATIC ${IE_SOURCES})
target_include_directories(IndeetsEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(IndeetsEngine PUBLIC volk vma glm_headers glfw imgui json_headers stb_headers ufbx cgltf_headers Jolt)
target_compile_definitions(IndeetsEngine PUBLIC
    GLFW_INCLUDE_NONE
    $<$<CONFIG:Debug>:IE_DEBUG>)
if(WIN32)
    target_compile_definitions(IndeetsEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    target_compile_options(IndeetsEngine PUBLIC /utf-8 /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(IndeetsEngine PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${IE_SOURCES})
set_target_properties(IndeetsEngine PROPERTIES FOLDER "Engine")

# Engine shaders -> build/shaders/*.spv
file(GLOB IE_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.frag)
file(GLOB IE_SHADER_INCLUDES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.glsl)
set(IE_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders CACHE INTERNAL "")
ie_compile_shaders(IndeetsEngineShaders ${IE_SHADER_OUTPUT_DIR}
    SOURCES ${IE_SHADER_SOURCES}
    INCLUDES ${IE_SHADER_INCLUDES})
add_dependencies(IndeetsEngine IndeetsEngineShaders)
