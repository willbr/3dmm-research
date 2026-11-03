include(ImGuiDependency)
add_library(inspector-imgui ALIAS imgui-sdl3)
add_executable(three-mm-inspector main.cpp)
set_target_properties(three-mm-inspector PROPERTIES OUTPUT_NAME 3mm-inspector)
target_link_libraries(three-mm-inspector PRIVATE inspector-data inspector-imgui ThreeMM::Preview)
# Legacy diagnostics and native types stay behind the archive library.
if(BUILD_TESTING)
  add_executable(inspector-data-test ../../tests/inspector_data_test.cpp)
  target_link_libraries(inspector-data-test PRIVATE inspector-data)
  add_test(NAME inspector-data COMMAND ${CMAKE_COMMAND}
    -DTEST_EXE=$<TARGET_FILE:inspector-data-test> -DFIXTURE_EXE=$<TARGET_FILE:three-mm-fixture>
    -DGUI_EXE=$<TARGET_FILE:three-mm-inspector>
    -DGST_FIXTURE_EXE=$<TARGET_FILE:inspector-gst-fixture>
    -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${PROJECT_SOURCE_DIR}/tests/check_inspector.cmake)
  add_dependencies(three-mm-inspector three-mm-fixture inspector-data-test inspector-gst-fixture)
endif()
