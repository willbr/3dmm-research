include(SDL3Dependency)
include(ImGuizmoDependency)
include(CimguiDependency)
include(FreeTypeDependency)
include(EmbedMarcusShaders)

add_library(marcus-player STATIC player.cpp storyboard.cpp storyboard_tools.cpp storyboard_export.cpp colour_picker.cpp camera_guide.c camera_controls.c content.cpp playback.cpp movie_input.cpp scene_edit.cpp caption_quad.cpp gpu_renderer.cpp asm_renderer.cpp renderer_ids.c waveform.cpp sound_timeline.cpp timing_job.cpp frame_previews.cpp preview_protocol.cpp preview_worker.cpp text_preview.cpp)
if(WIN32)
  target_compile_definitions(marcus-player PRIVATE MARCUS_ASM_WORKER_PATH="${PROJECT_SOURCE_DIR}/build/marcus-x86/src/marcus/marcus-asm-worker.exe")
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(marcus-asm-worker EXCLUDE_FROM_ALL asm_worker.cpp)
    target_compile_definitions(marcus-asm-worker PRIVATE MARCUS_X86_ASM=1)
    target_compile_features(marcus-asm-worker PRIVATE cxx_std_17)
    target_link_libraries(marcus-asm-worker PRIVATE marcus-player MM::Preview)
    kauai_copy_midi_assets(marcus-asm-worker)
  endif()
endif()
target_compile_features(marcus-player PRIVATE cxx_std_17)
target_include_directories(marcus-player PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(marcus-player PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/bren/inc")
target_link_libraries(marcus-player PRIVATE MM::Preview mm-engine-c89 studio-pack-store studio-settings SDL3::SDL3-static
  kauai-sdl-audio kauai-midi-synth kauai-font-host miniz Threads::Threads)
target_link_libraries(marcus-player PUBLIC mm-v3dmm)
target_compile_definitions(marcus-player PRIVATE MARCUS_MIDI_BANK="${KAUAI_MIDI_BANK_FILE}")

# Explicitly built, headless playback diagnostic; no UI input simulation.
add_executable(marcus-playback EXCLUDE_FROM_ALL ../../tools/marcus_playback.c)
set_target_properties(marcus-playback PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(marcus-playback PRIVATE marcus-player)

# Playback-quality capture through the C API; never creates an application UI.
foreach(capture_target IN ITEMS marcus-regression marcus-capture)
  add_executable(${capture_target} EXCLUDE_FROM_ALL ../../tools/marcus_regression.cpp)
  target_compile_features(${capture_target} PRIVATE cxx_std_17)
  target_link_libraries(${capture_target} PRIVATE marcus-player SDL3::SDL3-static)
  kauai_copy_midi_assets(${capture_target})
endforeach()

add_executable(marcus-preview preview_helper.cpp browser_preview.cpp)
target_compile_features(marcus-preview PRIVATE cxx_std_17)
target_link_libraries(marcus-preview PRIVATE marcus-player MM::Archive engine-core)
kauai_copy_midi_assets(marcus-preview)

add_executable(marcus WIN32 main.c ribbon.c ui.c settings.c prop_gizmo.cpp movie_browser.cpp scene_organiser.cpp background_browser.cpp text_dialog.c costume_dialog.c camera_picker.cpp storyboard_editor.cpp preview_cache.cpp)
add_executable(marcus-dialog-capture EXCLUDE_FROM_ALL ../../tools/marcus_dialog_capture.c ui.c)
set_target_properties(marcus-dialog-capture PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(marcus-dialog-capture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(marcus-dialog-capture PRIVATE SDL3::SDL3-static freetype)
add_dependencies(marcus marcus-preview)
find_package(Threads REQUIRED)
target_compile_features(marcus PRIVATE cxx_std_17)
set_target_properties(marcus PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(marcus PRIVATE MARCUS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(marcus PRIVATE marcus-player cimgui-sdl3 imguizmo SDL3::SDL3-static freetype engine-core MM::Archive mm-v3dmm Threads::Threads)
kauai_copy_midi_assets(marcus)
add_custom_command(TARGET marcus POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/licenses/imgui-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/audio-codecs.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/dr-mp3-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/libgsm-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/freetype-FTL.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/freetype-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/sqlite-PUBLIC-DOMAIN.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/ok-color-LICENSE.txt"
    "$<TARGET_FILE_DIR:marcus>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${marcus_cimgui_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:marcus>/licenses/cimgui-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/vendor/ImGuizmo/LICENSE" "$<TARGET_FILE_DIR:marcus>/licenses/ImGuizmo-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/vendor/SDL3/LICENSE.txt" "$<TARGET_FILE_DIR:marcus>/licenses/SDL3-LICENSE.txt"
  VERBATIM)
install(FILES "${PROJECT_SOURCE_DIR}/docs/licenses/ok-color-LICENSE.txt" DESTINATION licenses)
# SDL's headers use platform integer/bool extensions. Keep diagnostics on our
# C89 sources strict while treating external declarations as system headers.
target_include_directories(marcus SYSTEM PRIVATE
  "$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>")
if(NOT WIN32)
  target_link_libraries(marcus PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(marcus PRIVATE "$<$<COMPILE_LANGUAGE:C>:-pedantic-errors;-Werror=declaration-after-statement>")
endif()
if(BUILD_TESTING)
  # Shared Fly navigation math; no application window or input simulation.
  add_executable(marcus-camera-controls-test EXCLUDE_FROM_ALL ../../tests/marcus_camera_controls_test.c camera_controls.c)
  set_target_properties(marcus-camera-controls-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_include_directories(marcus-camera-controls-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(marcus-camera-controls-test PRIVATE SDL3::SDL3-static)
  if(NOT WIN32)
    target_link_libraries(marcus-camera-controls-test PRIVATE m)
  endif()
  add_test(NAME marcus-camera-controls COMMAND marcus-camera-controls-test)
  set(marcus_font_fixture "${KAUAI_FONT_TEST_TTF}")
  if(WIN32 AND NOT marcus_font_fixture AND EXISTS "$ENV{WINDIR}/Fonts/arial.ttf")
    set(marcus_font_fixture "$ENV{WINDIR}/Fonts/arial.ttf")
  endif()
  # Headless preview protocol/process checks; no application UI or input simulation.
  add_executable(marcus-preview-worker-test EXCLUDE_FROM_ALL ../../tests/marcus_preview_worker_test.cpp preview_protocol.cpp preview_worker.cpp)
  target_compile_features(marcus-preview-worker-test PRIVATE cxx_std_17)
  target_include_directories(marcus-preview-worker-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(marcus-preview-worker-test PRIVATE SDL3::SDL3-static)
  add_dependencies(marcus-preview-worker-test marcus-preview)
  add_test(NAME marcus-preview-worker COMMAND marcus-preview-worker-test)
  set_tests_properties(marcus-preview-worker PROPERTIES TIMEOUT 15)
  # Persistent preview cache checks; no application window or UI simulation.
  add_executable(marcus-preview-cache-test EXCLUDE_FROM_ALL ../../tests/marcus_preview_cache_test.cpp preview_cache.cpp preview_protocol.cpp)
  target_compile_features(marcus-preview-cache-test PRIVATE cxx_std_17)
  target_include_directories(marcus-preview-cache-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(marcus-preview-cache-test PRIVATE SDL3::SDL3-static)
  add_test(NAME marcus-preview-cache COMMAND marcus-preview-cache-test "${CMAKE_CURRENT_BINARY_DIR}/test-data/preview-cache")
  # Native GPU rasterization checks; no application window or UI simulation.
  add_executable(marcus-renderer-test EXCLUDE_FROM_ALL ../../tests/marcus_renderer_test.cpp settings.c)
  target_compile_features(marcus-renderer-test PRIVATE cxx_std_17)
  target_include_directories(marcus-renderer-test PRIVATE "${PROJECT_SOURCE_DIR}/bren/inc")
  target_link_libraries(marcus-renderer-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  add_test(NAME marcus-renderer COMMAND marcus-renderer-test)
  set_tests_properties(marcus-renderer PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  add_executable(marcus-raster-parity-test EXCLUDE_FROM_ALL ../../tests/marcus_raster_parity_test.cpp)
  target_compile_features(marcus-raster-parity-test PRIVATE cxx_std_17)
  target_include_directories(marcus-raster-parity-test PRIVATE "${PROJECT_SOURCE_DIR}/bren/lib/zb"
    "${PROJECT_SOURCE_DIR}/bren/lib/fw")
  target_link_libraries(marcus-raster-parity-test PRIVATE marcus-player MM::Preview engine-core)
  add_test(NAME marcus-raster-parity COMMAND marcus-raster-parity-test)
  set_tests_properties(marcus-raster-parity PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  if(WIN32)
    add_test(NAME marcus-raster-parity-vulkan COMMAND marcus-raster-parity-test)
    set_tests_properties(marcus-raster-parity-vulkan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
      ENVIRONMENT "SDL_GPU_DRIVER=vulkan")
  endif()
  add_executable(marcus-asm-test EXCLUDE_FROM_ALL ../../tests/marcus_asm_test.cpp)
  target_link_libraries(marcus-asm-test PRIVATE marcus-player MM::Preview)
  target_compile_features(marcus-asm-test PRIVATE cxx_std_17)
  add_test(NAME marcus-asm COMMAND marcus-asm-test "${PROJECT_SOURCE_DIR}/movies/stranger1.zip"
    "${PROJECT_SOURCE_DIR}/build/studio-portable-x64/3dmovie/utest.chk" "${PROJECT_SOURCE_DIR}/content-files")
  set_tests_properties(marcus-asm PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_executable(marcus-caption-quad-test EXCLUDE_FROM_ALL ../../tests/marcus_caption_quad_test.cpp)
  target_link_libraries(marcus-caption-quad-test PRIVATE marcus-player MM::Preview)
  add_test(NAME marcus-caption-quad COMMAND marcus-caption-quad-test)
  add_executable(marcus-zip-test EXCLUDE_FROM_ALL ../../tests/marcus_zip_test.cpp)
  target_link_libraries(marcus-zip-test PRIVATE marcus-player miniz studio-pack-store kauai-font-host)
  add_test(NAME marcus-zip COMMAND marcus-zip-test "${PROJECT_SOURCE_DIR}/tmp/marcus-zip" "${marcus_font_fixture}")
  set_tests_properties(marcus-zip PROPERTIES TIMEOUT 30)
  add_executable(marcus-playback-test EXCLUDE_FROM_ALL ../../tests/marcus_playback_test.cpp)
  target_compile_features(marcus-playback-test PRIVATE cxx_std_17)
  add_dependencies(marcus-playback-test marcus-preview)
  target_link_libraries(marcus-playback-test PRIVATE marcus-player MM::Preview engine-core SDL3::SDL3-static miniz)
  kauai_copy_midi_assets(marcus-playback-test)
  # Native content adapter regression; no UI input or application window.
  add_executable(marcus-content-test EXCLUDE_FROM_ALL ../../tests/marcus_content_test.cpp)
  target_link_libraries(marcus-content-test PRIVATE marcus-player studio-pack-store studio-settings engine-core miniz SDL3::SDL3-static)
  add_test(NAME marcus-content COMMAND marcus-content-test "${PROJECT_SOURCE_DIR}/tmp/marcus-content")
  set_tests_properties(marcus-content PROPERTIES TIMEOUT 30)
  find_program(MARCUS_C89_COMPILER NAMES clang gcc)
  if(MARCUS_C89_COMPILER)
    add_test(NAME marcus-c89 COMMAND ${CMAKE_COMMAND}
      "-DCOMPILER=${MARCUS_C89_COMPILER}"
      "-DSDL_INCLUDES=$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>"
      "-DFREETYPE_INCLUDES=$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>"
      "-DCIMGUI_INCLUDES=${marcus_cimgui_SOURCE_DIR}"
      -DSOURCE=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tests/check_marcus_c89.cmake)
  endif()
  # Headless release latency/cache checks; no application window or UI input.
  add_executable(marcus-prop-move-test EXCLUDE_FROM_ALL ../../tests/marcus_prop_move_test.cpp)
  target_compile_features(marcus-prop-move-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-prop-move-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  add_executable(marcus-shape-test EXCLUDE_FROM_ALL ../../tests/marcus_shape_test.cpp)
  add_executable(marcus-insert-test EXCLUDE_FROM_ALL ../../tests/marcus_insert_test.cpp)
  add_executable(marcus-costume-test EXCLUDE_FROM_ALL ../../tests/marcus_costume_test.cpp)
  target_compile_features(marcus-costume-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-costume-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  kauai_copy_midi_assets(marcus-costume-test)
  target_compile_features(marcus-insert-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-insert-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  add_dependencies(marcus-insert-test marcus-preview)
  kauai_copy_midi_assets(marcus-insert-test)
  add_executable(marcus-storyboard-test EXCLUDE_FROM_ALL ../../tests/marcus_storyboard_test.cpp)
  target_compile_features(marcus-storyboard-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-storyboard-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  add_dependencies(marcus-storyboard-test marcus-preview)
  kauai_copy_midi_assets(marcus-storyboard-test)
  target_compile_features(marcus-shape-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-shape-test PRIVATE marcus-player MM::Preview SDL3::SDL3-static)
  add_executable(marcus-player-test EXCLUDE_FROM_ALL ../../tests/marcus_player_test.c)
  set_target_properties(marcus-player-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(marcus-player-test PRIVATE marcus-player)
  # Headless frame helper lifecycle/pixel checks; no application UI input.
  add_executable(marcus-frame-previews-test EXCLUDE_FROM_ALL ../../tests/marcus_frame_previews_test.cpp)
  target_compile_features(marcus-frame-previews-test PRIVATE cxx_std_17)
  target_link_libraries(marcus-frame-previews-test PRIVATE marcus-player SDL3::SDL3-static)
  add_dependencies(marcus-frame-previews-test marcus-preview)
  add_dependencies(marcus-player-test marcus-frame-previews-test marcus-prop-move-test marcus-shape-test marcus-storyboard-test marcus-insert-test marcus-costume-test)
  if(EXISTS "${PROJECT_SOURCE_DIR}/content-files/bkgds.3cn")
    set(marcus_resources "${PROJECT_SOURCE_DIR}/build/studio-portable-x64/3dmovie/utest.chk"
      CACHE FILEPATH "3dmovie.chk or built utest.chk for Marcus integration tests")
    if(EXISTS "${marcus_resources}")
      add_test(NAME marcus-costume COMMAND marcus-costume-test "${marcus_resources}"
        "${PROJECT_SOURCE_DIR}/content-files" "${CMAKE_CURRENT_BINARY_DIR}/test-data/costume/movie.3mm")
      set_tests_properties(marcus-costume PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
      add_test(NAME marcus-insert COMMAND marcus-insert-test "${marcus_resources}" "${PROJECT_SOURCE_DIR}/content-files"
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/inserted.3mm")
      set_tests_properties(marcus-insert PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
      add_test(NAME marcus-storyboard COMMAND marcus-storyboard-test "${marcus_resources}" "${PROJECT_SOURCE_DIR}/content-files"
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/storyboard.vmm")
      set_tests_properties(marcus-storyboard PROPERTIES TIMEOUT 90 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-data")
      add_test(NAME marcus-playback COMMAND marcus-playback-test
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/presentation.3mm" "${marcus_resources}" "${PROJECT_SOURCE_DIR}/content-files"
        "${marcus_font_fixture}")
      set_tests_properties(marcus-playback PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
      add_test(NAME marcus-player COMMAND ${CMAKE_COMMAND}
        -DAPI_EXE=$<TARGET_FILE:marcus-player-test>
        -DPROP_MOVE_EXE=$<TARGET_FILE:marcus-prop-move-test>
        -DSHAPE_EXE=$<TARGET_FILE:marcus-shape-test>
        -DFRAME_PREVIEW_EXE=$<TARGET_FILE:marcus-frame-previews-test>
        -DFIXTURE_EXE=$<TARGET_FILE:preview-scene-fixture>
        -DRESOURCES=${marcus_resources} -DCONTENT=${PROJECT_SOURCE_DIR}/content-files
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${PROJECT_SOURCE_DIR}/tests/check_marcus.cmake)
    endif()
  endif()
endif()
