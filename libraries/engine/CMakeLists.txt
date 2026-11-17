cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MMEngine LANGUAGES C)
  include(CTest)
endif()

add_library(mm-engine-c89 STATIC src/playback.c src/scroll.c src/read_stream.c src/movie_data.c src/movie_owner.c src/movie_title.c src/movie_root.c src/scene_data.c src/actor_data.c src/actor_track.c src/actor_fixed.c src/actor_route.c src/actor_state.c src/actor_frame.c src/actor_timeline.c src/actor_seek.c src/actor_math.c src/actor_pose.c src/actor_effect.c src/actor_sound.c src/actor_edit.c src/actor_sound_edit.c src/rollcall_storage.c src/chunky_file.c src/string_table.c src/text.c src/collection_storage.c src/collections.c src/collection_headers.c src/data_stream.c src/codec.c src/piece_table.c src/block_io.c src/block_owner.c src/file_text.c src/chunk_save.c src/chunk_heap.c src/chunk_edit.c src/chunk_graph.c src/chunk_ops.c src/chunk_metadata.c src/string_wire.c src/codec_registry.c src/chunk_forest.c src/chunk_loading.c src/chunk_storage.c src/chunk_lifecycle.c src/chunk_container.c src/chunk_context.c src/chunk_owner.c src/chunk_table.c src/tag.c src/tag_list.c src/source.c src/source_names.c src/source_discovery.c src/wide.c src/resource_cache.c src/resource_manager.c)
target_include_directories(mm-engine-c89 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_sources(mm-engine-c89 PRIVATE src/settings.c src/settings_paths.c)
target_sources(mm-engine-c89 PRIVATE src/runtime_math.c)
if(WIN32)
  set(native_runtime_source src/native_runtime_win.c)
else()
  set(native_runtime_source src/native_runtime_posix.c)
endif()
add_library(mm-native-runtime STATIC ${native_runtime_source})
target_include_directories(mm-native-runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(mm-native-runtime PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
target_link_libraries(mm-native-runtime PUBLIC mm-engine-c89)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(mm-native-runtime PUBLIC Threads::Threads)
endif()
add_executable(engine-runtime-math-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_runtime_math_test.c")
target_link_libraries(engine-runtime-math-test PRIVATE mm-engine-c89)
set_target_properties(engine-runtime-math-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-runtime-math COMMAND engine-runtime-math-test)
add_executable(engine-native-runtime-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_runtime_c_test.c")
target_link_libraries(engine-native-runtime-test PRIVATE mm-native-runtime)
set_target_properties(engine-native-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-native-runtime COMMAND engine-native-runtime-test)
set_tests_properties(engine-native-runtime PROPERTIES TIMEOUT 30)
add_executable(engine-native-runtime-provider-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_runtime_provider_test.c")
target_link_libraries(engine-native-runtime-provider-test PRIVATE mm-native-runtime)
set_target_properties(engine-native-runtime-provider-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-native-runtime-provider COMMAND engine-native-runtime-provider-test)
set_tests_properties(engine-native-runtime-provider PROPERTIES TIMEOUT 30)
add_executable(engine-settings-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_settings_test.c")
target_link_libraries(engine-settings-test PRIVATE mm-engine-c89)
set_target_properties(engine-settings-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-settings COMMAND engine-settings-test)
if(WIN32)
  set(native_users_source src/native_users_win.c)
  set(native_users_profiles ansi wide)
else()
  set(native_users_source src/native_users_posix.c)
  set(native_users_profiles posix)
endif()
foreach(profile IN LISTS native_users_profiles)
  set(users_library mm-native-users)
  if(profile STREQUAL "wide")
    set(users_library mm-native-users-wide)
  endif()
  add_library(${users_library} STATIC ${native_users_source} src/native_user_paths.c)
  target_include_directories(${users_library} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  set_target_properties(${users_library} PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  if(WIN32)
    target_link_libraries(${users_library} PUBLIC mpr shell32)
  endif()
  if(profile STREQUAL "wide")
    target_compile_definitions(${users_library} PUBLIC UNICODE _UNICODE)
  endif()
  foreach(check IN ITEMS c provider)
    add_executable(engine-native-users-${profile}-${check}-test EXCLUDE_FROM_ALL
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_users_${check}_test.c")
    target_link_libraries(engine-native-users-${profile}-${check}-test PRIVATE ${users_library})
    set_target_properties(engine-native-users-${profile}-${check}-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
    add_test(NAME engine-native-users-${profile}-${check} COMMAND engine-native-users-${profile}-${check}-test)
  endforeach()
endforeach()
if(WIN32)
  add_library(mm-native-files STATIC src/native_files_win.c)
  target_include_directories(mm-native-files PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(mm-native-files PUBLIC user32 shell32)
  set_target_properties(mm-native-files PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  add_library(mm-native-files-wide STATIC src/native_files_win.c)
  target_include_directories(mm-native-files-wide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_definitions(mm-native-files-wide PUBLIC UNICODE _UNICODE)
  target_link_libraries(mm-native-files-wide PUBLIC user32 shell32)
  set_target_properties(mm-native-files-wide PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  foreach(profile IN ITEMS ansi wide)
    add_executable(engine-native-files-${profile}-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_files_c_test.c")
    if(profile STREQUAL "wide")
      target_link_libraries(engine-native-files-${profile}-test PRIVATE mm-native-files-wide)
    else()
      target_link_libraries(engine-native-files-${profile}-test PRIVATE mm-native-files)
    endif()
    set_target_properties(engine-native-files-${profile}-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
    add_test(NAME engine-native-files-${profile} COMMAND engine-native-files-${profile}-test)
  endforeach()
endif()
if(NOT WIN32)
  add_library(mm-native-files STATIC src/native_files_posix.c)
  target_include_directories(mm-native-files PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  set_target_properties(mm-native-files PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  add_executable(engine-native-files-posix-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_files_c_test.c")
  target_link_libraries(engine-native-files-posix-test PRIVATE mm-native-files)
  set_target_properties(engine-native-files-posix-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  add_test(NAME engine-native-files-posix COMMAND engine-native-files-posix-test)
endif()
if(WIN32)
  set(native_settings_source src/native_settings_win.c)
  set(native_settings_profiles ansi wide)
else()
  set(native_settings_source src/native_settings_posix.c)
  set(native_settings_profiles posix)
endif()
foreach(profile IN LISTS native_settings_profiles)
  set(settings_library mm-native-settings)
  if(profile STREQUAL "wide")
    set(settings_library mm-native-settings-wide)
  endif()
  add_library(${settings_library} STATIC ${native_settings_source})
  target_include_directories(${settings_library} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(${settings_library} PUBLIC mm-engine-c89)
  set_target_properties(${settings_library} PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  if(WIN32)
    target_link_libraries(${settings_library} PUBLIC advapi32)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(${settings_library} PUBLIC mm-native-files Threads::Threads)
  endif()
  if(profile STREQUAL "wide")
    target_compile_definitions(${settings_library} PUBLIC UNICODE _UNICODE)
  endif()
  add_executable(engine-native-settings-${profile}-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_settings_c_test.c")
  target_link_libraries(engine-native-settings-${profile}-test PRIVATE ${settings_library})
  set_target_properties(engine-native-settings-${profile}-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  add_test(NAME engine-native-settings-${profile} COMMAND engine-native-settings-${profile}-test)
endforeach()

target_sources(mm-engine-c89 PRIVATE src/reference.c)
if(NOT WIN32)
  add_executable(engine-native-settings-posix-provider-test EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_settings_posix_test.c")
  target_link_libraries(engine-native-settings-posix-provider-test PRIVATE mm-native-settings)
  set_target_properties(engine-native-settings-posix-provider-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
  add_test(NAME engine-native-settings-posix-provider COMMAND engine-native-settings-posix-provider-test)
endif()
target_sources(mm-engine-c89 PRIVATE src/filename_enum.c)
target_sources(mm-engine-c89 PRIVATE src/filename_frame.c)
get_filename_component(engine_filename_enum_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_enum_test.c" ABSOLUTE)
add_executable(engine-filename-enum-test EXCLUDE_FROM_ALL "${engine_filename_enum_test_source}")
target_link_libraries(engine-filename-enum-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-enum-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-enum COMMAND engine-filename-enum-test)
target_sources(mm-engine-c89 PRIVATE src/filename_case.c)
get_filename_component(engine_filename_case_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_case_test.c" ABSOLUTE)
add_executable(engine-filename-case-test EXCLUDE_FROM_ALL "${engine_filename_case_test_source}")
target_link_libraries(engine-filename-case-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-case-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-case COMMAND engine-filename-case-test)
target_sources(mm-engine-c89 PRIVATE src/filename_naming.c)
get_filename_component(engine_filename_naming_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_naming_test.c" ABSOLUTE)
add_executable(engine-filename-naming-test EXCLUDE_FROM_ALL "${engine_filename_naming_test_source}")
target_link_libraries(engine-filename-naming-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-naming-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-naming COMMAND engine-filename-naming-test)
target_sources(mm-engine-c89 PRIVATE src/filename_path.c)
get_filename_component(engine_filename_path_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_path_test.c" ABSOLUTE)
add_executable(engine-filename-path-test EXCLUDE_FROM_ALL "${engine_filename_path_test_source}")
target_link_libraries(engine-filename-path-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-path-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-path COMMAND engine-filename-path-test)
target_sources(mm-engine-c89 PRIVATE src/filename_owned.c)
get_filename_component(engine_filename_owned_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_owned_test.c" ABSOLUTE)
add_executable(engine-filename-owned-test EXCLUDE_FROM_ALL "${engine_filename_owned_test_source}")
target_link_libraries(engine-filename-owned-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-owned-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-owned COMMAND engine-filename-owned-test)
target_sources(mm-engine-c89 PRIVATE src/filename_value.c)
get_filename_component(engine_filename_value_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_filename_value_test.c" ABSOLUTE)
add_executable(engine-filename-value-test EXCLUDE_FROM_ALL "${engine_filename_value_test_source}")
target_link_libraries(engine-filename-value-test PRIVATE mm-engine-c89)
set_target_properties(engine-filename-value-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-filename-value COMMAND engine-filename-value-test)
target_sources(mm-engine-c89 PRIVATE src/file_ops.c)
get_filename_component(engine_file_ops_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_file_ops_test.c" ABSOLUTE)
add_executable(engine-file-ops-test EXCLUDE_FROM_ALL "${engine_file_ops_test_source}")
target_link_libraries(engine-file-ops-test PRIVATE mm-engine-c89)
set_target_properties(engine-file-ops-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-file-ops COMMAND engine-file-ops-test)
target_sources(mm-engine-c89 PRIVATE src/file_owner.c)
get_filename_component(engine_file_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_file_owner_test.c" ABSOLUTE)
add_executable(engine-file-owner-test EXCLUDE_FROM_ALL "${engine_file_owner_test_source}")
target_link_libraries(engine-file-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-file-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-file-owner COMMAND engine-file-owner-test)
target_sources(mm-engine-c89 PRIVATE src/linked_list.c)
get_filename_component(engine_linked_list_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_linked_list_test.c" ABSOLUTE)
add_executable(engine-linked-list-test EXCLUDE_FROM_ALL "${engine_linked_list_test_source}")
target_link_libraries(engine-linked-list-test PRIVATE mm-engine-c89)
set_target_properties(engine-linked-list-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-linked-list COMMAND engine-linked-list-test)
target_sources(mm-engine-c89 PRIVATE src/handle_memory.c)
get_filename_component(engine_handle_memory_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_handle_memory_test.c" ABSOLUTE)
add_executable(engine-handle-memory-test EXCLUDE_FROM_ALL "${engine_handle_memory_test_source}")
target_link_libraries(engine-handle-memory-test PRIVATE mm-engine-c89)
set_target_properties(engine-handle-memory-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-handle-memory COMMAND engine-handle-memory-test)

target_sources(mm-engine-c89 PRIVATE src/memory_debug.c)
get_filename_component(engine_memory_debug_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_memory_debug_test.c" ABSOLUTE)
add_executable(engine-memory-debug-test EXCLUDE_FROM_ALL "${engine_memory_debug_test_source}")
target_link_libraries(engine-memory-debug-test PRIVATE mm-engine-c89)
set_target_properties(engine-memory-debug-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-memory-debug COMMAND engine-memory-debug-test)

target_sources(mm-engine-c89 PRIVATE src/object_debug.c)
get_filename_component(engine_object_debug_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_object_debug_test.c" ABSOLUTE)
add_executable(engine-object-debug-test EXCLUDE_FROM_ALL "${engine_object_debug_test_source}")
target_link_libraries(engine-object-debug-test PRIVATE mm-engine-c89)
set_target_properties(engine-object-debug-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-object-debug COMMAND engine-object-debug-test)

target_sources(mm-engine-c89 PRIVATE src/heap.c src/object_memory.c)
get_filename_component(engine_heap_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_heap_test.c" ABSOLUTE)
add_executable(engine-heap-test EXCLUDE_FROM_ALL "${engine_heap_test_source}")
target_link_libraries(engine-heap-test PRIVATE mm-engine-c89)
set_target_properties(engine-heap-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-heap COMMAND engine-heap-test)
get_filename_component(engine_object_memory_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_object_memory_test.c" ABSOLUTE)
add_executable(engine-object-memory-test EXCLUDE_FROM_ALL "${engine_object_memory_test_source}")
target_link_libraries(engine-object-memory-test PRIVATE mm-engine-c89)
set_target_properties(engine-object-memory-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-object-memory COMMAND engine-object-memory-test)

target_sources(mm-engine-c89 PRIVATE src/memory.c src/error_stack.c)
get_filename_component(engine_memory_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_memory_test.c" ABSOLUTE)
add_executable(engine-memory-test EXCLUDE_FROM_ALL "${engine_memory_test_source}")
target_link_libraries(engine-memory-test PRIVATE mm-engine-c89)
set_target_properties(engine-memory-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-memory COMMAND engine-memory-test)
get_filename_component(engine_error_stack_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_error_stack_test.c" ABSOLUTE)
add_executable(engine-error-stack-test EXCLUDE_FROM_ALL "${engine_error_stack_test_source}")
target_link_libraries(engine-error-stack-test PRIVATE mm-engine-c89)
set_target_properties(engine-error-stack-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-error-stack COMMAND engine-error-stack-test)
target_sources(mm-engine-c89 PRIVATE src/string_value.c)
target_sources(mm-engine-c89 PRIVATE src/string_format.c)
target_sources(mm-engine-c89 PRIVATE src/string_ops.c)
get_filename_component(engine_string_ops_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_string_ops_test.c" ABSOLUTE)
add_executable(engine-string-ops-test EXCLUDE_FROM_ALL "${engine_string_ops_test_source}")
target_link_libraries(engine-string-ops-test PRIVATE mm-engine-c89)
set_target_properties(engine-string-ops-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-string-ops COMMAND engine-string-ops-test)
get_filename_component(engine_string_format_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_string_format_test.c" ABSOLUTE)
add_executable(engine-string-format-test EXCLUDE_FROM_ALL "${engine_string_format_test_source}")
target_link_libraries(engine-string-format-test PRIVATE mm-engine-c89)
set_target_properties(engine-string-format-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-string-format COMMAND engine-string-format-test)
get_filename_component(engine_string_value_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_string_value_test.c" ABSOLUTE)
add_executable(engine-string-value-test EXCLUDE_FROM_ALL "${engine_string_value_test_source}")
target_link_libraries(engine-string-value-test PRIVATE mm-engine-c89)
set_target_properties(engine-string-value-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-string-value COMMAND engine-string-value-test)
get_filename_component(engine_reference_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_reference_test.c" ABSOLUTE)
add_executable(engine-reference-test EXCLUDE_FROM_ALL "${engine_reference_test_source}")
target_link_libraries(engine-reference-test PRIVATE mm-engine-c89)
set_target_properties(engine-reference-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-reference COMMAND engine-reference-test)
if(UNIX)
  target_link_libraries(mm-engine-c89 PUBLIC m)
endif()
target_sources(mm-engine-c89 PRIVATE src/bitmap_file.c)
target_sources(mm-engine-c89 PRIVATE src/depth_bitmap.c)
target_sources(mm-engine-c89 PRIVATE src/masked_bitmap.c)
target_sources(mm-engine-c89 PRIVATE src/world_buffers.c)
target_sources(mm-engine-c89 PRIVATE src/world_camera.c)
target_sources(mm-engine-c89 PRIVATE src/world_render.c)
target_sources(mm-engine-c89 PRIVATE src/world_export.c)
target_sources(mm-engine-c89 PRIVATE src/scene_owner.c)
target_sources(mm-engine-c89 PRIVATE src/scene_runtime.c)
target_sources(mm-engine-c89 PRIVATE src/scene_events.c)
target_sources(mm-engine-c89 PRIVATE src/scene_sound.c)
target_sources(mm-engine-c89 PRIVATE src/scene_sound_edit.c)
target_sources(mm-engine-c89 PRIVATE src/scene_load.c)
target_sources(mm-engine-c89 PRIVATE src/scene_store.c)
target_sources(mm-engine-c89 PRIVATE src/scene_edit.c)
target_sources(mm-engine-c89 PRIVATE src/scene_content.c)
target_sources(mm-engine-c89 PRIVATE src/scene_selection.c)
target_sources(mm-engine-c89 PRIVATE src/scene_chop.c)
target_sources(mm-engine-c89 PRIVATE src/scene_control.c)
target_sources(mm-engine-c89 PRIVATE src/scene_thumbnail.c)
target_sources(mm-engine-c89 PRIVATE src/background_camera.c)
target_sources(mm-engine-c89 PRIVATE src/background_resource.c)
target_sources(mm-engine-c89 PRIVATE src/background_mesh.c)
target_sources(mm-engine-c89 PRIVATE src/background_preview.c)
target_sources(mm-engine-c89 PRIVATE src/movie_scene.c)
target_sources(mm-engine-c89 PRIVATE src/movie_save.c)
target_sources(mm-engine-c89 PRIVATE src/movie_rollcall.c src/movie_edit.c)
target_sources(mm-engine-c89 PRIVATE src/movie_content.c)
target_sources(mm-engine-c89 PRIVATE src/movie_compose.c)
target_sources(mm-engine-c89 PRIVATE src/text_store.c)
target_sources(mm-engine-c89 PRIVATE src/text_live.c)
target_sources(mm-engine-c89 PRIVATE src/text_document.c)
target_sources(mm-engine-c89 PRIVATE src/text_materialize.c)
target_sources(mm-engine-c89 PRIVATE src/text_properties.c)
target_sources(mm-engine-c89 PRIVATE src/text_format.c)
target_sources(mm-engine-c89 PRIVATE src/text_access.c src/text_replace.c)
target_sources(mm-engine-c89 PRIVATE src/text_copy.c)
target_sources(mm-engine-c89 PRIVATE src/text_undo.c)
target_sources(mm-engine-c89 PRIVATE src/document_undo.c)
target_sources(mm-engine-c89 PRIVATE src/document_lifecycle.c)
target_sources(mm-engine-c89 PRIVATE src/document_views.c)
target_sources(mm-engine-c89 PRIVATE src/help_text.c)
target_sources(mm-engine-c89 PRIVATE src/help_display.c)
target_sources(mm-engine-c89 PRIVATE src/help_interaction.c)
target_sources(mm-engine-c89 PRIVATE src/help_balloon.c)
target_sources(mm-engine-c89 PRIVATE src/help_topic.c)
target_sources(mm-engine-c89 PRIVATE src/movie_undo.c)
get_filename_component(engine_movie_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_undo_test.c" ABSOLUTE)
add_executable(engine-movie-undo-test EXCLUDE_FROM_ALL "${engine_movie_undo_test_source}")
target_link_libraries(engine-movie-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-undo COMMAND engine-movie-undo-test)
target_sources(mm-engine-c89 PRIVATE src/text_box_edit.c src/text_box_undo.c)
get_filename_component(engine_text_box_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_box_edit_test.c" ABSOLUTE)
add_executable(engine-text-box-edit-test EXCLUDE_FROM_ALL "${engine_text_box_edit_test_source}")
target_link_libraries(engine-text-box-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-box-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-box-edit COMMAND engine-text-box-edit-test)
get_filename_component(engine_text_box_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_box_undo_test.c" ABSOLUTE)
add_executable(engine-text-box-undo-test EXCLUDE_FROM_ALL "${engine_text_box_undo_test_source}")
target_link_libraries(engine-text-box-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-box-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-box-undo COMMAND engine-text-box-undo-test)
target_sources(mm-engine-c89 PRIVATE src/text_box_runtime.c)
get_filename_component(engine_text_box_runtime_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_box_runtime_test.c" ABSOLUTE)
add_executable(engine-text-box-runtime-test EXCLUDE_FROM_ALL "${engine_text_box_runtime_test_source}")
target_link_libraries(engine-text-box-runtime-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-box-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-box-runtime COMMAND engine-text-box-runtime-test)
target_sources(mm-engine-c89 PRIVATE src/rich_text_layout.c)
get_filename_component(engine_rich_text_layout_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_rich_text_layout_test.c" ABSOLUTE)
add_executable(engine-rich-text-layout-test EXCLUDE_FROM_ALL "${engine_rich_text_layout_test_source}")
target_link_libraries(engine-rich-text-layout-test PRIVATE mm-engine-c89)
set_target_properties(engine-rich-text-layout-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-rich-text-layout COMMAND engine-rich-text-layout-test)
get_filename_component(engine_help_topic_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_help_topic_test.c" ABSOLUTE)
add_executable(engine-help-topic-test EXCLUDE_FROM_ALL "${engine_help_topic_test_source}")
target_link_libraries(engine-help-topic-test PRIVATE mm-engine-c89)
set_target_properties(engine-help-topic-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-help-topic COMMAND engine-help-topic-test)
get_filename_component(engine_help_balloon_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_help_balloon_test.c" ABSOLUTE)
add_executable(engine-help-balloon-test EXCLUDE_FROM_ALL "${engine_help_balloon_test_source}")
target_link_libraries(engine-help-balloon-test PRIVATE mm-engine-c89)
set_target_properties(engine-help-balloon-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-help-balloon COMMAND engine-help-balloon-test)
get_filename_component(engine_help_interaction_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_help_interaction_test.c" ABSOLUTE)
add_executable(engine-help-interaction-test EXCLUDE_FROM_ALL "${engine_help_interaction_test_source}")
target_link_libraries(engine-help-interaction-test PRIVATE mm-engine-c89)
set_target_properties(engine-help-interaction-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-help-interaction COMMAND engine-help-interaction-test)
get_filename_component(engine_help_display_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_help_display_test.c" ABSOLUTE)
add_executable(engine-help-display-test EXCLUDE_FROM_ALL "${engine_help_display_test_source}")
target_link_libraries(engine-help-display-test PRIVATE mm-engine-c89)
set_target_properties(engine-help-display-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-help-display COMMAND engine-help-display-test)
get_filename_component(engine_help_text_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_help_text_test.c" ABSOLUTE)
add_executable(engine-help-text-test EXCLUDE_FROM_ALL "${engine_help_text_test_source}")
target_link_libraries(engine-help-text-test PRIVATE mm-engine-c89)
set_target_properties(engine-help-text-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-help-text COMMAND engine-help-text-test)
get_filename_component(engine_document_views_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_document_views_test.c" ABSOLUTE)
add_executable(engine-document-views-test EXCLUDE_FROM_ALL "${engine_document_views_test_source}")
target_link_libraries(engine-document-views-test PRIVATE mm-engine-c89)
set_target_properties(engine-document-views-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-document-views COMMAND engine-document-views-test)
get_filename_component(engine_document_lifecycle_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_document_lifecycle_test.c" ABSOLUTE)
add_executable(engine-document-lifecycle-test EXCLUDE_FROM_ALL "${engine_document_lifecycle_test_source}")
target_link_libraries(engine-document-lifecycle-test PRIVATE mm-engine-c89)
set_target_properties(engine-document-lifecycle-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-document-lifecycle COMMAND engine-document-lifecycle-test)
get_filename_component(engine_document_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_document_undo_test.c" ABSOLUTE)
add_executable(engine-document-undo-test EXCLUDE_FROM_ALL "${engine_document_undo_test_source}")
target_link_libraries(engine-document-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-document-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-document-undo COMMAND engine-document-undo-test)
get_filename_component(engine_text_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_undo_test.c" ABSOLUTE)
add_executable(engine-text-undo-test EXCLUDE_FROM_ALL "${engine_text_undo_test_source}")
target_link_libraries(engine-text-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-undo COMMAND engine-text-undo-test)
get_filename_component(engine_text_copy_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_copy_test.c" ABSOLUTE)
add_executable(engine-text-copy-test EXCLUDE_FROM_ALL "${engine_text_copy_test_source}")
target_link_libraries(engine-text-copy-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-copy-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-copy COMMAND engine-text-copy-test)
get_filename_component(engine_text_access_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_access_test.c" ABSOLUTE)
add_executable(engine-text-access-test EXCLUDE_FROM_ALL "${engine_text_access_test_source}")
target_link_libraries(engine-text-access-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-access-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-access COMMAND engine-text-access-test)
get_filename_component(engine_text_replace_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_replace_test.c" ABSOLUTE)
add_executable(engine-text-replace-test EXCLUDE_FROM_ALL "${engine_text_replace_test_source}")
target_link_libraries(engine-text-replace-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-replace-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-replace COMMAND engine-text-replace-test)
get_filename_component(engine_text_format_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_format_test.c" ABSOLUTE)
add_executable(engine-text-format-test EXCLUDE_FROM_ALL "${engine_text_format_test_source}")
target_link_libraries(engine-text-format-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-format-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-format COMMAND engine-text-format-test)
get_filename_component(engine_text_properties_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_properties_test.c" ABSOLUTE)
add_executable(engine-text-properties-test EXCLUDE_FROM_ALL "${engine_text_properties_test_source}")
target_link_libraries(engine-text-properties-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-properties-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-properties COMMAND engine-text-properties-test)
get_filename_component(engine_text_materialize_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_materialize_test.c" ABSOLUTE)
add_executable(engine-text-materialize-test EXCLUDE_FROM_ALL "${engine_text_materialize_test_source}")
target_link_libraries(engine-text-materialize-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-materialize-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-materialize COMMAND engine-text-materialize-test)
get_filename_component(engine_text_document_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_document_test.c" ABSOLUTE)
add_executable(engine-text-document-test EXCLUDE_FROM_ALL "${engine_text_document_test_source}")
target_link_libraries(engine-text-document-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-document-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-document COMMAND engine-text-document-test)
get_filename_component(engine_text_live_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_live_test.c" ABSOLUTE)
add_executable(engine-text-live-test EXCLUDE_FROM_ALL "${engine_text_live_test_source}")
target_link_libraries(engine-text-live-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-live-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-live COMMAND engine-text-live-test)
get_filename_component(engine_text_store_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_store_test.c" ABSOLUTE)
add_executable(engine-text-store-test EXCLUDE_FROM_ALL "${engine_text_store_test_source}")
target_link_libraries(engine-text-store-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-store-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-store COMMAND engine-text-store-test)
target_sources(mm-engine-c89 PRIVATE src/preview_geometry.c src/preview_render.c src/preview_pick.c src/preview_camera.c src/preview_live.c src/preview_admission.c src/preview_actor.c src/preview_session.c src/preview_metadata.c src/preview_media.c)
get_filename_component(engine_preview_media_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_media_test.c" ABSOLUTE)
add_executable(engine-preview-media-test EXCLUDE_FROM_ALL "${engine_preview_media_test_source}")
target_link_libraries(engine-preview-media-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-media-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-media COMMAND engine-preview-media-test)
get_filename_component(engine_preview_metadata_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_metadata_test.c" ABSOLUTE)
add_executable(engine-preview-metadata-test EXCLUDE_FROM_ALL "${engine_preview_metadata_test_source}")
target_link_libraries(engine-preview-metadata-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-metadata-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-metadata COMMAND engine-preview-metadata-test)
get_filename_component(engine_preview_session_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_session_test.c" ABSOLUTE)
add_executable(engine-preview-session-test EXCLUDE_FROM_ALL "${engine_preview_session_test_source}")
target_link_libraries(engine-preview-session-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-session-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-session COMMAND engine-preview-session-test)
get_filename_component(engine_preview_actor_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_actor_test.c" ABSOLUTE)
add_executable(engine-preview-actor-test EXCLUDE_FROM_ALL "${engine_preview_actor_test_source}")
target_link_libraries(engine-preview-actor-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-actor-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-actor COMMAND engine-preview-actor-test)
get_filename_component(engine_preview_admission_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_admission_test.c" ABSOLUTE)
add_executable(engine-preview-admission-test EXCLUDE_FROM_ALL "${engine_preview_admission_test_source}")
target_link_libraries(engine-preview-admission-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-admission-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-admission COMMAND engine-preview-admission-test)
get_filename_component(engine_preview_live_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_live_test.c" ABSOLUTE)
add_executable(engine-preview-live-test EXCLUDE_FROM_ALL "${engine_preview_live_test_source}")
target_link_libraries(engine-preview-live-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-live-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-live COMMAND engine-preview-live-test)
get_filename_component(engine_preview_camera_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_camera_test.c" ABSOLUTE)
add_executable(engine-preview-camera-test EXCLUDE_FROM_ALL "${engine_preview_camera_test_source}")
target_link_libraries(engine-preview-camera-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-camera-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-camera COMMAND engine-preview-camera-test)
get_filename_component(engine_preview_pick_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_pick_test.c" ABSOLUTE)
add_executable(engine-preview-pick-test EXCLUDE_FROM_ALL "${engine_preview_pick_test_source}")
target_link_libraries(engine-preview-pick-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-pick-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-pick COMMAND engine-preview-pick-test)
get_filename_component(engine_preview_render_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_preview_render_test.c" ABSOLUTE)
add_executable(engine-preview-render-test EXCLUDE_FROM_ALL "${engine_preview_render_test_source}")
target_link_libraries(engine-preview-render-test PRIVATE mm-engine-c89)
set_target_properties(engine-preview-render-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-preview-render COMMAND engine-preview-render-test)
get_filename_component(engine_movie_compose_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_compose_test.c" ABSOLUTE)
add_executable(engine-movie-compose-test EXCLUDE_FROM_ALL "${engine_movie_compose_test_source}")
target_link_libraries(engine-movie-compose-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-compose-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-compose COMMAND engine-movie-compose-test)
get_filename_component(engine_movie_content_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_content_test.c" ABSOLUTE)
add_executable(engine-movie-content-test EXCLUDE_FROM_ALL "${engine_movie_content_test_source}")
target_link_libraries(engine-movie-content-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-content-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-content COMMAND engine-movie-content-test)
get_filename_component(engine_movie_rollcall_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_rollcall_test.c" ABSOLUTE)
add_executable(engine-movie-rollcall-test EXCLUDE_FROM_ALL "${engine_movie_rollcall_test_source}")
target_link_libraries(engine-movie-rollcall-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-rollcall-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-rollcall COMMAND engine-movie-rollcall-test)
get_filename_component(engine_movie_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_edit_test.c" ABSOLUTE)
add_executable(engine-movie-edit-test EXCLUDE_FROM_ALL "${engine_movie_edit_test_source}")
target_link_libraries(engine-movie-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-edit COMMAND engine-movie-edit-test)
get_filename_component(engine_movie_save_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_save_test.c" ABSOLUTE)
add_executable(engine-movie-save-test EXCLUDE_FROM_ALL "${engine_movie_save_test_source}")
target_link_libraries(engine-movie-save-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-save-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-save COMMAND engine-movie-save-test)
get_filename_component(engine_movie_scene_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_scene_test.c" ABSOLUTE)
add_executable(engine-movie-scene-test EXCLUDE_FROM_ALL "${engine_movie_scene_test_source}")
target_link_libraries(engine-movie-scene-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-scene-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-scene COMMAND engine-movie-scene-test)
get_filename_component(engine_scene_runtime_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_runtime_test.c" ABSOLUTE)
add_executable(engine-scene-runtime-test EXCLUDE_FROM_ALL "${engine_scene_runtime_test_source}")
target_link_libraries(engine-scene-runtime-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-runtime COMMAND engine-scene-runtime-test)
target_sources(mm-engine-c89 PRIVATE src/texture_resource.c src/texture_import.c)
target_sources(mm-engine-c89 PRIVATE src/material_resource.c src/custom_material.c)
target_sources(mm-engine-c89 PRIVATE src/audio_clock.c src/audio_mixer.c src/midi_render.c src/sound_queue.c src/sound_manager.c src/midi_mix.c)
target_sources(mm-engine-c89 PRIVATE src/actor_action_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_costume_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_route_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_subroute_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_placement.c)
target_sources(mm-engine-c89 PRIVATE src/actor_stage_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_record.c)
target_sources(mm-engine-c89 PRIVATE src/actor_transform_edit.c)
target_sources(mm-engine-c89 PRIVATE src/actor_owner.c)
target_sources(mm-engine-c89 PRIVATE src/actor_undo.c)
target_sources(mm-engine-c89 PRIVATE src/actor_group_undo.c)
target_sources(mm-engine-c89 PRIVATE src/actor_transaction.c)
target_sources(mm-engine-c89 PRIVATE src/actor_copy.c)
target_sources(mm-engine-c89 PRIVATE src/actor_route_paste.c)
target_sources(mm-engine-c89 PRIVATE src/actor_paste.c)
target_sources(mm-engine-c89 PRIVATE src/actor_clipboard.c)
target_sources(mm-engine-c89 PRIVATE src/actor_create.c)
target_sources(mm-engine-c89 PRIVATE src/movie_sound.c)
target_sources(mm-engine-c89 PRIVATE src/body_create.c)
target_sources(mm-engine-c89 PRIVATE src/body_edit.c)
target_sources(mm-engine-c89 PRIVATE src/body_runtime.c src/body_geometry.c src/model_resource.c src/model_lighting.c)
target_sources(mm-engine-c89 PRIVATE src/template_create.c)
target_sources(mm-engine-c89 PRIVATE src/template_runtime.c)
target_sources(mm-engine-c89 PRIVATE src/action_definition.c)
target_sources(mm-engine-c89 PRIVATE src/font_resource.c)
target_sources(mm-engine-c89 PRIVATE src/text_generation.c src/text_generation_math.c src/text_resource.c src/sound_resource.c src/wave.c src/midi.c src/sound_payload.c src/wave_import.c)
set_target_properties(mm-engine-c89 PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(mm-engine-c89 PRIVATE -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra)
endif()

get_filename_component(engine_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_c89_test.c" ABSOLUTE)
add_executable(engine-c89-test EXCLUDE_FROM_ALL "${engine_test_source}")
target_link_libraries(engine-c89-test PRIVATE mm-engine-c89)
set_target_properties(engine-c89-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-c89 COMMAND engine-c89-test)
get_filename_component(engine_movie_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_data_test.c" ABSOLUTE)
add_executable(engine-movie-data-test EXCLUDE_FROM_ALL "${engine_movie_test_source}")
target_link_libraries(engine-movie-data-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-data COMMAND engine-movie-data-test)
get_filename_component(engine_chunky_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunky_test.c" ABSOLUTE)
add_executable(engine-chunky-test EXCLUDE_FROM_ALL "${engine_chunky_test_source}")
target_link_libraries(engine-chunky-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunky-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunky COMMAND engine-chunky-test)
get_filename_component(engine_strings_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_strings_test.c" ABSOLUTE)
add_executable(engine-strings-test EXCLUDE_FROM_ALL "${engine_strings_test_source}")
target_link_libraries(engine-strings-test PRIVATE mm-engine-c89)
set_target_properties(engine-strings-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-strings COMMAND engine-strings-test)
get_filename_component(engine_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_storage_test.c" ABSOLUTE)
add_executable(engine-storage-test EXCLUDE_FROM_ALL "${engine_storage_test_source}")
target_link_libraries(engine-storage-test PRIVATE mm-engine-c89)
set_target_properties(engine-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-storage COMMAND engine-storage-test)
get_filename_component(engine_collections_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_collections_test.c" ABSOLUTE)
add_executable(engine-collections-test EXCLUDE_FROM_ALL "${engine_collections_test_source}")
target_link_libraries(engine-collections-test PRIVATE mm-engine-c89)
set_target_properties(engine-collections-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-collections COMMAND engine-collections-test)
get_filename_component(engine_data_stream_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_data_stream_test.c" ABSOLUTE)
add_executable(engine-data-stream-test EXCLUDE_FROM_ALL "${engine_data_stream_test_source}")
target_link_libraries(engine-data-stream-test PRIVATE mm-engine-c89)
set_target_properties(engine-data-stream-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-data-stream COMMAND engine-data-stream-test)
find_program(ENGINE_C89_COMPILER NAMES clang gcc)
get_filename_component(engine_codec_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_codec_test.c" ABSOLUTE)
add_executable(engine-codec-test EXCLUDE_FROM_ALL "${engine_codec_test_source}")
target_link_libraries(engine-codec-test PRIVATE mm-engine-c89)
set_target_properties(engine-codec-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-codec COMMAND engine-codec-test)
get_filename_component(engine_piece_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_piece_table_test.c" ABSOLUTE)
add_executable(engine-piece-table-test EXCLUDE_FROM_ALL "${engine_piece_test_source}")
target_link_libraries(engine-piece-table-test PRIVATE mm-engine-c89)
set_target_properties(engine-piece-table-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-piece-table COMMAND engine-piece-table-test)
get_filename_component(engine_block_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_block_io_test.c" ABSOLUTE)
add_executable(engine-block-io-test EXCLUDE_FROM_ALL "${engine_block_test_source}")
target_link_libraries(engine-block-io-test PRIVATE mm-engine-c89)
set_target_properties(engine-block-io-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-block-io COMMAND engine-block-io-test)
get_filename_component(engine_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_block_owner_test.c" ABSOLUTE)
add_executable(engine-block-owner-test EXCLUDE_FROM_ALL "${engine_owner_test_source}")
target_link_libraries(engine-block-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-block-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-block-owner COMMAND engine-block-owner-test)
get_filename_component(engine_file_policy_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_file_policy_test.c" ABSOLUTE)
add_executable(engine-file-policy-test EXCLUDE_FROM_ALL "${engine_file_policy_test_source}")
target_link_libraries(engine-file-policy-test PRIVATE mm-engine-c89)
set_target_properties(engine-file-policy-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-file-policy COMMAND engine-file-policy-test)
get_filename_component(engine_chunk_save_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_save_test.c" ABSOLUTE)
add_executable(engine-chunk-save-test EXCLUDE_FROM_ALL "${engine_chunk_save_test_source}")
target_link_libraries(engine-chunk-save-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-save-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-save COMMAND engine-chunk-save-test)
get_filename_component(engine_chunk_heap_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_heap_test.c" ABSOLUTE)
add_executable(engine-chunk-heap-test EXCLUDE_FROM_ALL "${engine_chunk_heap_test_source}")
target_link_libraries(engine-chunk-heap-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-heap-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-heap COMMAND engine-chunk-heap-test)
get_filename_component(engine_chunk_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_edit_test.c" ABSOLUTE)
add_executable(engine-chunk-edit-test EXCLUDE_FROM_ALL "${engine_chunk_edit_test_source}")
target_link_libraries(engine-chunk-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-edit COMMAND engine-chunk-edit-test)
get_filename_component(engine_chunk_graph_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_graph_test.c" ABSOLUTE)
add_executable(engine-chunk-graph-test EXCLUDE_FROM_ALL "${engine_chunk_graph_test_source}")
target_link_libraries(engine-chunk-graph-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-graph-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-graph COMMAND engine-chunk-graph-test)
get_filename_component(engine_chunk_ops_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_ops_test.c" ABSOLUTE)
add_executable(engine-chunk-ops-test EXCLUDE_FROM_ALL "${engine_chunk_ops_test_source}")
target_link_libraries(engine-chunk-ops-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-ops-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-ops COMMAND engine-chunk-ops-test)
get_filename_component(engine_metadata_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_metadata_test.c" ABSOLUTE)
add_executable(engine-metadata-test EXCLUDE_FROM_ALL "${engine_metadata_test_source}")
target_link_libraries(engine-metadata-test PRIVATE mm-engine-c89)
set_target_properties(engine-metadata-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-metadata COMMAND engine-metadata-test)
get_filename_component(engine_forest_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_forest_test.c" ABSOLUTE)
add_executable(engine-forest-test EXCLUDE_FROM_ALL "${engine_forest_test_source}")
target_link_libraries(engine-forest-test PRIVATE mm-engine-c89)
set_target_properties(engine-forest-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-forest COMMAND engine-forest-test)
get_filename_component(engine_loading_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_loading_test.c" ABSOLUTE)
add_executable(engine-loading-test EXCLUDE_FROM_ALL "${engine_loading_test_source}")
target_link_libraries(engine-loading-test PRIVATE mm-engine-c89)
set_target_properties(engine-loading-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-loading COMMAND engine-loading-test)
get_filename_component(engine_chunk_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_storage_test.c" ABSOLUTE)
add_executable(engine-chunk-storage-test EXCLUDE_FROM_ALL "${engine_chunk_storage_test_source}")
target_link_libraries(engine-chunk-storage-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-storage COMMAND engine-chunk-storage-test)
get_filename_component(engine_lifecycle_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_lifecycle_test.c" ABSOLUTE)
add_executable(engine-lifecycle-test EXCLUDE_FROM_ALL "${engine_lifecycle_test_source}")
target_link_libraries(engine-lifecycle-test PRIVATE mm-engine-c89)
set_target_properties(engine-lifecycle-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-lifecycle COMMAND engine-lifecycle-test)
get_filename_component(engine_container_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_container_test.c" ABSOLUTE)
add_executable(engine-container-test EXCLUDE_FROM_ALL "${engine_container_test_source}")
target_link_libraries(engine-container-test PRIVATE mm-engine-c89)
set_target_properties(engine-container-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-container COMMAND engine-container-test)
get_filename_component(engine_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_resource_cache_test.c" ABSOLUTE)
add_executable(engine-resource-cache-test EXCLUDE_FROM_ALL "${engine_resource_test_source}")
target_link_libraries(engine-resource-cache-test PRIVATE mm-engine-c89)
set_target_properties(engine-resource-cache-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-resource-cache COMMAND engine-resource-cache-test)
get_filename_component(engine_chunk_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_owner_test.c" ABSOLUTE)
add_executable(engine-chunk-owner-test EXCLUDE_FROM_ALL "${engine_chunk_owner_test_source}")
target_link_libraries(engine-chunk-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-owner COMMAND engine-chunk-owner-test)
get_filename_component(engine_chunk_table_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_table_test.c" ABSOLUTE)
add_executable(engine-chunk-table-test EXCLUDE_FROM_ALL "${engine_chunk_table_test_source}")
target_link_libraries(engine-chunk-table-test PRIVATE mm-engine-c89)
set_target_properties(engine-chunk-table-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-table COMMAND engine-chunk-table-test)
get_filename_component(engine_tag_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_tag_test.c" ABSOLUTE)
add_executable(engine-tag-test EXCLUDE_FROM_ALL "${engine_tag_test_source}")
target_link_libraries(engine-tag-test PRIVATE mm-engine-c89)
set_target_properties(engine-tag-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-tag COMMAND engine-tag-test)
get_filename_component(engine_source_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_source_test.c" ABSOLUTE)
add_executable(engine-source-test EXCLUDE_FROM_ALL "${engine_source_test_source}")
target_link_libraries(engine-source-test PRIVATE mm-engine-c89)
set_target_properties(engine-source-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-source COMMAND engine-source-test)
get_filename_component(engine_source_names_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_source_names_test.c" ABSOLUTE)
add_executable(engine-source-names-test EXCLUDE_FROM_ALL "${engine_source_names_test_source}")
target_link_libraries(engine-source-names-test PRIVATE mm-engine-c89)
set_target_properties(engine-source-names-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-source-names COMMAND engine-source-names-test)
get_filename_component(engine_movie_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_owner_test.c" ABSOLUTE)
add_executable(engine-movie-owner-test EXCLUDE_FROM_ALL "${engine_movie_owner_test_source}")
target_link_libraries(engine-movie-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-owner COMMAND engine-movie-owner-test)
get_filename_component(engine_rollcall_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_rollcall_storage_test.c" ABSOLUTE)
add_executable(engine-rollcall-storage-test EXCLUDE_FROM_ALL "${engine_rollcall_storage_test_source}")
target_link_libraries(engine-rollcall-storage-test PRIVATE mm-engine-c89)
set_target_properties(engine-rollcall-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-rollcall-storage COMMAND engine-rollcall-storage-test)
get_filename_component(engine_movie_title_root_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_title_root_test.c" ABSOLUTE)
add_executable(engine-movie-title-root-test EXCLUDE_FROM_ALL "${engine_movie_title_root_test_source}")
target_link_libraries(engine-movie-title-root-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-title-root-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-title-root COMMAND engine-movie-title-root-test)
get_filename_component(engine_scene_data_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_data_test.c" ABSOLUTE)
add_executable(engine-scene-data-test EXCLUDE_FROM_ALL "${engine_scene_data_test_source}")
target_link_libraries(engine-scene-data-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-data COMMAND engine-scene-data-test)
get_filename_component(engine_actor_data_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_data_test.c" ABSOLUTE)
add_executable(engine-actor-data-test EXCLUDE_FROM_ALL "${engine_actor_data_test_source}")
target_link_libraries(engine-actor-data-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-data COMMAND engine-actor-data-test)
get_filename_component(engine_actor_track_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_track_test.c" ABSOLUTE)
add_executable(engine-actor-track-test EXCLUDE_FROM_ALL "${engine_actor_track_test_source}")
target_link_libraries(engine-actor-track-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-track-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-track COMMAND engine-actor-track-test)
get_filename_component(engine_actor_motion_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_motion_test.c" ABSOLUTE)
add_executable(engine-actor-motion-test EXCLUDE_FROM_ALL "${engine_actor_motion_test_source}")
target_link_libraries(engine-actor-motion-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-motion-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-motion COMMAND engine-actor-motion-test)
get_filename_component(engine_actor_frame_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_frame_test.c" ABSOLUTE)
add_executable(engine-actor-frame-test EXCLUDE_FROM_ALL "${engine_actor_frame_test_source}")
target_link_libraries(engine-actor-frame-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-frame-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-frame COMMAND engine-actor-frame-test)
get_filename_component(engine_actor_timeline_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_timeline_test.c" ABSOLUTE)
add_executable(engine-actor-timeline-test EXCLUDE_FROM_ALL "${engine_actor_timeline_test_source}")
target_link_libraries(engine-actor-timeline-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-timeline-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-timeline COMMAND engine-actor-timeline-test)
get_filename_component(engine_actor_pose_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_pose_test.c" ABSOLUTE)
add_executable(engine-actor-pose-test EXCLUDE_FROM_ALL "${engine_actor_pose_test_source}" "${engine_actor_effect_test_source}" "${engine_actor_sound_test_source}" "${engine_sound_edit_test_source}")
target_link_libraries(engine-actor-pose-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-pose-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-pose COMMAND engine-actor-pose-test)
get_filename_component(engine_actor_effect_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_effect_test.c" ABSOLUTE)
add_executable(engine-actor-effect-test EXCLUDE_FROM_ALL "${engine_actor_effect_test_source}")
target_link_libraries(engine-actor-effect-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-effect-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-effect COMMAND engine-actor-effect-test)
get_filename_component(engine_actor_sound_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_sound_test.c" ABSOLUTE)
add_executable(engine-actor-sound-test EXCLUDE_FROM_ALL "${engine_actor_sound_test_source}")
target_link_libraries(engine-actor-sound-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-sound-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-sound COMMAND engine-actor-sound-test)
get_filename_component(engine_sound_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_sound_edit_test.c" ABSOLUTE)
add_executable(engine-sound-edit-test EXCLUDE_FROM_ALL "${engine_sound_edit_test_source}")
target_link_libraries(engine-sound-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-sound-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-sound-edit COMMAND engine-sound-edit-test)
get_filename_component(engine_event_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_event_edit_test.c" ABSOLUTE)
add_executable(engine-event-edit-test EXCLUDE_FROM_ALL "${engine_event_edit_test_source}")
target_link_libraries(engine-event-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-event-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-event-edit COMMAND engine-event-edit-test)
get_filename_component(engine_action_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_action_edit_test.c" ABSOLUTE)
add_executable(engine-action-edit-test EXCLUDE_FROM_ALL "${engine_action_edit_test_source}")
target_link_libraries(engine-action-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-action-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-action-edit COMMAND engine-action-edit-test)
get_filename_component(engine_costume_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_costume_edit_test.c" ABSOLUTE)
add_executable(engine-costume-edit-test EXCLUDE_FROM_ALL "${engine_costume_edit_test_source}")
target_link_libraries(engine-costume-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-costume-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-costume-edit COMMAND engine-costume-edit-test)
get_filename_component(engine_route_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_route_edit_test.c" ABSOLUTE)
add_executable(engine-route-edit-test EXCLUDE_FROM_ALL "${engine_route_edit_test_source}")
target_link_libraries(engine-route-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-route-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-route-edit COMMAND engine-route-edit-test)
get_filename_component(engine_subroute_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_subroute_edit_test.c" ABSOLUTE)
add_executable(engine-subroute-edit-test EXCLUDE_FROM_ALL "${engine_subroute_edit_test_source}")
target_link_libraries(engine-subroute-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-subroute-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-subroute-edit COMMAND engine-subroute-edit-test)
get_filename_component(engine_placement_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_placement_test.c" ABSOLUTE)
add_executable(engine-placement-test EXCLUDE_FROM_ALL "${engine_placement_test_source}")
target_link_libraries(engine-placement-test PRIVATE mm-engine-c89)
set_target_properties(engine-placement-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-placement COMMAND engine-placement-test)
get_filename_component(engine_stage_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_stage_edit_test.c" ABSOLUTE)
add_executable(engine-stage-edit-test EXCLUDE_FROM_ALL "${engine_stage_edit_test_source}")
target_link_libraries(engine-stage-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-stage-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-stage-edit COMMAND engine-stage-edit-test)
get_filename_component(engine_record_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_record_test.c" ABSOLUTE)
add_executable(engine-record-test EXCLUDE_FROM_ALL "${engine_record_test_source}")
target_link_libraries(engine-record-test PRIVATE mm-engine-c89)
set_target_properties(engine-record-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-record COMMAND engine-record-test)
get_filename_component(engine_transform_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_transform_edit_test.c" ABSOLUTE)
add_executable(engine-transform-edit-test EXCLUDE_FROM_ALL "${engine_transform_edit_test_source}")
target_link_libraries(engine-transform-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-transform-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-transform-edit COMMAND engine-transform-edit-test)
get_filename_component(engine_actor_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_owner_test.c" ABSOLUTE)
add_executable(engine-actor-owner-test EXCLUDE_FROM_ALL "${engine_actor_owner_test_source}")
target_link_libraries(engine-actor-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-owner COMMAND engine-actor-owner-test)
get_filename_component(engine_actor_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_undo_test.c" ABSOLUTE)
add_executable(engine-actor-undo-test EXCLUDE_FROM_ALL "${engine_actor_undo_test_source}")
target_link_libraries(engine-actor-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-undo COMMAND engine-actor-undo-test)
get_filename_component(engine_group_undo_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_group_undo_test.c" ABSOLUTE)
add_executable(engine-group-undo-test EXCLUDE_FROM_ALL "${engine_group_undo_test_source}")
target_link_libraries(engine-group-undo-test PRIVATE mm-engine-c89)
set_target_properties(engine-group-undo-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-group-undo COMMAND engine-group-undo-test)
get_filename_component(engine_actor_transaction_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_transaction_test.c" ABSOLUTE)
add_executable(engine-actor-transaction-test EXCLUDE_FROM_ALL "${engine_actor_transaction_test_source}")
target_link_libraries(engine-actor-transaction-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-transaction-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-transaction COMMAND engine-actor-transaction-test)
get_filename_component(engine_actor_copy_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_copy_test.c" ABSOLUTE)
add_executable(engine-actor-copy-test EXCLUDE_FROM_ALL "${engine_actor_copy_test_source}")
target_link_libraries(engine-actor-copy-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-copy-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-copy COMMAND engine-actor-copy-test)
get_filename_component(engine_route_paste_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_route_paste_test.c" ABSOLUTE)
add_executable(engine-route-paste-test EXCLUDE_FROM_ALL "${engine_route_paste_test_source}")
target_link_libraries(engine-route-paste-test PRIVATE mm-engine-c89)
set_target_properties(engine-route-paste-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-route-paste COMMAND engine-route-paste-test)
get_filename_component(engine_actor_paste_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_paste_test.c" ABSOLUTE)
add_executable(engine-actor-paste-test EXCLUDE_FROM_ALL "${engine_actor_paste_test_source}")
target_link_libraries(engine-actor-paste-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-paste-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-paste COMMAND engine-actor-paste-test)
get_filename_component(engine_actor_clipboard_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_clipboard_test.c" ABSOLUTE)
add_executable(engine-actor-clipboard-test EXCLUDE_FROM_ALL "${engine_actor_clipboard_test_source}")
target_link_libraries(engine-actor-clipboard-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-clipboard-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-clipboard COMMAND engine-actor-clipboard-test)
get_filename_component(engine_actor_create_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_create_test.c" ABSOLUTE)
add_executable(engine-actor-create-test EXCLUDE_FROM_ALL "${engine_actor_create_test_source}")
target_link_libraries(engine-actor-create-test PRIVATE mm-engine-c89)
set_target_properties(engine-actor-create-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-create COMMAND engine-actor-create-test)
get_filename_component(engine_movie_sound_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_sound_test.c" ABSOLUTE)
add_executable(engine-movie-sound-test EXCLUDE_FROM_ALL "${engine_movie_sound_test_source}")
target_link_libraries(engine-movie-sound-test PRIVATE mm-engine-c89)
set_target_properties(engine-movie-sound-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-sound COMMAND engine-movie-sound-test)
get_filename_component(engine_body_create_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_body_create_test.c" ABSOLUTE)
add_executable(engine-body-create-test EXCLUDE_FROM_ALL "${engine_body_create_test_source}")
target_link_libraries(engine-body-create-test PRIVATE mm-engine-c89)
set_target_properties(engine-body-create-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-body-create COMMAND engine-body-create-test)
get_filename_component(engine_body_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_body_edit_test.c" ABSOLUTE)
add_executable(engine-body-edit-test EXCLUDE_FROM_ALL "${engine_body_edit_test_source}")
target_link_libraries(engine-body-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-body-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-body-edit COMMAND engine-body-edit-test)
get_filename_component(engine_body_runtime_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_body_runtime_test.c" ABSOLUTE)
add_executable(engine-body-runtime-test EXCLUDE_FROM_ALL "${engine_body_runtime_test_source}")
target_link_libraries(engine-body-runtime-test PRIVATE mm-engine-c89)
set_target_properties(engine-body-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-body-runtime COMMAND engine-body-runtime-test)
get_filename_component(engine_template_create_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_template_create_test.c" ABSOLUTE)
add_executable(engine-template-create-test EXCLUDE_FROM_ALL "${engine_template_create_test_source}")
target_link_libraries(engine-template-create-test PRIVATE mm-engine-c89)
set_target_properties(engine-template-create-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-template-create COMMAND engine-template-create-test)
get_filename_component(engine_template_runtime_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_template_runtime_test.c" ABSOLUTE)
add_executable(engine-template-runtime-test EXCLUDE_FROM_ALL "${engine_template_runtime_test_source}")
target_link_libraries(engine-template-runtime-test PRIVATE mm-engine-c89)
set_target_properties(engine-template-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-template-runtime COMMAND engine-template-runtime-test)
get_filename_component(engine_action_definition_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_action_definition_test.c" ABSOLUTE)
add_executable(engine-action-definition-test EXCLUDE_FROM_ALL "${engine_action_definition_test_source}")
target_link_libraries(engine-action-definition-test PRIVATE mm-engine-c89)
set_target_properties(engine-action-definition-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-action-definition COMMAND engine-action-definition-test)
get_filename_component(engine_font_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_font_resource_test.c" ABSOLUTE)
add_executable(engine-font-resource-test EXCLUDE_FROM_ALL "${engine_font_resource_test_source}")
target_link_libraries(engine-font-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-font-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-font-resource COMMAND engine-font-resource-test)
get_filename_component(engine_text_generation_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_generation_test.c" ABSOLUTE)
add_executable(engine-text-generation-test EXCLUDE_FROM_ALL "${engine_text_generation_test_source}")
target_link_libraries(engine-text-generation-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-generation-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-generation COMMAND engine-text-generation-test)
get_filename_component(engine_text_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_text_resource_test.c" ABSOLUTE)
add_executable(engine-text-resource-test EXCLUDE_FROM_ALL "${engine_text_resource_test_source}")
target_link_libraries(engine-text-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-text-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-text-resource COMMAND engine-text-resource-test)
get_filename_component(engine_sound_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_sound_resource_test.c" ABSOLUTE)
add_executable(engine-sound-resource-test EXCLUDE_FROM_ALL "${engine_sound_resource_test_source}")
target_link_libraries(engine-sound-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-sound-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-sound-resource COMMAND engine-sound-resource-test)
get_filename_component(engine_sound_payload_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_sound_payload_test.c" ABSOLUTE)
add_executable(engine-sound-payload-test EXCLUDE_FROM_ALL "${engine_sound_payload_test_source}")
target_link_libraries(engine-sound-payload-test PRIVATE mm-engine-c89)
set_target_properties(engine-sound-payload-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-sound-payload COMMAND engine-sound-payload-test)
get_filename_component(engine_body_geometry_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_body_geometry_test.c" ABSOLUTE)
add_executable(engine-body-geometry-test EXCLUDE_FROM_ALL "${engine_body_geometry_test_source}")
target_link_libraries(engine-body-geometry-test PRIVATE mm-engine-c89)
set_target_properties(engine-body-geometry-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-body-geometry COMMAND engine-body-geometry-test)
get_filename_component(engine_model_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_model_resource_test.c" ABSOLUTE)
add_executable(engine-model-resource-test EXCLUDE_FROM_ALL "${engine_model_resource_test_source}")
target_link_libraries(engine-model-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-model-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-model-resource COMMAND engine-model-resource-test)
get_filename_component(engine_audio_runtime_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_audio_runtime_test.c" ABSOLUTE)
add_executable(engine-audio-runtime-test EXCLUDE_FROM_ALL "${engine_audio_runtime_test_source}")
target_link_libraries(engine-audio-runtime-test PRIVATE mm-engine-c89)
set_target_properties(engine-audio-runtime-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-audio-runtime COMMAND engine-audio-runtime-test)
get_filename_component(engine_material_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_material_resource_test.c" ABSOLUTE)
add_executable(engine-material-resource-test EXCLUDE_FROM_ALL "${engine_material_resource_test_source}")
target_link_libraries(engine-material-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-material-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-material-resource COMMAND engine-material-resource-test)
get_filename_component(engine_texture_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_texture_resource_test.c" ABSOLUTE)
add_executable(engine-texture-resource-test EXCLUDE_FROM_ALL "${engine_texture_resource_test_source}")
target_link_libraries(engine-texture-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-texture-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-texture-resource COMMAND engine-texture-resource-test)
get_filename_component(engine_bitmap_file_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_bitmap_file_test.c" ABSOLUTE)
add_executable(engine-bitmap-file-test EXCLUDE_FROM_ALL "${engine_bitmap_file_test_source}")
target_link_libraries(engine-bitmap-file-test PRIVATE mm-engine-c89)
set_target_properties(engine-bitmap-file-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-bitmap-file COMMAND engine-bitmap-file-test)
get_filename_component(engine_depth_bitmap_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_depth_bitmap_test.c" ABSOLUTE)
add_executable(engine-depth-bitmap-test EXCLUDE_FROM_ALL "${engine_depth_bitmap_test_source}")
target_link_libraries(engine-depth-bitmap-test PRIVATE mm-engine-c89)
set_target_properties(engine-depth-bitmap-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-depth-bitmap COMMAND engine-depth-bitmap-test)
get_filename_component(engine_masked_bitmap_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_masked_bitmap_test.c" ABSOLUTE)
add_executable(engine-masked-bitmap-test EXCLUDE_FROM_ALL "${engine_masked_bitmap_test_source}")
target_link_libraries(engine-masked-bitmap-test PRIVATE mm-engine-c89)
set_target_properties(engine-masked-bitmap-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-masked-bitmap COMMAND engine-masked-bitmap-test)
get_filename_component(engine_world_buffers_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_world_buffers_test.c" ABSOLUTE)
add_executable(engine-world-buffers-test EXCLUDE_FROM_ALL "${engine_world_buffers_test_source}")
target_link_libraries(engine-world-buffers-test PRIVATE mm-engine-c89)
set_target_properties(engine-world-buffers-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-world-buffers COMMAND engine-world-buffers-test)
get_filename_component(engine_world_camera_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_world_camera_test.c" ABSOLUTE)
add_executable(engine-world-camera-test EXCLUDE_FROM_ALL "${engine_world_camera_test_source}")
target_link_libraries(engine-world-camera-test PRIVATE mm-engine-c89)
set_target_properties(engine-world-camera-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-world-camera COMMAND engine-world-camera-test)
get_filename_component(engine_world_render_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_world_render_test.c" ABSOLUTE)
add_executable(engine-world-render-test EXCLUDE_FROM_ALL "${engine_world_render_test_source}")
target_link_libraries(engine-world-render-test PRIVATE mm-engine-c89)
set_target_properties(engine-world-render-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-world-render COMMAND engine-world-render-test)
get_filename_component(engine_world_export_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_world_export_test.c" ABSOLUTE)
add_executable(engine-world-export-test EXCLUDE_FROM_ALL "${engine_world_export_test_source}")
target_link_libraries(engine-world-export-test PRIVATE mm-engine-c89)
set_target_properties(engine-world-export-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-world-export COMMAND engine-world-export-test)
get_filename_component(engine_scene_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_owner_test.c" ABSOLUTE)
add_executable(engine-scene-owner-test EXCLUDE_FROM_ALL "${engine_scene_owner_test_source}")
target_link_libraries(engine-scene-owner-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-owner COMMAND engine-scene-owner-test)
get_filename_component(engine_scene_events_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_events_test.c" ABSOLUTE)
add_executable(engine-scene-events-test EXCLUDE_FROM_ALL "${engine_scene_events_test_source}")
target_link_libraries(engine-scene-events-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-events-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-events COMMAND engine-scene-events-test)
get_filename_component(engine_scene_sound_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_sound_test.c" ABSOLUTE)
add_executable(engine-scene-sound-test EXCLUDE_FROM_ALL "${engine_scene_sound_test_source}")
target_link_libraries(engine-scene-sound-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-sound-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-sound COMMAND engine-scene-sound-test)
get_filename_component(engine_scene_sound_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_sound_edit_test.c" ABSOLUTE)
add_executable(engine-scene-sound-edit-test EXCLUDE_FROM_ALL "${engine_scene_sound_edit_test_source}")
target_link_libraries(engine-scene-sound-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-sound-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-sound-edit COMMAND engine-scene-sound-edit-test)
get_filename_component(engine_scene_load_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_load_test.c" ABSOLUTE)
add_executable(engine-scene-load-test EXCLUDE_FROM_ALL "${engine_scene_load_test_source}")
target_link_libraries(engine-scene-load-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-load-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-load COMMAND engine-scene-load-test)
get_filename_component(engine_scene_store_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_store_test.c" ABSOLUTE)
add_executable(engine-scene-store-test EXCLUDE_FROM_ALL "${engine_scene_store_test_source}")
target_link_libraries(engine-scene-store-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-store-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-store COMMAND engine-scene-store-test)
get_filename_component(engine_scene_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_edit_test.c" ABSOLUTE)
add_executable(engine-scene-edit-test EXCLUDE_FROM_ALL "${engine_scene_edit_test_source}")
target_link_libraries(engine-scene-edit-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-edit COMMAND engine-scene-edit-test)
get_filename_component(engine_scene_content_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_content_test.c" ABSOLUTE)
add_executable(engine-scene-content-test EXCLUDE_FROM_ALL "${engine_scene_content_test_source}")
target_link_libraries(engine-scene-content-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-content-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-content COMMAND engine-scene-content-test)
get_filename_component(engine_scene_selection_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_selection_test.c" ABSOLUTE)
add_executable(engine-scene-selection-test EXCLUDE_FROM_ALL "${engine_scene_selection_test_source}")
target_link_libraries(engine-scene-selection-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-selection-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-selection COMMAND engine-scene-selection-test)
get_filename_component(engine_scene_chop_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_chop_test.c" ABSOLUTE)
add_executable(engine-scene-chop-test EXCLUDE_FROM_ALL "${engine_scene_chop_test_source}")
target_link_libraries(engine-scene-chop-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-chop-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-chop COMMAND engine-scene-chop-test)
get_filename_component(engine_scene_control_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_control_test.c" ABSOLUTE)
add_executable(engine-scene-control-test EXCLUDE_FROM_ALL "${engine_scene_control_test_source}")
target_link_libraries(engine-scene-control-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-control-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-control COMMAND engine-scene-control-test)
get_filename_component(engine_scene_thumbnail_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_thumbnail_test.c" ABSOLUTE)
add_executable(engine-scene-thumbnail-test EXCLUDE_FROM_ALL "${engine_scene_thumbnail_test_source}")
target_link_libraries(engine-scene-thumbnail-test PRIVATE mm-engine-c89)
set_target_properties(engine-scene-thumbnail-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-thumbnail COMMAND engine-scene-thumbnail-test)
get_filename_component(engine_background_camera_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_background_camera_test.c" ABSOLUTE)
add_executable(engine-background-camera-test EXCLUDE_FROM_ALL "${engine_background_camera_test_source}")
target_link_libraries(engine-background-camera-test PRIVATE mm-engine-c89)
set_target_properties(engine-background-camera-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-background-camera COMMAND engine-background-camera-test)
get_filename_component(engine_background_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_background_resource_test.c" ABSOLUTE)
add_executable(engine-background-resource-test EXCLUDE_FROM_ALL "${engine_background_resource_test_source}")
target_link_libraries(engine-background-resource-test PRIVATE mm-engine-c89)
set_target_properties(engine-background-resource-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-background-resource COMMAND engine-background-resource-test)
get_filename_component(engine_background_mesh_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_background_mesh_test.c" ABSOLUTE)
add_executable(engine-background-mesh-test EXCLUDE_FROM_ALL "${engine_background_mesh_test_source}")
target_link_libraries(engine-background-mesh-test PRIVATE mm-engine-c89)
set_target_properties(engine-background-mesh-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-background-mesh COMMAND engine-background-mesh-test)
get_filename_component(engine_background_preview_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_background_preview_test.c" ABSOLUTE)
add_executable(engine-background-preview-test EXCLUDE_FROM_ALL "${engine_background_preview_test_source}")
target_link_libraries(engine-background-preview-test PRIVATE mm-engine-c89)
set_target_properties(engine-background-preview-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-background-preview COMMAND engine-background-preview-test)

# These tests use assert for checks and some setup calls. Keep them active in
# Release builds too; NDEBUG otherwise turns passing tests into partial no-ops.
get_property(engine_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(engine-c89-checks)
foreach(engine_test_target IN LISTS engine_test_targets)
  if(engine_test_target MATCHES "^engine-.*-test$")
    add_dependencies(engine-c89-checks ${engine_test_target})
    if(MSVC)
      target_compile_options(${engine_test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${engine_test_target} PRIVATE -UNDEBUG)
    endif()
  endif()
endforeach()
if(ENGINE_C89_COMPILER)
  add_test(NAME engine-native-runtime-c89-syntax
    COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
      "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/${native_runtime_source}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_math.c" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_runtime_math_test.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_runtime_c_test.c")
  add_test(NAME engine-native-runtime-provider-c89-syntax
    COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
      "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_runtime_provider_test.c")
  set(settings_extra_sources)
  if(NOT WIN32)
    set(settings_extra_sources "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_settings_posix_test.c")
  endif()
  foreach(profile IN LISTS native_settings_profiles)
    set(settings_definitions)
    if(profile STREQUAL "wide")
      set(settings_definitions -DUNICODE -D_UNICODE)
    endif()
    add_test(NAME engine-native-settings-${profile}-c89-syntax
      COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
        ${settings_definitions} "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only
        "${CMAKE_CURRENT_SOURCE_DIR}/${native_settings_source}" "${CMAKE_CURRENT_SOURCE_DIR}/src/settings.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/settings_paths.c" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_settings_test.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_settings_c_test.c" ${settings_extra_sources})
  endforeach()
  foreach(profile IN LISTS native_users_profiles)
    set(users_definitions)
    if(profile STREQUAL "wide")
      set(users_definitions -DUNICODE -D_UNICODE)
    endif()
    add_test(NAME engine-native-users-${profile}-c89-syntax
      COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
        ${users_definitions} "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only
        "${CMAKE_CURRENT_SOURCE_DIR}/${native_users_source}" "${CMAKE_CURRENT_SOURCE_DIR}/src/native_user_paths.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_users_c_test.c" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_users_provider_test.c")
  endforeach()
  if(WIN32)
    add_test(NAME engine-native-files-c89-syntax
      COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
        "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/src/native_files_win.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_files_c_test.c")
    add_test(NAME engine-native-files-wide-c89-syntax
      COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
        -DUNICODE -D_UNICODE "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/src/native_files_win.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_files_c_test.c")
  endif()
  if(NOT WIN32)
    add_test(NAME engine-native-files-c89-syntax
      COMMAND "${ENGINE_C89_COMPILER}" -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror
        "-I${CMAKE_CURRENT_SOURCE_DIR}/include" -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/src/native_files_posix.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native_files_c_test.c")
  endif()
  add_test(NAME engine-c89-syntax COMMAND "${ENGINE_C89_COMPILER}"
    -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror -fsyntax-only
    "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_math.c" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_runtime_math_test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/settings.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/settings_paths.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_settings_test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/playback.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reference.c" "${engine_reference_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handle_memory.c" "${engine_handle_memory_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/linked_list.c" "${engine_linked_list_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/file_owner.c" "${engine_file_owner_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/file_ops.c" "${engine_file_ops_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_value.c" "${engine_filename_value_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_owned.c" "${engine_filename_owned_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_path.c" "${engine_filename_path_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_naming.c" "${engine_filename_naming_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_case.c" "${engine_filename_case_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_enum.c" "${engine_filename_enum_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filename_frame.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/memory_debug.c" "${engine_memory_debug_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/object_debug.c" "${engine_object_debug_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/heap.c" "${engine_heap_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/object_memory.c" "${engine_object_memory_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/memory.c" "${engine_memory_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/error_stack.c" "${engine_error_stack_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_value.c" "${engine_string_value_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_format.c" "${engine_string_format_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_ops.c" "${engine_string_ops_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bitmap_file.c"
    "${engine_bitmap_file_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/depth_bitmap.c"
    "${engine_depth_bitmap_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/masked_bitmap.c"
    "${engine_masked_bitmap_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/world_buffers.c"
    "${engine_world_buffers_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/world_camera.c"
    "${engine_world_camera_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/world_render.c"
    "${engine_world_render_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/world_export.c"
    "${engine_world_export_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_events.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_sound.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_sound_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_load.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_store.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_content.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_selection.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_chop.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_control.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_thumbnail.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/background_camera.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/background_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/background_mesh.c"
    "${engine_background_mesh_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/background_preview.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_scene.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_save.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_rollcall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_content.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_compose.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_geometry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_render.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_pick.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_camera.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_live.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_admission.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_actor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_session.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_metadata.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/preview_media.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_store.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_live.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_document.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_materialize.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_properties.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_format.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_access.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_replace.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_copy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_undo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/document_undo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/document_lifecycle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/document_views.c"
    "${engine_document_views_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/help_text.c"
    "${engine_help_text_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/help_display.c"
    "${engine_help_display_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/help_interaction.c"
    "${engine_help_interaction_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/help_balloon.c"
    "${engine_help_balloon_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_undo.c" "${engine_movie_undo_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_box_edit.c" "${engine_text_box_edit_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_box_undo.c" "${engine_text_box_undo_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_box_runtime.c" "${engine_text_box_runtime_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rich_text_layout.c" "${engine_rich_text_layout_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/help_topic.c"
    "${engine_help_topic_test_source}"
    "${engine_document_lifecycle_test_source}"
    "${engine_document_undo_test_source}"
    "${engine_text_undo_test_source}"
    "${engine_text_copy_test_source}"
    "${engine_text_access_test_source}"
    "${engine_text_replace_test_source}"
    "${engine_text_format_test_source}"
    "${engine_text_properties_test_source}"
    "${engine_text_materialize_test_source}"
    "${engine_text_document_test_source}"
    "${engine_text_live_test_source}"
    "${engine_text_store_test_source}"
    "${engine_movie_compose_test_source}"
    "${engine_preview_render_test_source}"
    "${engine_preview_pick_test_source}"
    "${engine_preview_camera_test_source}"
    "${engine_preview_live_test_source}"
    "${engine_preview_admission_test_source}"
    "${engine_preview_actor_test_source}"
    "${engine_preview_session_test_source}"
    "${engine_preview_metadata_test_source}"
    "${engine_preview_media_test_source}"
    "${engine_movie_content_test_source}"
    "${engine_movie_rollcall_test_source}"
    "${engine_movie_edit_test_source}"
    "${engine_movie_save_test_source}"
    "${engine_movie_scene_test_source}"
    "${engine_background_preview_test_source}"
    "${engine_background_resource_test_source}"
    "${engine_background_camera_test_source}"
    "${engine_scene_thumbnail_test_source}"
    "${engine_scene_control_test_source}"
    "${engine_scene_chop_test_source}"
    "${engine_scene_selection_test_source}"
    "${engine_scene_content_test_source}"
    "${engine_scene_edit_test_source}"
    "${engine_scene_store_test_source}"
    "${engine_scene_load_test_source}"
    "${engine_scene_sound_edit_test_source}"
    "${engine_scene_sound_test_source}"
    "${engine_scene_events_test_source}"
    "${engine_scene_runtime_test_source}"
    "${engine_scene_owner_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/texture_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/texture_import.c"
    "${engine_texture_resource_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/material_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/custom_material.c"
    "${engine_material_resource_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio_mixer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_render.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_mix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sound_queue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sound_manager.c"
    "${engine_audio_runtime_test_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scroll.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/read_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_data.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_title.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_root.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene_data.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_data.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_track.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_fixed.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_route.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_frame.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_timeline.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_seek.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_math.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_pose.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_effect.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_sound.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_sound_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_action_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_costume_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_route_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_subroute_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_placement.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_stage_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_record.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_transform_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_undo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_group_undo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_transaction.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_copy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_route_paste.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_paste.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_clipboard.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/actor_create.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_sound.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/body_create.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/body_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/body_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/body_geometry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model_lighting.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/template_create.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/template_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/action_definition.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/font_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_generation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_generation_math.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sound_resource.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wave.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sound_payload.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wave_import.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rollcall_storage.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunky_file.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_table.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collection_storage.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collections.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collection_headers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/data_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/codec.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/piece_table.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/block_io.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/block_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/file_text.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_save.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_heap.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_graph.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_ops.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_metadata.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_wire.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/codec_registry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_forest.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_loading.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_storage.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_lifecycle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_container.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_context.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_table.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tag.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tag_list.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/source.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/source_names.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/source_discovery.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wide.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resource_cache.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resource_manager.c"
    "${engine_actor_effect_test_source}" "${engine_actor_sound_test_source}"
    "${engine_sound_edit_test_source}" "${engine_event_edit_test_source}"
    "${engine_action_edit_test_source}"
    "${engine_costume_edit_test_source}"
    "${engine_route_edit_test_source}"
    "${engine_subroute_edit_test_source}"
    "${engine_placement_test_source}"
    "${engine_stage_edit_test_source}"
    "${engine_record_test_source}"
    "${engine_transform_edit_test_source}"
    "${engine_actor_owner_test_source}"
    "${engine_actor_undo_test_source}"
    "${engine_group_undo_test_source}"
    "${engine_actor_transaction_test_source}"
    "${engine_actor_copy_test_source}"
    "${engine_route_paste_test_source}"
    "${engine_actor_paste_test_source}"
    "${engine_actor_clipboard_test_source}"
    "${engine_actor_create_test_source}"
    "${engine_movie_sound_test_source}"
    "${engine_body_create_test_source}"
    "${engine_body_edit_test_source}"
    "${engine_body_runtime_test_source}"
    "${engine_body_geometry_test_source}"
    "${engine_model_resource_test_source}"
    "${engine_template_create_test_source}"
    "${engine_template_runtime_test_source}"
    "${engine_action_definition_test_source}"
    "${engine_font_resource_test_source}"
    "${engine_text_generation_test_source}"
    "${engine_text_resource_test_source}"
    "${engine_sound_resource_test_source}"
    "${engine_sound_payload_test_source}"
    "${engine_test_source}" "${engine_movie_test_source}" "${engine_chunky_test_source}" "${engine_strings_test_source}" "${engine_storage_test_source}" "${engine_collections_test_source}" "${engine_data_stream_test_source}" "${engine_piece_test_source}" "${engine_codec_test_source}" "${engine_block_test_source}" "${engine_owner_test_source}" "${engine_file_policy_test_source}" "${engine_chunk_save_test_source}" "${engine_chunk_heap_test_source}" "${engine_chunk_edit_test_source}" "${engine_chunk_graph_test_source}" "${engine_chunk_ops_test_source}" "${engine_metadata_test_source}" "${engine_forest_test_source}" "${engine_loading_test_source}" "${engine_chunk_storage_test_source}" "${engine_lifecycle_test_source}" "${engine_container_test_source}" "${engine_resource_test_source}" "${engine_chunk_owner_test_source}" "${engine_chunk_table_test_source}" "${engine_tag_test_source}" "${engine_source_test_source}" "${engine_source_names_test_source}" "${engine_movie_owner_test_source}" "${engine_rollcall_storage_test_source}" "${engine_movie_title_root_test_source}" "${engine_scene_data_test_source}" "${engine_actor_data_test_source}" "${engine_actor_track_test_source}" "${engine_actor_motion_test_source}" "${engine_actor_frame_test_source}" "${engine_actor_timeline_test_source}" "${engine_actor_pose_test_source}")
endif()
