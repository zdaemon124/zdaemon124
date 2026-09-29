# Unity project import test (no window / GPU). Also converts real Unity assets:
#   UnityImportTests --convert <Assets dir> <scene.unity|prefab.prefab> [out.zscene]
add_executable(UnityImportTests main.cpp)
target_link_libraries(UnityImportTests PRIVATE IndeetsEngine)
set_target_properties(UnityImportTests PROPERTIES FOLDER "Tests")
add_test(NAME UnityImportTests COMMAND UnityImportTests ${CMAKE_CURRENT_SOURCE_DIR}/Project)
ie_deploy_managed(UnityImportTests)
