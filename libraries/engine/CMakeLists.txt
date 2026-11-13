cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ThreeMMEngine LANGUAGES C)
  include(CTest)
endif()

add_library(three-mm-engine-c89 STATIC src/playback.c src/scroll.c src/read_stream.c src/movie_data.c src/movie_owner.c src/movie_title.c src/movie_root.c src/scene_data.c src/actor_data.c src/actor_track.c src/actor_fixed.c src/actor_route.c src/actor_state.c src/actor_frame.c src/actor_timeline.c src/actor_seek.c src/actor_math.c src/actor_pose.c src/actor_effect.c src/actor_sound.c src/actor_edit.c src/actor_sound_edit.c src/rollcall_storage.c src/chunky_file.c src/string_table.c src/text.c src/collection_storage.c src/collections.c src/collection_headers.c src/data_stream.c src/codec.c src/piece_table.c src/block_io.c src/block_owner.c src/file_text.c src/chunk_save.c src/chunk_heap.c src/chunk_edit.c src/chunk_graph.c src/chunk_ops.c src/chunk_metadata.c src/string_wire.c src/codec_registry.c src/chunk_forest.c src/chunk_loading.c src/chunk_storage.c src/chunk_lifecycle.c src/chunk_container.c src/chunk_context.c src/chunk_owner.c src/chunk_table.c src/tag.c src/tag_list.c src/source.c src/source_names.c src/source_discovery.c src/wide.c src/resource_cache.c src/resource_manager.c)
target_include_directories(three-mm-engine-c89 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_sources(three-mm-engine-c89 PRIVATE src/actor_action_edit.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_costume_edit.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_route_edit.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_subroute_edit.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_placement.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_stage_edit.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_record.c)
target_sources(three-mm-engine-c89 PRIVATE src/actor_transform_edit.c)
set_target_properties(three-mm-engine-c89 PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(three-mm-engine-c89 PRIVATE -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra)
endif()

get_filename_component(engine_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_c89_test.c" ABSOLUTE)
add_executable(engine-c89-test EXCLUDE_FROM_ALL "${engine_test_source}")
target_link_libraries(engine-c89-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-c89-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-c89 COMMAND engine-c89-test)
get_filename_component(engine_movie_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_data_test.c" ABSOLUTE)
add_executable(engine-movie-data-test EXCLUDE_FROM_ALL "${engine_movie_test_source}")
target_link_libraries(engine-movie-data-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-movie-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-data COMMAND engine-movie-data-test)
get_filename_component(engine_chunky_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunky_test.c" ABSOLUTE)
add_executable(engine-chunky-test EXCLUDE_FROM_ALL "${engine_chunky_test_source}")
target_link_libraries(engine-chunky-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunky-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunky COMMAND engine-chunky-test)
get_filename_component(engine_strings_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_strings_test.c" ABSOLUTE)
add_executable(engine-strings-test EXCLUDE_FROM_ALL "${engine_strings_test_source}")
target_link_libraries(engine-strings-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-strings-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-strings COMMAND engine-strings-test)
get_filename_component(engine_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_storage_test.c" ABSOLUTE)
add_executable(engine-storage-test EXCLUDE_FROM_ALL "${engine_storage_test_source}")
target_link_libraries(engine-storage-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-storage COMMAND engine-storage-test)
get_filename_component(engine_collections_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_collections_test.c" ABSOLUTE)
add_executable(engine-collections-test EXCLUDE_FROM_ALL "${engine_collections_test_source}")
target_link_libraries(engine-collections-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-collections-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-collections COMMAND engine-collections-test)
get_filename_component(engine_data_stream_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_data_stream_test.c" ABSOLUTE)
add_executable(engine-data-stream-test EXCLUDE_FROM_ALL "${engine_data_stream_test_source}")
target_link_libraries(engine-data-stream-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-data-stream-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-data-stream COMMAND engine-data-stream-test)
find_program(ENGINE_C89_COMPILER NAMES clang gcc)
get_filename_component(engine_codec_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_codec_test.c" ABSOLUTE)
add_executable(engine-codec-test EXCLUDE_FROM_ALL "${engine_codec_test_source}")
target_link_libraries(engine-codec-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-codec-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-codec COMMAND engine-codec-test)
get_filename_component(engine_piece_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_piece_table_test.c" ABSOLUTE)
add_executable(engine-piece-table-test EXCLUDE_FROM_ALL "${engine_piece_test_source}")
target_link_libraries(engine-piece-table-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-piece-table-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-piece-table COMMAND engine-piece-table-test)
get_filename_component(engine_block_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_block_io_test.c" ABSOLUTE)
add_executable(engine-block-io-test EXCLUDE_FROM_ALL "${engine_block_test_source}")
target_link_libraries(engine-block-io-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-block-io-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-block-io COMMAND engine-block-io-test)
get_filename_component(engine_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_block_owner_test.c" ABSOLUTE)
add_executable(engine-block-owner-test EXCLUDE_FROM_ALL "${engine_owner_test_source}")
target_link_libraries(engine-block-owner-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-block-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-block-owner COMMAND engine-block-owner-test)
get_filename_component(engine_file_policy_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_file_policy_test.c" ABSOLUTE)
add_executable(engine-file-policy-test EXCLUDE_FROM_ALL "${engine_file_policy_test_source}")
target_link_libraries(engine-file-policy-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-file-policy-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-file-policy COMMAND engine-file-policy-test)
get_filename_component(engine_chunk_save_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_save_test.c" ABSOLUTE)
add_executable(engine-chunk-save-test EXCLUDE_FROM_ALL "${engine_chunk_save_test_source}")
target_link_libraries(engine-chunk-save-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-save-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-save COMMAND engine-chunk-save-test)
get_filename_component(engine_chunk_heap_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_heap_test.c" ABSOLUTE)
add_executable(engine-chunk-heap-test EXCLUDE_FROM_ALL "${engine_chunk_heap_test_source}")
target_link_libraries(engine-chunk-heap-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-heap-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-heap COMMAND engine-chunk-heap-test)
get_filename_component(engine_chunk_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_edit_test.c" ABSOLUTE)
add_executable(engine-chunk-edit-test EXCLUDE_FROM_ALL "${engine_chunk_edit_test_source}")
target_link_libraries(engine-chunk-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-edit COMMAND engine-chunk-edit-test)
get_filename_component(engine_chunk_graph_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_graph_test.c" ABSOLUTE)
add_executable(engine-chunk-graph-test EXCLUDE_FROM_ALL "${engine_chunk_graph_test_source}")
target_link_libraries(engine-chunk-graph-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-graph-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-graph COMMAND engine-chunk-graph-test)
get_filename_component(engine_chunk_ops_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_ops_test.c" ABSOLUTE)
add_executable(engine-chunk-ops-test EXCLUDE_FROM_ALL "${engine_chunk_ops_test_source}")
target_link_libraries(engine-chunk-ops-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-ops-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-ops COMMAND engine-chunk-ops-test)
get_filename_component(engine_metadata_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_metadata_test.c" ABSOLUTE)
add_executable(engine-metadata-test EXCLUDE_FROM_ALL "${engine_metadata_test_source}")
target_link_libraries(engine-metadata-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-metadata-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-metadata COMMAND engine-metadata-test)
get_filename_component(engine_forest_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_forest_test.c" ABSOLUTE)
add_executable(engine-forest-test EXCLUDE_FROM_ALL "${engine_forest_test_source}")
target_link_libraries(engine-forest-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-forest-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-forest COMMAND engine-forest-test)
get_filename_component(engine_loading_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_loading_test.c" ABSOLUTE)
add_executable(engine-loading-test EXCLUDE_FROM_ALL "${engine_loading_test_source}")
target_link_libraries(engine-loading-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-loading-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-loading COMMAND engine-loading-test)
get_filename_component(engine_chunk_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_storage_test.c" ABSOLUTE)
add_executable(engine-chunk-storage-test EXCLUDE_FROM_ALL "${engine_chunk_storage_test_source}")
target_link_libraries(engine-chunk-storage-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-storage COMMAND engine-chunk-storage-test)
get_filename_component(engine_lifecycle_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_lifecycle_test.c" ABSOLUTE)
add_executable(engine-lifecycle-test EXCLUDE_FROM_ALL "${engine_lifecycle_test_source}")
target_link_libraries(engine-lifecycle-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-lifecycle-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-lifecycle COMMAND engine-lifecycle-test)
get_filename_component(engine_container_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_container_test.c" ABSOLUTE)
add_executable(engine-container-test EXCLUDE_FROM_ALL "${engine_container_test_source}")
target_link_libraries(engine-container-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-container-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-container COMMAND engine-container-test)
get_filename_component(engine_resource_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_resource_cache_test.c" ABSOLUTE)
add_executable(engine-resource-cache-test EXCLUDE_FROM_ALL "${engine_resource_test_source}")
target_link_libraries(engine-resource-cache-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-resource-cache-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-resource-cache COMMAND engine-resource-cache-test)
get_filename_component(engine_chunk_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_owner_test.c" ABSOLUTE)
add_executable(engine-chunk-owner-test EXCLUDE_FROM_ALL "${engine_chunk_owner_test_source}")
target_link_libraries(engine-chunk-owner-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-owner COMMAND engine-chunk-owner-test)
get_filename_component(engine_chunk_table_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunk_table_test.c" ABSOLUTE)
add_executable(engine-chunk-table-test EXCLUDE_FROM_ALL "${engine_chunk_table_test_source}")
target_link_libraries(engine-chunk-table-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunk-table-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunk-table COMMAND engine-chunk-table-test)
get_filename_component(engine_tag_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_tag_test.c" ABSOLUTE)
add_executable(engine-tag-test EXCLUDE_FROM_ALL "${engine_tag_test_source}")
target_link_libraries(engine-tag-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-tag-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-tag COMMAND engine-tag-test)
get_filename_component(engine_source_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_source_test.c" ABSOLUTE)
add_executable(engine-source-test EXCLUDE_FROM_ALL "${engine_source_test_source}")
target_link_libraries(engine-source-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-source-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-source COMMAND engine-source-test)
get_filename_component(engine_source_names_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_source_names_test.c" ABSOLUTE)
add_executable(engine-source-names-test EXCLUDE_FROM_ALL "${engine_source_names_test_source}")
target_link_libraries(engine-source-names-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-source-names-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-source-names COMMAND engine-source-names-test)
get_filename_component(engine_movie_owner_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_owner_test.c" ABSOLUTE)
add_executable(engine-movie-owner-test EXCLUDE_FROM_ALL "${engine_movie_owner_test_source}")
target_link_libraries(engine-movie-owner-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-movie-owner-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-owner COMMAND engine-movie-owner-test)
get_filename_component(engine_rollcall_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_rollcall_storage_test.c" ABSOLUTE)
add_executable(engine-rollcall-storage-test EXCLUDE_FROM_ALL "${engine_rollcall_storage_test_source}")
target_link_libraries(engine-rollcall-storage-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-rollcall-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-rollcall-storage COMMAND engine-rollcall-storage-test)
get_filename_component(engine_movie_title_root_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_title_root_test.c" ABSOLUTE)
add_executable(engine-movie-title-root-test EXCLUDE_FROM_ALL "${engine_movie_title_root_test_source}")
target_link_libraries(engine-movie-title-root-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-movie-title-root-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-title-root COMMAND engine-movie-title-root-test)
get_filename_component(engine_scene_data_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_scene_data_test.c" ABSOLUTE)
add_executable(engine-scene-data-test EXCLUDE_FROM_ALL "${engine_scene_data_test_source}")
target_link_libraries(engine-scene-data-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-scene-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-scene-data COMMAND engine-scene-data-test)
get_filename_component(engine_actor_data_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_data_test.c" ABSOLUTE)
add_executable(engine-actor-data-test EXCLUDE_FROM_ALL "${engine_actor_data_test_source}")
target_link_libraries(engine-actor-data-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-data COMMAND engine-actor-data-test)
get_filename_component(engine_actor_track_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_track_test.c" ABSOLUTE)
add_executable(engine-actor-track-test EXCLUDE_FROM_ALL "${engine_actor_track_test_source}")
target_link_libraries(engine-actor-track-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-track-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-track COMMAND engine-actor-track-test)
get_filename_component(engine_actor_motion_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_motion_test.c" ABSOLUTE)
add_executable(engine-actor-motion-test EXCLUDE_FROM_ALL "${engine_actor_motion_test_source}")
target_link_libraries(engine-actor-motion-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-motion-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-motion COMMAND engine-actor-motion-test)
get_filename_component(engine_actor_frame_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_frame_test.c" ABSOLUTE)
add_executable(engine-actor-frame-test EXCLUDE_FROM_ALL "${engine_actor_frame_test_source}")
target_link_libraries(engine-actor-frame-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-frame-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-frame COMMAND engine-actor-frame-test)
get_filename_component(engine_actor_timeline_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_timeline_test.c" ABSOLUTE)
add_executable(engine-actor-timeline-test EXCLUDE_FROM_ALL "${engine_actor_timeline_test_source}")
target_link_libraries(engine-actor-timeline-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-timeline-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-timeline COMMAND engine-actor-timeline-test)
get_filename_component(engine_actor_pose_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_pose_test.c" ABSOLUTE)
add_executable(engine-actor-pose-test EXCLUDE_FROM_ALL "${engine_actor_pose_test_source}" "${engine_actor_effect_test_source}" "${engine_actor_sound_test_source}" "${engine_sound_edit_test_source}")
target_link_libraries(engine-actor-pose-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-pose-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-pose COMMAND engine-actor-pose-test)
get_filename_component(engine_actor_effect_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_effect_test.c" ABSOLUTE)
add_executable(engine-actor-effect-test EXCLUDE_FROM_ALL "${engine_actor_effect_test_source}")
target_link_libraries(engine-actor-effect-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-effect-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-effect COMMAND engine-actor-effect-test)
get_filename_component(engine_actor_sound_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_actor_sound_test.c" ABSOLUTE)
add_executable(engine-actor-sound-test EXCLUDE_FROM_ALL "${engine_actor_sound_test_source}")
target_link_libraries(engine-actor-sound-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-actor-sound-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-actor-sound COMMAND engine-actor-sound-test)
get_filename_component(engine_sound_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_sound_edit_test.c" ABSOLUTE)
add_executable(engine-sound-edit-test EXCLUDE_FROM_ALL "${engine_sound_edit_test_source}")
target_link_libraries(engine-sound-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-sound-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-sound-edit COMMAND engine-sound-edit-test)
get_filename_component(engine_event_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_event_edit_test.c" ABSOLUTE)
add_executable(engine-event-edit-test EXCLUDE_FROM_ALL "${engine_event_edit_test_source}")
target_link_libraries(engine-event-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-event-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-event-edit COMMAND engine-event-edit-test)
get_filename_component(engine_action_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_action_edit_test.c" ABSOLUTE)
add_executable(engine-action-edit-test EXCLUDE_FROM_ALL "${engine_action_edit_test_source}")
target_link_libraries(engine-action-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-action-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-action-edit COMMAND engine-action-edit-test)
get_filename_component(engine_costume_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_costume_edit_test.c" ABSOLUTE)
add_executable(engine-costume-edit-test EXCLUDE_FROM_ALL "${engine_costume_edit_test_source}")
target_link_libraries(engine-costume-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-costume-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-costume-edit COMMAND engine-costume-edit-test)
get_filename_component(engine_route_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_route_edit_test.c" ABSOLUTE)
add_executable(engine-route-edit-test EXCLUDE_FROM_ALL "${engine_route_edit_test_source}")
target_link_libraries(engine-route-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-route-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-route-edit COMMAND engine-route-edit-test)
get_filename_component(engine_subroute_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_subroute_edit_test.c" ABSOLUTE)
add_executable(engine-subroute-edit-test EXCLUDE_FROM_ALL "${engine_subroute_edit_test_source}")
target_link_libraries(engine-subroute-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-subroute-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-subroute-edit COMMAND engine-subroute-edit-test)
get_filename_component(engine_placement_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_placement_test.c" ABSOLUTE)
add_executable(engine-placement-test EXCLUDE_FROM_ALL "${engine_placement_test_source}")
target_link_libraries(engine-placement-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-placement-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-placement COMMAND engine-placement-test)
get_filename_component(engine_stage_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_stage_edit_test.c" ABSOLUTE)
add_executable(engine-stage-edit-test EXCLUDE_FROM_ALL "${engine_stage_edit_test_source}")
target_link_libraries(engine-stage-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-stage-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-stage-edit COMMAND engine-stage-edit-test)
get_filename_component(engine_record_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_record_test.c" ABSOLUTE)
add_executable(engine-record-test EXCLUDE_FROM_ALL "${engine_record_test_source}")
target_link_libraries(engine-record-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-record-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-record COMMAND engine-record-test)
get_filename_component(engine_transform_edit_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_transform_edit_test.c" ABSOLUTE)
add_executable(engine-transform-edit-test EXCLUDE_FROM_ALL "${engine_transform_edit_test_source}")
target_link_libraries(engine-transform-edit-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-transform-edit-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-transform-edit COMMAND engine-transform-edit-test)
if(ENGINE_C89_COMPILER)
  add_test(NAME engine-c89-syntax COMMAND "${ENGINE_C89_COMPILER}"
    -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror -fsyntax-only
    "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/playback.c"
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
    "${engine_test_source}" "${engine_movie_test_source}" "${engine_chunky_test_source}" "${engine_strings_test_source}" "${engine_storage_test_source}" "${engine_collections_test_source}" "${engine_data_stream_test_source}" "${engine_piece_test_source}" "${engine_codec_test_source}" "${engine_block_test_source}" "${engine_owner_test_source}" "${engine_file_policy_test_source}" "${engine_chunk_save_test_source}" "${engine_chunk_heap_test_source}" "${engine_chunk_edit_test_source}" "${engine_chunk_graph_test_source}" "${engine_chunk_ops_test_source}" "${engine_metadata_test_source}" "${engine_forest_test_source}" "${engine_loading_test_source}" "${engine_chunk_storage_test_source}" "${engine_lifecycle_test_source}" "${engine_container_test_source}" "${engine_resource_test_source}" "${engine_chunk_owner_test_source}" "${engine_chunk_table_test_source}" "${engine_tag_test_source}" "${engine_source_test_source}" "${engine_source_names_test_source}" "${engine_movie_owner_test_source}" "${engine_rollcall_storage_test_source}" "${engine_movie_title_root_test_source}" "${engine_scene_data_test_source}" "${engine_actor_data_test_source}" "${engine_actor_track_test_source}" "${engine_actor_motion_test_source}" "${engine_actor_frame_test_source}" "${engine_actor_timeline_test_source}" "${engine_actor_pose_test_source}")
endif()
