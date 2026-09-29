# Third-party dependencies, downloaded at configure time.
# Only the Vulkan SDK (for glslc and validation layers) must be installed on the system.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Header-only / manually built libraries: SOURCE_SUBDIR points to a missing folder
# so FetchContent only downloads them without calling add_subdirectory.
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.3.296.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _none)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.296.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _none)
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.2.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _none)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _none)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(vulkan_headers volk vma glm glfw)

# volk: Vulkan function loader (no link against vulkan-1.lib needed).
add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk PUBLIC ${volk_SOURCE_DIR} ${vulkan_headers_SOURCE_DIR}/include)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ${vma_SOURCE_DIR}/include)
target_compile_definitions(vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

add_library(glm_headers INTERFACE)
target_include_directories(glm_headers SYSTEM INTERFACE ${glm_SOURCE_DIR})
target_compile_definitions(glm_headers INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)

set_target_properties(volk glfw PROPERTIES FOLDER "ThirdParty")
