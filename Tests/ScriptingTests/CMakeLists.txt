# Headless C# scripting test (no window / GPU): `ScriptingTests [scripts dir]`, also run by CTest.
add_executable(ScriptingTests main.cpp)
target_link_libraries(ScriptingTests PRIVATE IndeetsEngine)
ie_deploy_managed(ScriptingTests)
set_target_properties(ScriptingTests PROPERTIES FOLDER "Tests")
if(IE_DOTNET)
    add_test(NAME ScriptingTests COMMAND ScriptingTests ${CMAKE_CURRENT_SOURCE_DIR}/Scripts)
endif()
