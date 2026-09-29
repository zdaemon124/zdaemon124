cmake_minimum_required(VERSION 3.24)
project(IndeetsEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# All executables land in build/bin (build/bin/<Config> for Visual Studio).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Static MSVC runtime: the built .exe runs without installing the VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

add_subdirectory(Engine)
add_subdirectory(Sandbox)
add_subdirectory(Editor)

if(MSVC)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Editor)
endif()
