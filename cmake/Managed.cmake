# Managed half of the engine (Engine/ScriptCore, C#): built with the .NET SDK into
# build/managed and copied next to each executable as "Managed/".
# Without the SDK the engine still builds; C# scripting is then reported as unavailable at run time.
find_program(IE_DOTNET dotnet HINTS "$ENV{DOTNET_ROOT}" "$ENV{ProgramFiles}/dotnet")
set(IE_MANAGED_OUTPUT_DIR ${CMAKE_BINARY_DIR}/managed CACHE INTERNAL "")

function(ie_add_managed_project TARGET PROJECT_DIR)
    if(NOT IE_DOTNET)
        message(WARNING "dotnet not found: C# scripting will be unavailable. Install the .NET 8 SDK to enable it.")
        add_custom_target(${TARGET})
        return()
    endif()
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${PROJECT_DIR}/*.cs ${PROJECT_DIR}/*.csproj)
    list(FILTER SOURCES EXCLUDE REGEX "/(obj|bin)/")
    set(STAMP ${IE_MANAGED_OUTPUT_DIR}/IndeetsEngine.ScriptCore.dll)
    add_custom_command(
        OUTPUT ${STAMP}
        COMMAND ${IE_DOTNET} build ${PROJECT_DIR} -c Release -o ${IE_MANAGED_OUTPUT_DIR} --nologo -v quiet
                "-p:IE_BUILD_DIR=${CMAKE_BINARY_DIR}/managed-obj"
        COMMAND ${CMAKE_COMMAND} -E touch ${STAMP}
        DEPENDS ${SOURCES}
        COMMENT "Building managed engine (C#)"
        VERBATIM)
    add_custom_target(${TARGET} DEPENDS ${STAMP} SOURCES ${SOURCES})
    set_target_properties(${TARGET} PROPERTIES FOLDER "Engine")
endfunction()

# Copies the managed engine next to an executable (<exe dir>/Managed). A separate target (not a
# POST_BUILD step) so a C#-only change is deployed even when the executable itself is up to date.
function(ie_deploy_managed EXE_TARGET)
    if(NOT IE_DOTNET)
        return()
    endif()
    add_custom_target(${EXE_TARGET}Managed
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${IE_MANAGED_OUTPUT_DIR} $<TARGET_FILE_DIR:${EXE_TARGET}>/Managed
        DEPENDS IndeetsEngineManaged
        VERBATIM)
    set_target_properties(${EXE_TARGET}Managed PROPERTIES FOLDER "Engine")
    add_dependencies(${EXE_TARGET} ${EXE_TARGET}Managed)
endfunction()
