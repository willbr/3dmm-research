include(ImGuiDependency)
add_library(inspector-imgui ALIAS imgui-sdl3)
add_executable(mm-inspector main.cpp)
set_target_properties(mm-inspector PROPERTIES OUTPUT_NAME 3mm-inspector)
target_link_libraries(mm-inspector PRIVATE inspector-data inspector-imgui MM::Preview)
# Legacy diagnostics and native types stay behind the archive library.
if(BUILD_TESTING)
  add_executable(inspector-data-test ../../tests/inspector_data_test.cpp)
  target_link_libraries(inspector-data-test PRIVATE inspector-data)
  add_test(NAME inspector-data COMMAND ${CMAKE_COMMAND}
    -DTEST_EXE=$<TARGET_FILE:inspector-data-test> -DFIXTURE_EXE=$<TARGET_FILE:mm-fixture>
    -DGUI_EXE=$<TARGET_FILE:mm-inspector>
    -DGST_FIXTURE_EXE=$<TARGET_FILE:inspector-gst-fixture>
    -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${PROJECT_SOURCE_DIR}/tests/check_inspector.cmake)
  add_dependencies(mm-inspector mm-fixture inspector-data-test inspector-gst-fixture)
endif()
