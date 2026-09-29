file(GLOB ZE_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

add_executable(Editor ${ZE_EDITOR_SOURCES})
target_link_libraries(Editor PRIVATE ZEngine)
set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "ZEngine-Editor"
    VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Editor>
    FOLDER "Apps")
if(MSVC)
    # Runs as a normal windowed app in Release (no console window).
    set_target_properties(Editor PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()
ze_deploy_shaders(Editor)
