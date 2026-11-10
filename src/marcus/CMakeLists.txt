include(SDL3Dependency)
include(FreeTypeDependency)
include(EmbedMarcusShaders)

add_library(marcus-player STATIC player.cpp content.cpp playback.cpp movie_input.cpp caption_quad.cpp gpu_renderer.cpp renderer_ids.c waveform.cpp sound_timeline.cpp)
target_compile_features(marcus-player PRIVATE cxx_std_17)
target_include_directories(marcus-player PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(marcus-player PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/bren/inc")
target_link_libraries(marcus-player PRIVATE ThreeMM::Preview studio-pack-store studio-settings SDL3::SDL3-static
  kauai-sdl-audio kauai-midi-synth kauai-font-host miniz)
target_compile_definitions(marcus-player PRIVATE MARCUS_MIDI_BANK="${KAUAI_MIDI_BANK_FILE}")

# Explicitly built, headless playback diagnostic; no UI input simulation.
add_executable(marcus-playback EXCLUDE_FROM_ALL ../../tools/marcus_playback.c)
set_target_properties(marcus-playback PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(marcus-playback PRIVATE marcus-player)

add_executable(marcus WIN32 main.c ui.c settings.c camera_guide.c)
set_target_properties(marcus PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(marcus PRIVATE MARCUS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(marcus PRIVATE marcus-player SDL3::SDL3-static freetype)
kauai_copy_midi_assets(marcus)
add_custom_command(TARGET marcus POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/licenses/audio-codecs.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/dr-mp3-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/libgsm-LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/freetype-FTL.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/freetype-LICENSE.txt"
    "$<TARGET_FILE_DIR:marcus>/licenses"
  VERBATIM)
# SDL's headers use platform integer/bool extensions. Keep diagnostics on our
# C89 sources strict while treating external declarations as system headers.
target_include_directories(marcus SYSTEM PRIVATE
  "$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>")
if(NOT WIN32)
  target_link_libraries(marcus PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(marcus PRIVATE -pedantic-errors -Werror=declaration-after-statement)
endif()
if(BUILD_TESTING)
  # Native GPU rasterization checks; no application window or UI simulation.
  add_executable(marcus-renderer-test EXCLUDE_FROM_ALL ../../tests/marcus_renderer_test.cpp settings.c)
  target_compile_features(marcus-renderer-test PRIVATE cxx_std_17)
  target_include_directories(marcus-renderer-test PRIVATE "${PROJECT_SOURCE_DIR}/bren/inc")
  target_link_libraries(marcus-renderer-test PRIVATE marcus-player ThreeMM::Preview SDL3::SDL3-static)
  add_test(NAME marcus-renderer COMMAND marcus-renderer-test)
  set_tests_properties(marcus-renderer PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  add_executable(marcus-caption-quad-test EXCLUDE_FROM_ALL ../../tests/marcus_caption_quad_test.cpp)
  target_link_libraries(marcus-caption-quad-test PRIVATE marcus-player ThreeMM::Preview)
  add_test(NAME marcus-caption-quad COMMAND marcus-caption-quad-test)
  add_executable(marcus-zip-test EXCLUDE_FROM_ALL ../../tests/marcus_zip_test.cpp)
  target_link_libraries(marcus-zip-test PRIVATE marcus-player miniz studio-pack-store)
  add_test(NAME marcus-zip COMMAND marcus-zip-test "${PROJECT_SOURCE_DIR}/tmp/marcus-zip")
  set_tests_properties(marcus-zip PROPERTIES TIMEOUT 30)
  add_executable(marcus-playback-test EXCLUDE_FROM_ALL ../../tests/marcus_playback_test.cpp)
  target_link_libraries(marcus-playback-test PRIVATE marcus-player ThreeMM::Preview engine-core SDL3::SDL3-static)
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
      -DSOURCE=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tests/check_marcus_c89.cmake)
  endif()
  add_executable(marcus-player-test EXCLUDE_FROM_ALL ../../tests/marcus_player_test.c)
  set_target_properties(marcus-player-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(marcus-player-test PRIVATE marcus-player)
  if(EXISTS "${PROJECT_SOURCE_DIR}/content-files/bkgds.3cn")
    set(marcus_resources "${PROJECT_SOURCE_DIR}/build/studio-portable-x64/3dmovie/utest.chk"
      CACHE FILEPATH "3dmovie.chk or built utest.chk for Marcus integration tests")
    if(EXISTS "${marcus_resources}")
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-data")
      add_test(NAME marcus-playback COMMAND marcus-playback-test
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/presentation.3mm" "${marcus_resources}" "${PROJECT_SOURCE_DIR}/content-files")
      set_tests_properties(marcus-playback PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
      add_test(NAME marcus-player COMMAND ${CMAKE_COMMAND}
        -DAPI_EXE=$<TARGET_FILE:marcus-player-test>
        -DFIXTURE_EXE=$<TARGET_FILE:preview-scene-fixture>
        -DRESOURCES=${marcus_resources} -DCONTENT=${PROJECT_SOURCE_DIR}/content-files
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${PROJECT_SOURCE_DIR}/tests/check_marcus.cmake)
    endif()
  endif()
endif()
