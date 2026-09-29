file(GLOB IE_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

add_executable(Editor ${IE_EDITOR_SOURCES})
target_link_libraries(Editor PRIVATE IndeetsEngine)
set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "IndeetsEngine-Editor"
    VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Editor>
    FOLDER "Apps")
if(MSVC)
    # Runs as a normal windowed app in Release (no console window).
    set_target_properties(Editor PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()
ie_deploy_shaders(Editor)
