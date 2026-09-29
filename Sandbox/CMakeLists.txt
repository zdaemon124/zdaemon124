add_executable(Sandbox main.cpp)
target_link_libraries(Sandbox PRIVATE IndeetsEngine)
ie_deploy_shaders(Sandbox)
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Sandbox>
    FOLDER "Apps")
