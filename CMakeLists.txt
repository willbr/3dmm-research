cmake_minimum_required(VERSION 3.22)
project(3DMMForever LANGUAGES CXX C VERSION 0.2)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
  # permissive forces C++14 const char* as default for literals
  # this flag re-enables the auto-conversion for the microsoft compiler
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:strictStrings-")
endif()

include(CMakeDependentOption)
include(TargetChompSources)

find_package(AudioMan)
add_subdirectory(bren/lib)
find_package(BRender REQUIRED)

# Options
add_library(kauai-wave-codec STATIC kauai/src/wave_codec.cpp)
target_include_directories(kauai-wave-codec PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
add_executable(wave-codec-test EXCLUDE_FROM_ALL tests/wave_codec_test.cpp)
target_link_libraries(wave-codec-test PRIVATE kauai-wave-codec)
add_library(kauai-recording-data STATIC kauai/src/recording_data.cpp)
target_include_directories(kauai-recording-data PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
target_link_libraries(kauai-recording-data PUBLIC kauai-wave-codec)
add_executable(recording-data-test EXCLUDE_FROM_ALL tests/recording_data_test.cpp)
target_link_libraries(recording-data-test PRIVATE kauai-recording-data)
add_library(kauai-audio-mixer STATIC kauai/src/audio_mixer.cpp)
target_include_directories(kauai-audio-mixer PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
target_link_libraries(kauai-audio-mixer PUBLIC Threads::Threads)
add_executable(audio-mixer-test EXCLUDE_FROM_ALL tests/audio_mixer_test.cpp)
target_link_libraries(audio-mixer-test PRIVATE kauai-audio-mixer)
add_library(kauai-damage STATIC kauai/src/damage_map.cpp)
target_include_directories(kauai-damage PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
add_executable(damage-map-test EXCLUDE_FROM_ALL tests/damage_map_test.cpp)
target_link_libraries(damage-map-test PRIVATE kauai-damage)
add_library(studio-content STATIC src/studio/content.cpp
  $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/src/studio/content_win.cpp>
  $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/src/studio/content_posix.cpp>)
target_include_directories(studio-content PUBLIC "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/kauai/src")
add_library(studio-mcp-io STATIC src/studio/mcp_io.cpp src/studio/mcp_png.cpp)
target_include_directories(studio-mcp-io PUBLIC "${PROJECT_SOURCE_DIR}/src/studio")
target_link_libraries(studio-mcp-io PUBLIC Threads::Threads)
add_executable(mcp-io-test EXCLUDE_FROM_ALL tests/mcp_io_test.cpp)
add_executable(presentation-capture-test EXCLUDE_FROM_ALL tests/presentation_capture_test.cpp)
target_compile_features(presentation-capture-test PRIVATE cxx_std_17)
target_link_libraries(presentation-capture-test PRIVATE Threads::Threads)
add_test(NAME presentation-capture COMMAND presentation-capture-test)
target_link_libraries(mcp-io-test PRIVATE studio-mcp-io)
if(WIN32)
  target_link_libraries(studio-content PRIVATE advapi32)
endif()
add_executable(studio-content-test EXCLUDE_FROM_ALL tests/studio_content_test.cpp)
target_link_libraries(studio-content-test PRIVATE studio-content)
add_library(studio-iso-cache STATIC src/studio/iso_cache.cpp)
target_compile_features(studio-iso-cache PUBLIC cxx_std_17)
target_include_directories(studio-iso-cache PUBLIC "${PROJECT_SOURCE_DIR}/src/studio")
add_executable(studio-iso-cache-test EXCLUDE_FROM_ALL tests/studio_iso_cache_test.cpp)
target_link_libraries(studio-iso-cache-test PRIVATE studio-iso-cache Threads::Threads)
add_executable(studio-startup-test EXCLUDE_FROM_ALL tests/studio_startup_test.cpp kauai/src/launch_context.cpp)
target_include_directories(studio-startup-test PRIVATE "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/kauai/src")
add_executable(studio-entry-test EXCLUDE_FROM_ALL tests/studio_entry_test.cpp
  kauai/src/app_entry_posix.cpp kauai/src/launch_context.cpp)
target_include_directories(studio-entry-test PRIVATE "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/kauai/src")
add_executable(studio-shortcuts-test EXCLUDE_FROM_ALL tests/studio_shortcuts_test.cpp)
target_include_directories(studio-shortcuts-test PRIVATE "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/kauai/src")
if(WIN32)
  target_link_libraries(studio-shortcuts-test PRIVATE user32)
endif()
option(BUILD_PACKAGES "Build packages for 3DMM" ON)
option(BUILD_STUDIO "Build the Studio application and content" ON)
option(BUILD_INSPECTOR "Build the SDL3 / Dear ImGui archive inspector" OFF)
option(BUILD_MARCUS "Build the read-only C89 Marcus movie player" OFF)
option(STUDIO_SDL3 "Use the portable SDL3 host for the existing Studio" OFF)
option(STUDIO_PORTABLE_RUNTIME "Exercise the non-native Studio runtime, including on Windows" OFF)
option(STUDIO_TEST_TOOLS "Enable opt-in MCP and batch playback tools, including in Release builds" OFF)
option(KAUAI_EXPENSIVE_CHECKS "Enable recursive object and allocation validation in Debug builds" OFF)
if(STUDIO_TEST_TOOLS)
  add_compile_definitions(STUDIO_TEST_TOOLS)
endif()
if(NOT WIN32 AND BUILD_STUDIO AND NOT STUDIO_SDL3)
  message(FATAL_ERROR "Mac/Linux Studio requires STUDIO_SDL3=ON; use the sdl-debug preset or BUILD_STUDIO=OFF for core libraries")
endif()
if(STUDIO_PORTABLE_RUNTIME AND NOT STUDIO_SDL3)
  message(FATAL_ERROR "STUDIO_PORTABLE_RUNTIME requires STUDIO_SDL3=ON")
endif()
set(STUDIO_NATIVE_WINDOWS ${WIN32})
if(STUDIO_PORTABLE_RUNTIME)
  set(STUDIO_NATIVE_WINDOWS OFF)
endif()
option(BUILD_SDL_HOST "Build the portable SDL window host independently of Studio" OFF)
if(STUDIO_SDL3 OR BUILD_SDL_HOST)
  include(SDL3Dependency)
  add_library(kauai-sdl-host STATIC kauai/src/sdl_host.cpp kauai/src/sdl_surface.cpp)
  target_include_directories(kauai-sdl-host PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-sdl-host PRIVATE SDL3::SDL3-static kauai-damage)
  add_executable(sdl-host-test EXCLUDE_FROM_ALL tests/sdl_host_test.cpp)
  target_link_libraries(sdl-host-test PRIVATE kauai-sdl-host SDL3::SDL3-static)
  add_executable(sdl-surface-test EXCLUDE_FROM_ALL tests/sdl_surface_test.cpp)
  target_link_libraries(sdl-surface-test PRIVATE kauai-sdl-host kauai-core)
  add_executable(window-services-sdl-test EXCLUDE_FROM_ALL tests/window_services_sdl_test.cpp)
  target_link_libraries(window-services-sdl-test PRIVATE kauai-sdl-host SDL3::SDL3-static)
endif()
option(BUILD_SDL_DIALOGS "Build the portable SDL3/ImGui dialog host independently of Studio" OFF)
option(KAUAI_FREETYPE "Build the portable FreeType font provider" ${STUDIO_SDL3})
if((STUDIO_PORTABLE_RUNTIME OR (STUDIO_SDL3 AND NOT WIN32)) AND NOT KAUAI_FREETYPE)
  message(FATAL_ERROR "The portable Studio runtime requires KAUAI_FREETYPE=ON")
endif()
option(KAUAI_MIDI_SYNTH "Build portable SoundFont MIDI synthesis" ${STUDIO_SDL3})
if(KAUAI_MIDI_SYNTH OR BUILD_MARCUS)
  include(TinySoundFontDependency)
  add_library(kauai-midi-synth STATIC kauai/src/midi_synth.cpp kauai/src/midi_render.cpp)
  target_include_directories(kauai-midi-synth PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src" PRIVATE "${KAUAI_TSF_INCLUDE_DIR}")
  target_link_libraries(kauai-midi-synth PUBLIC Threads::Threads)
  add_executable(midi-synth-test EXCLUDE_FROM_ALL tests/midi_synth_test.cpp)
  target_link_libraries(midi-synth-test PRIVATE kauai-midi-synth)
  install(FILES "${KAUAI_MIDI_BANK_FILE}" DESTINATION assets RENAME GeneralUserGS.sf2)
  install(FILES docs/licenses/tinysoundfont-LICENSE.txt docs/licenses/tinysoundfont-PATCHES.md
    docs/licenses/generaluser-gs-LICENSE.txt DESTINATION licenses)
endif()
option(BUILD_SDL_MIDI "Build portable SDL MIDI output independently of Studio" OFF)
option(BUILD_SDL_VIDEO "Build portable SDL video playback independently of Studio" OFF)
if(STUDIO_SDL3 OR BUILD_SDL_VIDEO)
  include(SDL3Dependency)
  add_library(kauai-sdl-video STATIC kauai/src/video_sdl.cpp)
  target_include_directories(kauai-sdl-video PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-sdl-video PUBLIC kauai-core PRIVATE SDL3::SDL3-static)
  add_executable(video-sdl-test EXCLUDE_FROM_ALL tests/video_sdl_test.cpp)
  target_link_libraries(video-sdl-test PRIVATE kauai-sdl-video)
endif()
if(STUDIO_SDL3 OR BUILD_SDL_MIDI)
  if(NOT KAUAI_MIDI_SYNTH)
    message(FATAL_ERROR "SDL MIDI requires KAUAI_MIDI_SYNTH=ON")
  endif()
  include(SDL3Dependency)
  add_library(kauai-sdl-midi STATIC kauai/src/midi_sdl.cpp)
  target_include_directories(kauai-sdl-midi PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-sdl-midi PUBLIC kauai-core PRIVATE kauai-midi-synth SDL3::SDL3-static)
  add_executable(midi-sdl-test EXCLUDE_FROM_ALL tests/midi_sdl_test.cpp)
  target_link_libraries(midi-sdl-test PRIVATE kauai-sdl-midi)
  kauai_copy_midi_assets(midi-sdl-test)
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/assets")
  get_filename_component(bank_source "${KAUAI_MIDI_BANK_FILE}" ABSOLUTE)
  if(NOT bank_source STREQUAL "${PROJECT_BINARY_DIR}/assets/GeneralUserGS.sf2")
    configure_file("${KAUAI_MIDI_BANK_FILE}" "${PROJECT_BINARY_DIR}/assets/GeneralUserGS.sf2" COPYONLY)
  endif()
endif()
if(KAUAI_FREETYPE OR BUILD_MARCUS)
  include(FreeTypeDependency)
  add_library(kauai-freetype STATIC kauai/src/font_freetype.cpp kauai/src/font_catalog.cpp)
  target_include_directories(kauai-freetype PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-freetype PUBLIC kauai-core PRIVATE freetype)
  install(FILES docs/licenses/freetype-FTL.txt docs/licenses/freetype-LICENSE.txt
          DESTINATION licenses)
endif()
add_library(kauai-font-host STATIC kauai/src/font_host.cpp)
target_include_directories(kauai-font-host PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
target_link_libraries(kauai-font-host PUBLIC kauai-core)
if(KAUAI_FREETYPE OR BUILD_MARCUS)
  target_compile_definitions(kauai-font-host PRIVATE KAUAI_FREETYPE)
  target_link_libraries(kauai-font-host PUBLIC kauai-freetype)
  if(WIN32)
    target_sources(kauai-font-host PRIVATE kauai/src/font_locations_win.cpp)
  else()
    target_sources(kauai-font-host PRIVATE kauai/src/font_locations_posix.cpp)
  endif()
endif()
if(STUDIO_SDL3 OR BUILD_SDL_DIALOGS)
  include(ImGuiDependency)
  add_library(kauai-sdl-dialog STATIC kauai/src/sdl_dialog.cpp kauai/src/sdl_file_dialog.cpp)
  target_compile_features(kauai-sdl-dialog PRIVATE cxx_std_17)
  target_include_directories(kauai-sdl-dialog PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-sdl-dialog PUBLIC kauai-core PRIVATE imgui-sdl3 kauai-font-host)
  add_executable(dialog-sdl-test EXCLUDE_FROM_ALL tests/dialog_sdl_test.cpp)
  target_link_libraries(dialog-sdl-test PRIVATE kauai-sdl-dialog SDL3::SDL3-static)
  add_executable(file-dialog-sdl-test EXCLUDE_FROM_ALL tests/file_dialog_sdl_test.cpp)
  target_link_libraries(file-dialog-sdl-test PRIVATE kauai-sdl-dialog SDL3::SDL3-static)
  if(NOT WIN32 OR STUDIO_PORTABLE_RUNTIME)
    add_executable(filename-dialog-sdl-test EXCLUDE_FROM_ALL tests/filename_dialog_sdl_test.cpp kauai/src/fnidialogsdl.cpp)
    target_compile_definitions(filename-dialog-sdl-test PRIVATE KAUAI_SDL3)
    target_link_libraries(filename-dialog-sdl-test PRIVATE kauai-core kauai-sdl-host kauai-sdl-dialog)
    add_test(NAME filename-dialog-sdl COMMAND filename-dialog-sdl-test)
    set_tests_properties(filename-dialog-sdl PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software"
      SKIP_RETURN_CODE 77)
  endif()
endif()
if(STUDIO_SDL3 OR BUILD_MARCUS)
  include(SDL3Dependency)
  include(CompressedAudioDependency)
  add_library(kauai-sdl-audio STATIC kauai/src/sdl_audio.cpp kauai/src/sdl_recording.cpp)
  target_include_directories(kauai-sdl-audio PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src")
  target_link_libraries(kauai-sdl-audio PUBLIC kauai-audio-mixer kauai-wave-codec kauai-recording-data
    PRIVATE SDL3::SDL3-static kauai-compressed-wave)
endif()
if(STUDIO_SDL3)
  add_executable(recording-sdl-test EXCLUDE_FROM_ALL tests/recording_sdl_test.cpp)
  target_link_libraries(recording-sdl-test PRIVATE kauai-sdl-audio)
  add_executable(movie-recording-test EXCLUDE_FROM_ALL tests/movie_recording_test.cpp)
  target_link_libraries(movie-recording-test PRIVATE engine)
  add_executable(audio-sdl-test EXCLUDE_FROM_ALL tests/audio_sdl_test.cpp)
  target_link_libraries(audio-sdl-test PRIVATE kauai-sdl-audio)
  add_executable(compressed-wave-test EXCLUDE_FROM_ALL tests/compressed_wave_test.cpp)
  target_link_libraries(compressed-wave-test PRIVATE kauai-compressed-wave kauai-sdl-audio)
  add_executable(wave-import-sdl-test EXCLUDE_FROM_ALL tests/wave_import_sdl_test.cpp)
  target_link_libraries(wave-import-sdl-test PRIVATE kauai-sdl-audio SDL3::SDL3-static $<$<PLATFORM_ID:Windows>:msacm32>)
  add_executable(movie-sound-import-test EXCLUDE_FROM_ALL tests/movie_sound_import_test.cpp)
  target_link_libraries(movie-sound-import-test PRIVATE engine)
  add_executable(sound-sdl-test EXCLUDE_FROM_ALL tests/sound_sdl_test.cpp)
  target_link_libraries(sound-sdl-test PRIVATE kauai)
  add_executable(clipboard-sdl-test EXCLUDE_FROM_ALL tests/clipboard_sdl_test.cpp tests/sdl_clipboard_event.cpp)
  target_link_libraries(clipboard-sdl-test PRIVATE kauai SDL3::SDL3-static)
  add_executable(cursor-sdl-test EXCLUDE_FROM_ALL tests/cursor_sdl_test.cpp)
  target_link_libraries(cursor-sdl-test PRIVATE kauai-sdl-host user32 gdi32)
  add_executable(scroll-sdl-test EXCLUDE_FROM_ALL tests/scroll_sdl_test.cpp)
  target_link_libraries(scroll-sdl-test PRIVATE kauai)
  add_executable(window-geometry-sdl-test EXCLUDE_FROM_ALL tests/window_geometry_sdl_test.cpp)
  target_link_libraries(window-geometry-sdl-test PRIVATE kauai)
  add_executable(focus-sdl-test EXCLUDE_FROM_ALL tests/focus_sdl_test.cpp tests/sdl_focus_event.cpp)
  target_link_libraries(focus-sdl-test PRIVATE kauai SDL3::SDL3-static)
  add_executable(input-sdl-test EXCLUDE_FROM_ALL tests/input_sdl_test.cpp)
  target_link_libraries(input-sdl-test PRIVATE kauai-sdl-host SDL3::SDL3-static)
  add_executable(events-sdl-test EXCLUDE_FROM_ALL tests/events_sdl_test.cpp tests/sdl_focus_event.cpp)
  target_include_directories(events-sdl-test PRIVATE "${PROJECT_SOURCE_DIR}/inc")
  target_link_libraries(events-sdl-test PRIVATE kauai SDL3::SDL3-static)
  add_executable(window-lifecycle-sdl-test EXCLUDE_FROM_ALL tests/window_lifecycle_sdl_test.cpp)
  target_link_libraries(window-lifecycle-sdl-test PRIVATE kauai SDL3::SDL3-static)
endif()
include(CTest)
add_subdirectory(libraries/engine)

cmake_dependent_option(3DMM_PACKAGE_WIX "Generate an MSI via WiX" ON "BUILD_PACKAGES AND DEFINED ENV{WIX}" OFF)
cmake_dependent_option(3DMM_PACKAGE_ZIP "Generate a portable ZIP" ON "BUILD_PACKAGES" OFF)

# Optional Tools
find_package(ClangTidy)
find_package(CCache)

if (TARGET CCache::CCache AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  get_property(CMAKE_CXX_COMPILER_LAUNCHER TARGET CCache::CCache PROPERTY IMPORTED_LOCATION)
  get_property(CMAKE_C_COMPILER_LAUNCHER TARGET CCache::CCache PROPERTY IMPORTED_LOCATION)
endif()

# TODO: Enable this when the 
#if (TARGET Clang::Tidy AND NOT CMAKE_CXX_CLANG_TIDY)
#  get_property(CMAKE_CXX_CLANG_TIDY TARGET Clang::Tidy PROPERTY IMPORTED_LOCATION)
#endif()

# IN_80386 gates inline x86 __asm fast paths in kauai (codkauai, gfx,
# mbmpgui, utilcopy, utilint, utilrnd). Each block has a portable C #else
# branch, and MSVC x64 cannot compile __asm at all -- so on x64 we drop
# the define and the C fallback takes over. Keep the define on x86 builds
# only.
set(SOC_X86 FALSE)
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(SOC_X86 TRUE)
endif()

# Local Tooling


# kcdc-386 / kcd2-386 are codegen tools that emit kcdc_386.h / kcd2_386.h --
# inline-__asm tables consumed by codkauai.cpp inside #ifdef IN_80386. They
# cannot currently cross-compile (host-as-target only) and the generated
# headers are only needed when IN_80386 is defined, so build them on x86 only.
if (SOC_X86)
  add_executable(kcdc-386 EXCLUDE_FROM_ALL)
  target_sources(kcdc-386 PRIVATE "${PROJECT_SOURCE_DIR}/kauai/src/kcdc_386.c")
  target_include_directories(kcdc-386 PRIVATE $<TARGET_PROPERTY:kauai,INCLUDE_DIRECTORIES>)

  add_executable(kcd2-386 EXCLUDE_FROM_ALL)
  target_sources(kcd2-386 PRIVATE "${PROJECT_SOURCE_DIR}/kauai/src/kcd2_386.c")
  target_include_directories(kcd2-386 PRIVATE $<TARGET_PROPERTY:kauai,INCLUDE_DIRECTORIES>)
endif()

if (NOT TARGET 3DMMForever::AudioMan)
  if(WIN32)
    add_library(audioman)
  else()
    # This legacy Windows compatibility stub is unused by the SDL runtime.
    add_library(audioman EXCLUDE_FROM_ALL)
  endif()
  add_library(3DMMForever::AudioMan ALIAS audioman)
  target_include_directories(audioman PUBLIC
      "${PROJECT_SOURCE_DIR}/kauai/src"
  )
  target_sources(audioman PRIVATE
      "${PROJECT_SOURCE_DIR}/audioman/audioman.cpp"
  )
endif()

add_executable(chelp WIN32 EXCLUDE_FROM_ALL)
target_sources(chelp PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/chtop.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chelp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chelpexp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chelp.rc"
)
target_include_directories(chelp PRIVATE $<TARGET_PROPERTY:kauai,INCLUDE_DIRECTORIES>)
target_link_libraries(chelp PRIVATE $<TARGET_NAME_IF_EXISTS:kauai>)

# Kauai test applications
add_executable(ft WIN32 EXCLUDE_FROM_ALL)
target_sources(ft PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/src/test.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/ft.rc"
    "${PROJECT_SOURCE_DIR}/kauai/src/ft.cpp"
)
target_link_libraries(ft PRIVATE kauai)

add_executable(ut EXCLUDE_FROM_ALL)
target_sources(ut PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/src/ut.cpp"
)
target_link_libraries(ut PRIVATE kauai)

# Geometry math tests for kauai's PT, RC, and Region. Pure unit tests
# (no I/O, no GUI). Regression net for Project 1's sized-types audit:
# these are the types most likely to be silently miscompiled under LP64.
add_executable(geometry-test EXCLUDE_FROM_ALL)
target_sources(geometry-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/geometry_test.cpp"
)
# Pure unit test (PT/RC/Region arithmetic): no I/O, no GUI.
# Links kauai-core only -- runs on x64 today.
target_link_libraries(geometry-test PRIVATE kauai-core)

add_executable(global-cleanup-test EXCLUDE_FROM_ALL tests/global_cleanup_test.cpp)
target_link_libraries(global-cleanup-test PRIVATE kauai-core)

# KauaiCodec round-trip tests. On x86 the decoder uses the inline-asm fast
# path generated from kauai/src/kcd2_386.c; on x64 it falls back to the
# portable C path inside _FDecode/_FDecode2. The same encoder (always C)
# feeds both, so this test catches any drift between asm and C decoders
# without requiring 3dmm to launch and grind through chunky-file decoding.
add_executable(codec-test EXCLUDE_FROM_ALL)
target_sources(codec-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/codec_test.cpp"
)
# Pure unit test (KauaiCodec encode+decode round-trip). Links
# kauai-core only -- runs on x64 today.
target_link_libraries(codec-test PRIVATE kauai-core)

# Actor render test that calls the REAL engine loaders (Template::
# FReadTmpl, PbodyCreate, FSetActnCel) instead of re-implementing
# the binding (CelPartSpec, GLBS, GGCM, CMTL graph) by hand. Links
# engine-core only -- not the full engine playback runtime -- so it
# can run on x64 once the actor render path is wired in.
add_executable(actor-render-test EXCLUDE_FROM_ALL)
target_sources(actor-render-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/actor_render_test.cpp"
)
target_link_libraries(actor-render-test PRIVATE engine-core)

# Movie save/load round-trip regression test. Creates a blank movie,
# saves it to a real on-disk .3MM, releases, reopens, releases again.
# Exists to reproduce / pin a memory access error in the save+load
# path. Links the full engine + kauai (gui side) because Movie save
# uses vpappb (BeginLongOp/EndLongOp) and vptagm (PgstSource).
add_executable(movie-save-load-test EXCLUDE_FROM_ALL)
target_sources(movie-save-load-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/movie_save_load_test.cpp"
)
target_link_libraries(movie-save-load-test PRIVATE engine)
target_link_options(movie-save-load-test
  BEFORE PRIVATE
    $<$<AND:$<LINK_LANG_AND_ID:CXX,MSVC>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:libcmt.lib>)

# Direct-drive harness for the BRender ZB scan-converters. Calls the
# asm-only triangle/trapezoid renderers via the global zb state, dumps
# the colour buffer to a PGM file. On x86 the asm path runs; on x64 the
# C-fallback path runs. The bitmaps are diff-compared during the
# 2026-05-01-brender-x64-enablement port. EXCLUDE_FROM_ALL: explicit
# `cmake --build build --target bren-rasterizer-test`.
add_executable(bren-rasterizer-test EXCLUDE_FROM_ALL)
target_sources(bren-rasterizer-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/bren_rasterizer_test.cpp"
)
target_include_directories(bren-rasterizer-test PRIVATE
    "${PROJECT_SOURCE_DIR}/bren/lib/zb"
    "${PROJECT_SOURCE_DIR}/bren/lib/fw"
)
target_link_libraries(bren-rasterizer-test PRIVATE BRender::Libraries)
target_compile_definitions(bren-rasterizer-test PRIVATE BASED_FIXED=1)

# Link the core diagnostic adapter without any GUI or application stubs.
# Run with no arguments for a warning, or "fatal" to verify exit code 10.
add_executable(brender-diagnostic-test EXCLUDE_FROM_ALL tests/brender_diagnostic_test.c)
target_link_libraries(brender-diagnostic-test PRIVATE brender-core)

if (MSVC)
  # Match brender_zb's /Zp4 packing so struct layouts agree on both x86
  # and x64. Without this the test sees padding that the lib doesn't.
  target_compile_options(bren-rasterizer-test PRIVATE /Zp4 /wd4244)
endif()

add_executable(chomp EXCLUDE_FROM_ALL)
if(NOT WIN32)
  # The generated-source directory is also named chomp; POSIX executables
  # have no .exe suffix to keep the two names distinct.
  set_target_properties(chomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tools")
endif()
target_sources(chomp PRIVATE "${PROJECT_SOURCE_DIR}/kauai/tools/chomp.cpp")
target_include_directories(chomp PRIVATE $<TARGET_PROPERTY:kauai,INCLUDE_DIRECTORIES>)
target_link_libraries(chomp PRIVATE $<TARGET_NAME_IF_EXISTS:kauai>)


add_executable(movie-chomp EXCLUDE_FROM_ALL)
target_sources(movie-chomp PRIVATE "${PROJECT_SOURCE_DIR}/src/tools/movie_chomp.cpp")

target_include_directories(movie-chomp
  PUBLIC $<TARGET_PROPERTY:engine,INCLUDE_DIRECTORIES>
  "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(movie-chomp PRIVATE engine)

target_link_options(movie-chomp
  BEFORE PRIVATE
    $<$<AND:$<LINK_LANG_AND_ID:CXX,MSVC>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:libcmt.lib>)

# Pulls a single BMDL chunk's unpacked bytes out of a 3DMM chunky file,
# so the bren-rasterizer-test prop scene can render real game models
# from disk without a runtime engine init. EXCLUDE_FROM_ALL.
add_executable(extract-bmdl EXCLUDE_FROM_ALL)
target_sources(extract-bmdl PRIVATE "${PROJECT_SOURCE_DIR}/src/tools/extract_bmdl.cpp")
target_include_directories(extract-bmdl PRIVATE "${PROJECT_SOURCE_DIR}/inc")
# Links kauai-core (chunky-file runtime) only -- no UI / WinMain. The
# tool only needs ChunkyFile / DataBlock / Filename and FAlloc/Free
# helpers, all of which live in kauai-core.
target_link_libraries(extract-bmdl PRIVATE kauai-core)

# inspect-chunks: generic reflection / debug walker for 3DMM chunky
# files. Lists chunks, walks the parent->child graph, dumps unpacked
# bytes by (ctg, cno). Used to map TMPL graphs before writing actor
# loaders. Same kauai-core link as extract-bmdl.
add_executable(inspect-chunks EXCLUDE_FROM_ALL)
target_sources(inspect-chunks PRIVATE "${PROJECT_SOURCE_DIR}/src/tools/inspect_chunks.cpp")
target_include_directories(inspect-chunks PRIVATE "${PROJECT_SOURCE_DIR}/inc")
target_link_libraries(inspect-chunks PRIVATE kauai-core)

add_library(three-mm-expansion STATIC EXCLUDE_FROM_ALL libraries/expansion/src/nick_expansion.cpp)
target_include_directories(three-mm-expansion PUBLIC "${PROJECT_SOURCE_DIR}/libraries/expansion/include")
target_link_libraries(three-mm-expansion PRIVATE engine-core)
target_compile_features(three-mm-expansion PUBLIC cxx_std_17)
add_executable(nick-expansion EXCLUDE_FROM_ALL src/tools/nick_expansion.cpp)
target_link_libraries(nick-expansion PRIVATE three-mm-expansion kauai-core)
add_executable(nick-expansion-test EXCLUDE_FROM_ALL tests/nick_expansion_test.cpp)
target_link_libraries(nick-expansion-test PRIVATE three-mm-expansion engine-core)

include(PackDependencies)
add_library(three-mm-vfs STATIC libraries/content/src/content_vfs.c)
set_target_properties(three-mm-vfs PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(three-mm-vfs PUBLIC "${PROJECT_SOURCE_DIR}/libraries/content/include")
add_executable(content-vfs-test EXCLUDE_FROM_ALL tests/content_vfs_test.c)
set_target_properties(content-vfs-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(content-vfs-test PRIVATE three-mm-vfs)
add_library(three-mm-content STATIC libraries/content/src/content_vfs.cpp)
target_link_libraries(three-mm-content PUBLIC three-mm-vfs)
target_compile_features(three-mm-content PUBLIC cxx_std_17)
target_include_directories(three-mm-content PUBLIC "${PROJECT_SOURCE_DIR}/libraries/content/include")
add_library(three-mm-v3dmm STATIC libraries/v3dmm/src/v3dmm.cpp libraries/v3dmm/src/publish.cpp)
target_compile_features(three-mm-v3dmm PUBLIC cxx_std_17)
target_include_directories(three-mm-v3dmm PUBLIC "${PROJECT_SOURCE_DIR}/libraries/v3dmm/include")
target_link_libraries(three-mm-v3dmm PRIVATE miniz)
add_library(studio-pack-store STATIC src/studio/pack_store.cpp src/studio/pack_import.cpp src/studio/pack_hash.cpp)
include(SQLiteDependency)
target_link_libraries(studio-pack-store PRIVATE package-sqlite)
target_sources(studio-pack-store PRIVATE src/studio/vxp_store.cpp)
target_link_libraries(studio-pack-store PUBLIC three-mm-v3dmm)
target_link_libraries(studio-pack-store PUBLIC three-mm-content)
target_compile_features(studio-pack-store PUBLIC cxx_std_17)
target_include_directories(studio-pack-store PUBLIC "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/src/studio")
target_link_libraries(studio-pack-store PRIVATE ThreeMM::Archive three-mm-expansion studio-iso-cache miniz nlohmann_json::nlohmann_json)
add_executable(studio-packs-test EXCLUDE_FROM_ALL tests/studio_packs_test.cpp)
target_link_libraries(studio-packs-test PRIVATE studio-pack-store three-mm-expansion engine-core studio-iso-cache)
add_executable(v3dmm-test EXCLUDE_FROM_ALL tests/v3dmm_test.cpp)
target_link_libraries(v3dmm-test PRIVATE studio-pack-store engine-core miniz)

# Read-only normalized archive dump, with bounded core record decoding.
add_executable(three-mm EXCLUDE_FROM_ALL src/tools/three_mm.cpp)
set_target_properties(three-mm PROPERTIES OUTPUT_NAME 3mm)
target_link_libraries(three-mm PRIVATE ThreeMM::Archive)
if(MINGW)
  target_link_options(three-mm PRIVATE -municode)
endif()
add_executable(three-mm-fixture EXCLUDE_FROM_ALL tests/three_mm_fixture.cpp)
target_link_libraries(three-mm-fixture PRIVATE engine-core)
add_executable(platform-services-test EXCLUDE_FROM_ALL tests/platform_services_test.cpp)
target_link_libraries(platform-services-test PRIVATE kauai-core)
add_executable(file-services-test EXCLUDE_FROM_ALL tests/file_services_test.cpp)
target_link_libraries(file-services-test PRIVATE kauai-core)
add_executable(user-services-test EXCLUDE_FROM_ALL tests/user_services_test.cpp)
target_link_libraries(user-services-test PRIVATE kauai-core)
add_executable(settings-store-test EXCLUDE_FROM_ALL tests/settings_store_test.cpp)
target_link_libraries(settings-store-test PRIVATE kauai-core)
if(STUDIO_SDL3 OR BUILD_MARCUS)
  add_library(studio-settings STATIC src/studio/settings_ini.cpp)
  target_compile_features(studio-settings PUBLIC cxx_std_17)
  target_include_directories(studio-settings PUBLIC "${PROJECT_SOURCE_DIR}/inc")
  target_link_libraries(studio-settings PUBLIC kauai-core SDL3::SDL3-static)
  add_executable(settings-ini-test EXCLUDE_FROM_ALL tests/settings_ini_test.cpp)
  target_link_libraries(settings-ini-test PRIVATE studio-settings)
endif()
if(WIN32)
  add_executable(settings-store-win-test EXCLUDE_FROM_ALL tests/settings_store_win_test.cpp)
  target_link_libraries(settings-store-win-test PRIVATE kauai-core advapi32)
else()
  add_executable(settings-store-posix-test EXCLUDE_FROM_ALL tests/settings_store_posix_test.cpp)
  target_link_libraries(settings-store-posix-test PRIVATE kauai-core)
endif()
add_executable(filename-test EXCLUDE_FROM_ALL tests/filename_test.cpp)
add_executable(file-browser-test EXCLUDE_FROM_ALL tests/file_browser_test.cpp)
add_executable(dib-frame-test EXCLUDE_FROM_ALL tests/dib_frame_test.cpp)
target_link_libraries(dib-frame-test PRIVATE kauai-core)
add_executable(avi-movie-test EXCLUDE_FROM_ALL tests/avi_movie_test.cpp)
target_link_libraries(avi-movie-test PRIVATE kauai-core)
if(WIN32)
  add_executable(avi-corpus-win-test EXCLUDE_FROM_ALL tests/avi_corpus_win_test.cpp)
  target_compile_features(avi-corpus-win-test PRIVATE cxx_std_17)
  target_link_libraries(avi-corpus-win-test PRIVATE kauai-core vfw32)
endif()
target_link_libraries(file-browser-test PRIVATE kauai-core)
target_link_libraries(filename-test PRIVATE kauai-core)
add_executable(text-services-test EXCLUDE_FROM_ALL tests/text_services_test.cpp)
target_link_libraries(text-services-test PRIVATE kauai-core)
add_executable(text-portable-test EXCLUDE_FROM_ALL tests/text_services_test.cpp kauai/src/platform_text.cpp kauai/src/text_utf8.cpp)
target_include_directories(text-portable-test PRIVATE "${PROJECT_SOURCE_DIR}/kauai/src")
target_compile_definitions(text-portable-test PRIVATE KAUAI_PORTABLE_TEXT)
target_link_libraries(text-portable-test PRIVATE Threads::Threads)
add_executable(string-compat-test EXCLUDE_FROM_ALL tests/string_compat_test.cpp)
target_link_libraries(string-compat-test PRIVATE kauai-core)
add_executable(picture-data-test EXCLUDE_FROM_ALL tests/picture_data_test.cpp)
target_link_libraries(picture-data-test PRIVATE kauai-core)
add_executable(dialog-resource-test EXCLUDE_FROM_ALL tests/dialog_resource_test.cpp)
target_link_libraries(dialog-resource-test PRIVATE kauai-core)
add_library(dialog-api-check OBJECT EXCLUDE_FROM_ALL tests/dialog_api_include_order.cpp kauai/src/dlg.cpp)
target_link_libraries(dialog-api-check PRIVATE kauai-core)
add_library(dialog-controller-api-check OBJECT EXCLUDE_FROM_ALL kauai/src/dlgsdl.cpp)
target_link_libraries(dialog-controller-api-check PRIVATE kauai-core)
add_executable(dialog-model-test EXCLUDE_FROM_ALL tests/dialog_model_test.cpp $<TARGET_OBJECTS:dialog-api-check>)
target_link_libraries(dialog-model-test PRIVATE kauai-core)
add_executable(pixel-canvas-test EXCLUDE_FROM_ALL tests/pixel_canvas_test.cpp)
target_link_libraries(pixel-canvas-test PRIVATE kauai-core)
add_executable(resource-cache-test EXCLUDE_FROM_ALL tests/resource_cache_test.cpp)
target_link_libraries(resource-cache-test PRIVATE kauai-core)
add_executable(font-services-test EXCLUDE_FROM_ALL tests/font_services_test.cpp)
target_link_libraries(font-services-test PRIVATE kauai-core)
if(KAUAI_FREETYPE)
  add_executable(font-freetype-test EXCLUDE_FROM_ALL tests/font_freetype_test.cpp)
  target_link_libraries(font-freetype-test PRIVATE kauai-freetype)
  add_executable(font-host-test EXCLUDE_FROM_ALL tests/font_host_test.cpp)
  target_link_libraries(font-host-test PRIVATE kauai-font-host)
  set(KAUAI_FONT_TEST_TTF "" CACHE FILEPATH "Local TrueType fixture for portable font tests")
  set(KAUAI_FONT_TEST_FON "" CACHE FILEPATH "Local WinFNT fixture for portable font tests")
  set(KAUAI_FONT_TEST_COLLECTION "" CACHE FILEPATH "Optional local font collection fixture for portable font tests")
  if(WIN32 AND NOT KAUAI_FONT_TEST_TTF AND EXISTS "$ENV{WINDIR}/Fonts/arial.ttf")
    set(KAUAI_FONT_TEST_TTF "$ENV{WINDIR}/Fonts/arial.ttf" CACHE FILEPATH "Local TrueType fixture" FORCE)
  endif()
  if(WIN32 AND NOT KAUAI_FONT_TEST_FON AND EXISTS "$ENV{WINDIR}/Fonts/vgasys.fon")
    set(KAUAI_FONT_TEST_FON "$ENV{WINDIR}/Fonts/vgasys.fon" CACHE FILEPATH "Local WinFNT fixture" FORCE)
  endif()
  if(WIN32 AND NOT KAUAI_FONT_TEST_COLLECTION AND EXISTS "$ENV{WINDIR}/Fonts/cambria.ttc")
    set(KAUAI_FONT_TEST_COLLECTION "$ENV{WINDIR}/Fonts/cambria.ttc" CACHE FILEPATH "Local font collection fixture" FORCE)
  endif()
endif()
add_executable(legacy-math-test EXCLUDE_FROM_ALL tests/legacy_math_test.cpp)
add_executable(cursor-data-test EXCLUDE_FROM_ALL tests/cursor_data_test.cpp)
target_link_libraries(cursor-data-test PRIVATE kauai-core)
add_executable(midi-mixer-test EXCLUDE_FROM_ALL tests/midi_mixer_test.cpp)
target_link_libraries(midi-mixer-test PRIVATE kauai-core)
add_executable(midi-worker-test EXCLUDE_FROM_ALL tests/midi_worker_test.cpp)
target_link_libraries(midi-worker-test PRIVATE kauai-core)
add_executable(palette-state-test EXCLUDE_FROM_ALL tests/palette_state_test.cpp)
target_link_libraries(palette-state-test PRIVATE kauai-core)
add_executable(pixel-buffer-test EXCLUDE_FROM_ALL tests/pixel_buffer_test.cpp)
target_link_libraries(pixel-buffer-test PRIVATE kauai-core)
add_library(portfolio-api-check OBJECT tests/portfolio_api_include_order.cpp)
add_library(midi-stream-api-check OBJECT tests/midi_stream_api_include_order.cpp)
target_link_libraries(midi-stream-api-check PRIVATE kauai-core)
add_library(video-api-check OBJECT tests/video_api_include_order.cpp)
add_executable(graphics-api-test EXCLUDE_FROM_ALL tests/graphics_api_test.cpp)
add_library(gob-api-check OBJECT EXCLUDE_FROM_ALL tests/gob_api_include_order.cpp)
target_link_libraries(gob-api-check PRIVATE kauai-core)
add_library(app-api-check OBJECT EXCLUDE_FROM_ALL tests/app_api_include_order.cpp)
target_link_libraries(app-api-check PRIVATE kauai-core)
add_library(app-sdl-api-check OBJECT EXCLUDE_FROM_ALL tests/app_api_include_order.cpp)
target_compile_definitions(app-sdl-api-check PRIVATE KAUAI_SDL3)
target_link_libraries(app-sdl-api-check PRIVATE kauai-core)
# Compile the real SDL event/paint implementation and shared GUI umbrella on
# every platform, independently of the remaining native application/rasterizer.
add_library(sdl-runtime-compile-check OBJECT EXCLUDE_FROM_ALL
  tests/runtime_api_include_order.cpp kauai/src/appb.cpp kauai/src/appbsdl.cpp
  kauai/src/clip.cpp kauai/src/cmd_gui.cpp kauai/src/ctl.cpp kauai/src/docb.cpp
  kauai/src/kidhelp.cpp kauai/src/kidspace.cpp kauai/src/kidworld.cpp
  kauai/src/rtxt.cpp kauai/src/rtxt2.cpp kauai/src/screxeg.cpp
  kauai/src/text.cpp kauai/src/textdoc.cpp
  kauai/src/sdl_studio.cpp kauai/src/pic.cpp kauai/src/cursor.cpp
  kauai/src/gob.cpp kauai/src/gob_window.cpp kauai/src/gfx.cpp kauai/src/gfx_port.cpp kauai/src/gfx_values.cpp
  kauai/src/fontlist.cpp kauai/src/gfx_text.cpp kauai/src/gfx_pixels.cpp kauai/src/fontlist_portable.cpp
  $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/kauai/src/gfx_posix.cpp>
  $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/kauai/src/gob_sdl.cpp>)
target_compile_definitions(sdl-runtime-compile-check PRIVATE KAUAI_SDL3)
target_link_libraries(sdl-runtime-compile-check PRIVATE kauai-core)
if(TARGET kauai-sdl-host)
  # Executes the actual CPU/POSIX port, also on Windows without a native DC.
  add_executable(pixel-port-test EXCLUDE_FROM_ALL tests/pixel_port_test.cpp
    kauai/src/pic.cpp kauai/src/pic_portable.cpp
    kauai/src/gfx_port.cpp kauai/src/gfx_values.cpp kauai/src/fontlist.cpp kauai/src/gfx_text.cpp
    kauai/src/gfx_pixels.cpp kauai/src/gfx_posix.cpp kauai/src/fontlist_portable.cpp)
  target_compile_definitions(pixel-port-test PRIVATE KAUAI_SDL3)
  if(KAUAI_FREETYPE)
    target_compile_definitions(pixel-port-test PRIVATE KAUAI_TEST_FREETYPE)
  endif()
  target_link_libraries(pixel-port-test PRIVATE kauai-core kauai-sdl-host kauai-font-host)
endif()


target_link_libraries(graphics-api-test PRIVATE kauai-core)
target_link_libraries(video-api-check PRIVATE kauai-core)
target_include_directories(portfolio-api-check PRIVATE "${PROJECT_SOURCE_DIR}/inc")
target_link_libraries(portfolio-api-check PRIVATE kauai-core)
target_link_libraries(legacy-math-test PRIVATE kauai-core)
add_executable(debug-checks-test EXCLUDE_FROM_ALL tests/debug_checks_test.cpp)
target_link_libraries(debug-checks-test PRIVATE kauai-core)
if(STUDIO_NATIVE_WINDOWS)
  add_executable(midi-stream-host-test EXCLUDE_FROM_ALL tests/midi_stream_host_test.cpp)
  target_link_libraries(midi-stream-host-test PRIVATE kauai)
  add_executable(palette-gdi-test EXCLUDE_FROM_ALL tests/palette_gdi_test.cpp)
  target_link_libraries(palette-gdi-test PRIVATE kauai)
  add_executable(gdi-adapter-test EXCLUDE_FROM_ALL tests/gdi_adapter_test.cpp)
  add_executable(dib-frame-gdi-test EXCLUDE_FROM_ALL tests/dib_frame_gdi_test.cpp)
  target_link_libraries(dib-frame-gdi-test PRIVATE kauai)
  add_executable(video-stream-host-test EXCLUDE_FROM_ALL tests/video_stream_host_test.cpp)
  target_link_libraries(video-stream-host-test PRIVATE kauai)
  add_executable(dialog-resource-win-test EXCLUDE_FROM_ALL tests/dialog_resource_win_test.cpp tests/dialog_resources.rc)
  target_link_libraries(dialog-resource-win-test PRIVATE kauai)
  target_link_libraries(gdi-adapter-test PRIVATE kauai)
  add_executable(packed-shape-gdi-test EXCLUDE_FROM_ALL tests/packed_shape_gdi_test.cpp)
  target_link_libraries(packed-shape-gdi-test PRIVATE kauai)
  add_executable(pixel-canvas-gdi-test EXCLUDE_FROM_ALL tests/pixel_canvas_gdi_test.cpp)
  target_link_libraries(pixel-canvas-gdi-test PRIVATE kauai)
  add_executable(pixel-canvas-indexed-gdi-test EXCLUDE_FROM_ALL tests/pixel_canvas_indexed_gdi_test.cpp)
  target_link_libraries(pixel-canvas-indexed-gdi-test PRIVATE kauai)
  add_executable(masked-transfer-gdi-test EXCLUDE_FROM_ALL tests/masked_transfer_gdi_test.cpp)
  target_link_libraries(masked-transfer-gdi-test PRIVATE kauai)
  add_executable(indexed-copy-gdi-test EXCLUDE_FROM_ALL tests/indexed_copy_gdi_test.cpp)
  target_link_libraries(indexed-copy-gdi-test PRIVATE kauai)
  add_executable(packed-copy-gdi-test EXCLUDE_FROM_ALL tests/packed_copy_gdi_test.cpp)
  target_link_libraries(packed-copy-gdi-test PRIVATE kauai)
  add_executable(pixel-storage-gdi-test EXCLUDE_FROM_ALL tests/pixel_storage_gdi_test.cpp)
  target_link_libraries(pixel-storage-gdi-test PRIVATE kauai)
  add_executable(font-gdi-test EXCLUDE_FROM_ALL tests/font_gdi_test.cpp)
  target_link_libraries(font-gdi-test PRIVATE kauai)
endif()


# Chunk Editor
add_executable(ched WIN32 EXCLUDE_FROM_ALL)
target_sources(ched PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/ched.rc"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chdoc.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/ched.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chgrp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chhex.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chmbmp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chpic.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chtxt.cpp"
)
target_link_libraries(ched PRIVATE kauai)

# mkmbmp
add_executable(mkmbmp EXCLUDE_FROM_ALL)
target_sources(mkmbmp PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/mkmbmp.cpp"
)
target_link_libraries(mkmbmp PRIVATE kauai)

# kpack
add_executable(kpack EXCLUDE_FROM_ALL)
target_sources(kpack PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/kpack.cpp"
)
target_link_libraries(kpack PRIVATE kauai)

# chmerge
add_executable(chmerge EXCLUDE_FROM_ALL)
target_sources(chmerge PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/chmerge.cpp"
)
target_link_libraries(chmerge PRIVATE kauai)

# chelpdmp
add_executable(chelpdmp EXCLUDE_FROM_ALL)
target_sources(chelpdmp PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/tools/chelpdmp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/tools/chelpexp.cpp"
)
target_link_libraries(chelpdmp PRIVATE kauai)

# These are only here to ensure configure is rerun when dependencies are generated.
# This is 
file(GLOB building-chunk-sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/building/*.cht")
file(GLOB studio-chunk-sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/studio/*.cht")

if (SOC_X86)
  add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/generated/kauai/src/kcdc_386.h"
    COMMAND cmake -E make_directory "${PROJECT_BINARY_DIR}/generated/kauai/src"
    COMMAND kcdc-386 "${PROJECT_BINARY_DIR}/generated/kauai/src/kcdc_386.h"
    COMMENT "Generating kcdc_386.h"
    VERBATIM
  )

  add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/generated/kauai/src/kcd2_386.h"
    COMMAND cmake -E make_directory "${PROJECT_BINARY_DIR}/generated/kauai/src"
    COMMAND kcd2-386 "${PROJECT_BINARY_DIR}/generated/kauai/src/kcd2_386.h"
    COMMENT "Generating kcd2_386.h"
    VERBATIM
  )
endif()

add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd4430>)
# --- kauai-core: chunky-file/file/utility runtime, no UI dependencies ---
#
# The split exists so CLI tools (extract-bmdl, inspect-chunks, future
# extract-tmpl etc.) and headless test harnesses can link against just
# the chunk reader/writer + container + util layer without dragging in
# WinMain bootstrap, Win32 windowing, MIDI, sound, kidspace, etc.
#
# Boundary rule: a file goes in kauai-core iff its .cpp does not
# reference any UI class (GraphicsObject, GraphicsPort, Application,
# Cursor, Clipboard, MenuBar, DocumentBase, ...) AND can compile
# without frame.h being available transitively. chcm.cpp (the chunky-
# source compiler used by the chomp build tool) embeds GUI-typed
# parsers (GraphicsObjectCompiler, cursor parsing) and stays in the
# kauai (gui) target.
#
# kauai (the GUI lib) PUBLIC-depends on kauai-core, so existing
# consumers (studio, ched, ft, ut, ...) see every symbol they used
# to via the same `target_link_libraries(... kauai)` calls.
add_library(kauai-core)
target_include_directories(
  kauai-core
    PUBLIC
      "${PROJECT_SOURCE_DIR}/kauai/src"
      "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/kauai/src>")

target_sources(kauai-core
  PRIVATE
    $<$<BOOL:${SOC_X86}>:${PROJECT_BINARY_DIR}/generated/kauai/src/kcdc_386.h>
    $<$<BOOL:${SOC_X86}>:${PROJECT_BINARY_DIR}/generated/kauai/src/kcd2_386.h>

    "${PROJECT_SOURCE_DIR}/kauai/src/base.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/chse.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/chunk.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/codec.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/codkauai.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/core_platform.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/launch_context.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/settings_store.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/settings_paths.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/user_paths.cpp"
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/platform_users_win.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/kauai/src/platform_users_posix.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/settings_store_win.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/kauai/src/settings_store_posix.cpp>
    "${PROJECT_SOURCE_DIR}/kauai/src/platform_text.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/text_utf8.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/cursor_data.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/pixel_buffer.cpp"
    # cmd_core: CommandHandler + bare CommandExecutionManager. The
    # gui-coupled bits (mouse tracking, modal-gob walk, OS key pump,
    # vpappb hid registry) live in cmd_gui.cpp on the kauai (gui)
    # target via the GuiCommandExecutionManager subclass + cmd_hooks
    # seam. See docs/superpowers/plans/2026-05-02-cmd-core-split.md.
    "${PROJECT_SOURCE_DIR}/kauai/src/cmd_core.cpp"
    # clok: Clock class is a CommandHandler subclass. After the
    # cmd_core split it has no UI deps; promoted to kauai-core in
    # Task 5 of the plan.
    "${PROJECT_SOURCE_DIR}/kauai/src/clok.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/crf.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidspace_descriptor.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/file.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/groups.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/groups2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/lex.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mssio.cpp"
    # mbmp.cpp -- MaskedBitmapMBMP data side (alloc, RLE encode/decode,
    # chunk read/write, .bmp import). Pixel drawing and region clipping
    # in mbmpgui.cpp and mbmp_pixels.cpp use core memory buffers only.
    # No GraphicsPort or native graphics services are required.
    "${PROJECT_SOURCE_DIR}/kauai/src/mbmp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mbmpgui.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mbmp_pixels.cpp"
    # midi.cpp: MidiStream + MidiStreamParser. Pure parser/serializer
    # over a byte stream. The streamed cache, queues, priority mixer and timed
    # worker are also SDK-free; their device factory is injected by the caller.
    # WinMM output remains on the gui target in midi_stream_win.cpp.
    "${PROJECT_SOURCE_DIR}/kauai/src/midi.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mididev2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/midi_worker.cpp"
    # Fixed picture envelope and opaque payload; native decoding is on kauai.
    "${PROJECT_SOURCE_DIR}/kauai/src/picture_data.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/dialog_resource.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/dialog_session.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/pixel_canvas.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/pixel_stretch.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/palette_state.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/ellipse_geometry.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/font_services.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/font_canvas.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/font_names.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/rich_text_data.cpp"
    # Pure Region/RegionScanner geometry; HRGN adapters live on kauai.
    "${PROJECT_SOURCE_DIR}/kauai/src/region.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/scrcom.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/screxe.cpp"
    # sndm: SoundManager + SoundQueue. Pure data-structure / business
    # logic; instantiates no SoundDevice subclasses itself (callers
    # register them via FAddDevice). No UI, no CommandHandler, no
    # direct Win32 multimedia APIs -- those live in mididev*.cpp +
    # sndam.cpp on the gui target. Hosts register their own devices.
    "${PROJECT_SOURCE_DIR}/kauai/src/sndm.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/stream.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/util.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilcopy.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilerro.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilglob.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilint.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilmem.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilrnd.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilstr.cpp"

    # Shared file policy and filename navigation; OS calls stay in backends.
    "${PROJECT_SOURCE_DIR}/kauai/src/file_native.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/filename.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/file_browser.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/dib_frame.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/avi_movie.cpp"
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/platform_files_win.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/kauai/src/platform_files_posix.cpp>
    "${PROJECT_SOURCE_DIR}/kauai/src/memwin.cpp"

    # Stubs for Visual C++ 2.1 CRT functions
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/stub.cpp>
)

target_compile_definitions(kauai-core PUBLIC
  $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${STUDIO_PORTABLE_RUNTIME}>>>:WIN>
  $<$<BOOL:${STUDIO_PORTABLE_RUNTIME}>:KAUAI_PORTABLE_RUNTIME>
  $<$<BOOL:${SOC_X86}>:IN_80386>
  $<$<CONFIG:Debug>:DEBUG>
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${KAUAI_EXPENSIVE_CHECKS}>>:KAUAI_EXPENSIVE_CHECKS>
)

target_compile_definitions(kauai-core PUBLIC
  _LPCVOID_DEFINED
  STRICT)

# Native file calls stay in platform_files_win/posix; text and GDI still use Win32.
# Native dialogs and spelling integration belong to the GUI runtime.
# Allocation, synchronization and ticks use the portable core_platform layer.
find_package(Threads REQUIRED)
target_link_libraries(kauai-core
  PUBLIC
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:advapi32>
    $<$<PLATFORM_ID:Windows>:mpr>
    $<$<PLATFORM_ID:Windows>:shell32>)
add_executable(ellipse-geometry-test EXCLUDE_FROM_ALL tests/ellipse_geometry_test.cpp)
target_link_libraries(ellipse-geometry-test PRIVATE kauai-core)
if(WIN32)
  add_executable(ellipse-geometry-gdi-test EXCLUDE_FROM_ALL tests/ellipse_geometry_gdi_test.cpp)
  target_link_libraries(ellipse-geometry-gdi-test PRIVATE kauai-core gdi32)
endif()
install(FILES docs/licenses/ellipse-Apache-2.0.txt docs/licenses/ellipse-NOTICE.txt DESTINATION licenses)

# --- kauai (gui umbrella): everything else, transitively re-exports core ---
add_library(kauai)
target_link_libraries(kauai PRIVATE kauai-font-host)
if(KAUAI_FREETYPE)
  target_link_libraries(kauai PRIVATE kauai-freetype)
  target_compile_definitions(kauai PRIVATE KAUAI_FREETYPE)
endif()
if(STUDIO_SDL3)
  target_sources(kauai PRIVATE kauai/src/sdl_studio.cpp kauai/src/gfx_text.cpp kauai/src/gfx_pixels.cpp kauai/src/fontlist_portable.cpp
    $<$<NOT:$<BOOL:${STUDIO_NATIVE_WINDOWS}>>:${PROJECT_SOURCE_DIR}/kauai/src/gfx_posix.cpp>
    $<$<NOT:$<BOOL:${STUDIO_NATIVE_WINDOWS}>>:${PROJECT_SOURCE_DIR}/kauai/src/gob_sdl.cpp>
    $<$<NOT:$<BOOL:${STUDIO_NATIVE_WINDOWS}>>:${PROJECT_SOURCE_DIR}/kauai/src/fnidialogsdl.cpp>
    $<$<NOT:$<BOOL:${STUDIO_NATIVE_WINDOWS}>>:${PROJECT_SOURCE_DIR}/kauai/src/pic_portable.cpp> kauai/src/appbsdl.cpp kauai/src/host_event.cpp kauai/src/sdl_sound.cpp kauai/src/dlgsdl.cpp kauai/src/dialog_resources.cpp kauai/src/video_stream_sdl.cpp)
  target_link_libraries(kauai PUBLIC kauai-sdl-audio)
  target_compile_definitions(kauai PUBLIC KAUAI_SDL3)
  target_link_libraries(kauai PUBLIC kauai-sdl-host kauai-sdl-midi)
  target_link_libraries(kauai PUBLIC kauai-sdl-video)
  target_link_libraries(kauai PRIVATE kauai-sdl-dialog)
  add_executable(video-sdl-host-test EXCLUDE_FROM_ALL tests/video_sdl_host_test.cpp)
  target_link_libraries(video-sdl-host-test PRIVATE kauai)
  add_executable(app-assert-sdl-test EXCLUDE_FROM_ALL tests/app_assert_sdl_test.cpp)
  target_link_libraries(app-assert-sdl-test PRIVATE kauai)
  add_executable(dialog-controller-sdl-test EXCLUDE_FROM_ALL tests/dialog_controller_sdl_test.cpp tests/dialog_resources_portable.cpp)
  target_link_libraries(dialog-controller-sdl-test PRIVATE kauai)
  if(KAUAI_FREETYPE)
    add_executable(font-list-test EXCLUDE_FROM_ALL tests/font_list_test.cpp)
    target_link_libraries(font-list-test PRIVATE kauai)
  endif()
endif()
target_include_directories(
  kauai
    PUBLIC
      "${PROJECT_SOURCE_DIR}/kauai/src"
      "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/kauai/src>")

target_sources(kauai
  PRIVATE
    "${PROJECT_SOURCE_DIR}/kauai/src/appb.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/chcm.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/clip.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/cmd_gui.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/ctl.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/cursor.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/dlg.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/docb.cpp"
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/frame.rc>
    "${PROJECT_SOURCE_DIR}/kauai/src/gfx.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/gfx_port.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/gfx_values.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/fontlist.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/pic.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/gob.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/gob_window.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidhelp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidspace.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidworld.cpp"
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/mididev.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/midi_stream_win.cpp>
    "${PROJECT_SOURCE_DIR}/kauai/src/rtxt.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/rtxt2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/scrcomg.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/screxeg.cpp"
    # Registry/DLL-backed legacy spelling integration is a runtime service.
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/spell.cpp>
    "${PROJECT_SOURCE_DIR}/kauai/src/text.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/textdoc.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/video.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/video_window.cpp"
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/video_stream_win.cpp>

    # Windows GUI implementations
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/appbwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/app_entry_win.cpp>
    $<$<NOT:$<BOOL:${STUDIO_NATIVE_WINDOWS}>>:${PROJECT_SOURCE_DIR}/kauai/src/app_entry_posix.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/dlgwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/fnidialogwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/gfxwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/font_gdi.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/picwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/regionwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/region_native_win.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/menuwin.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/kauai/src/gobwin.cpp>
)

target_compile_definitions(kauai PUBLIC
  _LPCVOID_DEFINED
  STRICT)

target_link_libraries(kauai
  PUBLIC
    kauai-core
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:Vfw32>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:Winmm>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:comctl32>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:mpr>)
if(NOT STUDIO_SDL3)
  target_sources(kauai PRIVATE "${PROJECT_SOURCE_DIR}/kauai/src/sndam.cpp")
  target_link_libraries(kauai PUBLIC 3DMMForever::AudioMan $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:Msacm32>)
endif()

# brender-core: BRender wrapper minus the World/PWorld gui binding.
# Provides _BrDefault{Filesystem,Allocator,Diag} + the TextureMap and
# ZBMP wrappers + the std{file,err,mem} adapters that BRender::Libraries
# needs at link time. No kauai-gui link dep -- pulls only kauai-core.
add_library(brender-core)
target_sources(brender-core
    PRIVATE
    "${PROJECT_SOURCE_DIR}/bren/stderr.c"
    "${PROJECT_SOURCE_DIR}/bren/stdfile.c"
    "${PROJECT_SOURCE_DIR}/bren/stdmem.c"
    "${PROJECT_SOURCE_DIR}/bren/tmap.cpp"
    "${PROJECT_SOURCE_DIR}/bren/zbmp.cpp"
)
target_include_directories(brender-core PUBLIC "${PROJECT_SOURCE_DIR}/bren/inc")
target_link_libraries(brender-core
  PUBLIC
    kauai-core
    BRender::Libraries)

# brender: full wrapper including bwld.cpp (the World class that
# wraps a BRender world together with kauai gui bits like
# PGraphicsPort). PUBLIC re-exports brender-core + kauai gui.
add_library(brender)
target_sources(brender
    PRIVATE
    "${PROJECT_SOURCE_DIR}/bren/bwld.cpp"
)
target_include_directories(brender PUBLIC "${PROJECT_SOURCE_DIR}/bren/inc")
target_link_libraries(brender
  PUBLIC
    brender-core
    kauai
    BRender::Libraries)

# --- engine-core: stored data, assets, evaluation and playback policy ---
# Links only kauai-core + brender-core. Actor editing/playback, backgrounds
# bound to World, sound import/playback and recording belong to engine.
# World binding lives in body_world.cpp on engine; see library-split.md.
add_library(engine-core)
target_sources(engine-core
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/body.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie_data.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/portfolio_data.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/scene_data.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_data.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_track.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_route.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_state.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_pose.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_frame.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_timeline.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor_seek.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/text_box_data.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/sound_queue.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie_playback.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/retail_scroll.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/modl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/mtrl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagman.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdf.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdt.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tmpl.cpp"
)
target_include_directories(engine-core PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>
  # brender wrapper headers (BMAT34 alias, etc); the gui-side bwld.cpp
  # is NOT linked from engine-core, so we just need the include path.
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/bren/inc>)
target_link_libraries(engine-core PUBLIC kauai-core brender-core three-mm-engine-c89)
add_executable(script-name-test EXCLUDE_FROM_ALL tests/script_name_test.cpp)
target_link_libraries(script-name-test PRIVATE kauai-core)
add_executable(brender-list-test EXCLUDE_FROM_ALL tests/brender_list_test.c)
target_include_directories(brender-list-test PRIVATE "${PROJECT_SOURCE_DIR}/bren/lib/fw")
target_compile_definitions(brender-list-test PRIVATE BASED_FIXED=1)
target_link_libraries(brender-list-test PRIVATE brender_fw)

# Compile Studio's editor/startup against the portable runtime headers before
# enabling the complete POSIX executable and its content build pipeline.
# Windows codec installation remains a separate platform dependency.
add_library(studio-runtime-compile-check OBJECT EXCLUDE_FROM_ALL
  src/studio/ape.cpp src/studio/browser.cpp src/studio/esl.cpp
  src/studio/popup.cpp src/studio/portf.cpp src/studio/resource_tables.cpp
  src/studio/portfolio_sdl.cpp src/studio/scnsort.cpp src/studio/splot.cpp
  src/studio/stdiobrw.cpp src/studio/stdioscb.cpp src/studio/studio.cpp
  src/studio/tatr.cpp src/studio/tgob.cpp src/studio/utest.cpp src/studio/mcpserv.cpp
  kauai/src/scrcomg.cpp)
target_compile_definitions(studio-runtime-compile-check PRIVATE KAUAI_SDL3)
target_include_directories(studio-runtime-compile-check PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/studio")
target_link_libraries(studio-runtime-compile-check PRIVATE engine-core studio-content)
target_link_libraries(studio-runtime-compile-check PRIVATE studio-mcp-io)
target_link_libraries(studio-runtime-compile-check PRIVATE three-mm-v3dmm)
if(TARGET SDL3::SDL3-static)
  target_link_libraries(studio-runtime-compile-check PRIVATE SDL3::SDL3-static)
endif()
add_executable(studio-layout-test EXCLUDE_FROM_ALL tests/studio_layout_test.cpp)
target_compile_definitions(studio-layout-test PRIVATE KAUAI_SDL3)
target_include_directories(studio-layout-test PRIVATE "${PROJECT_SOURCE_DIR}/src/studio")
target_link_libraries(studio-layout-test PRIVATE engine-core)
add_executable(studio-resources-test EXCLUDE_FROM_ALL tests/studio_resources_test.cpp src/studio/resource_tables.cpp)
target_include_directories(studio-resources-test PRIVATE "${PROJECT_SOURCE_DIR}/inc")
target_link_libraries(studio-resources-test PRIVATE kauai-core)
add_executable(kidspace-descriptor-test EXCLUDE_FROM_ALL tests/kidspace_descriptor_test.cpp)
target_link_libraries(kidspace-descriptor-test PRIVATE kauai-core)

# Public archive boundary: legacy include paths/definitions stay private.
add_library(three-mm-archive STATIC EXCLUDE_FROM_ALL
  libraries/archive/src/archive.cpp libraries/archive/src/diagnostics.cpp
  libraries/archive/src/legacy_diagnostics.cpp)
add_library(ThreeMM::Archive ALIAS three-mm-archive)
target_include_directories(three-mm-archive PUBLIC "${PROJECT_SOURCE_DIR}/libraries/archive/include")
target_compile_features(three-mm-archive PUBLIC cxx_std_14)
target_link_libraries(three-mm-archive PRIVATE engine-core)
add_library(three-mm-preview STATIC EXCLUDE_FROM_ALL
  libraries/archive/src/preview.cpp libraries/archive/src/preview_actor.cpp
  libraries/archive/src/preview_background.cpp libraries/archive/src/preview_background_mesh.cpp
  libraries/archive/src/preview_presentation.cpp)
add_library(ThreeMM::Preview ALIAS three-mm-preview)
target_link_libraries(three-mm-preview PUBLIC ThreeMM::Archive PRIVATE engine-core)
add_executable(preview-api-test EXCLUDE_FROM_ALL tests/preview_api_test.cpp tests/preview_api_include_order.cpp)
target_link_libraries(preview-api-test PRIVATE ThreeMM::Preview)
add_executable(preview-scene-fixture EXCLUDE_FROM_ALL tests/preview_scene_fixture.cpp)
target_link_libraries(preview-scene-fixture PRIVATE engine-core)
add_executable(preview-scene-test EXCLUDE_FROM_ALL tests/preview_scene_test.cpp)
target_link_libraries(preview-scene-test PRIVATE ThreeMM::Preview)
add_executable(preview-background-test EXCLUDE_FROM_ALL tests/preview_background_test.cpp)
target_link_libraries(preview-background-test PRIVATE ThreeMM::Preview)
add_executable(preview-background-mesh-test EXCLUDE_FROM_ALL tests/preview_background_mesh_test.cpp)
target_include_directories(preview-background-mesh-test PRIVATE "${PROJECT_SOURCE_DIR}/libraries/archive/src"
  "${PROJECT_SOURCE_DIR}/bren/lib/zb" "${PROJECT_SOURCE_DIR}/bren/lib/fw")
target_link_libraries(preview-background-mesh-test PRIVATE ThreeMM::Preview engine-core)
add_custom_target(preview-checks DEPENDS preview-api-test preview-scene-test preview-background-test
  preview-background-mesh-test preview-scene-fixture)
add_executable(mbmp-pixels-test EXCLUDE_FROM_ALL tests/mbmp_pixels_test.cpp)
add_executable(portfolio-data-test EXCLUDE_FROM_ALL tests/portfolio_data_test.cpp)
target_link_libraries(portfolio-data-test PRIVATE engine-core)
target_link_libraries(mbmp-pixels-test PRIVATE kauai-core)
add_executable(archive-api-test EXCLUDE_FROM_ALL tests/archive_api_test.cpp tests/archive_api_include_order.cpp)
target_link_libraries(archive-api-test PRIVATE ThreeMM::Archive)
add_executable(inspector-gst-fixture EXCLUDE_FROM_ALL tests/inspector_gst_fixture.cpp)
target_link_libraries(inspector-gst-fixture PRIVATE kauai-core)

# Presentation adapters consume only the public archive API.
add_library(inspector-data STATIC EXCLUDE_FROM_ALL src/inspector/archive.cpp)
target_include_directories(inspector-data PUBLIC "${PROJECT_SOURCE_DIR}/src/inspector")
target_link_libraries(inspector-data PUBLIC ThreeMM::Archive)
if(BUILD_INSPECTOR)
  add_subdirectory(src/inspector)
endif()
if(BUILD_MARCUS)
  add_subdirectory(src/marcus)
endif()

# --- engine: full playback runtime, transitively re-exports core ---
add_library(engine)
target_sources(engine
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/body_world.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actor.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actredit.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsave.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/bkgd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/msnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie_container.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/scene.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tbox.cpp"
)
target_include_directories(engine PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>)
target_link_libraries(engine PUBLIC engine-core kauai brender)
# Compile the actual playback sources with SDL interfaces independently of the
# complete GUI link, so POSIX type/OS dependencies cannot hide behind headers.
get_target_property(ENGINE_RUNTIME_SOURCES engine SOURCES)
add_library(engine-runtime-compile-check OBJECT EXCLUDE_FROM_ALL
  ${ENGINE_RUNTIME_SOURCES} src/engine/srec_sdl.cpp bren/bwld.cpp)
target_compile_definitions(engine-runtime-compile-check PRIVATE KAUAI_SDL3)
target_link_libraries(engine-runtime-compile-check PRIVATE engine-core)
if(STUDIO_SDL3)
  target_sources(engine PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/srec_sdl.cpp")
else()
  target_sources(engine PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/srec.cpp")
endif()

if(BUILD_STUDIO)
# On non-windows WIN32 is a no-op
set(STUDIO_BUILD_STAMP_SRC "${PROJECT_BINARY_DIR}/generated/studio/build_stamp.cpp")
add_custom_target(studio_build_stamp ALL
    BYPRODUCTS "${STUDIO_BUILD_STAMP_SRC}"
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${STUDIO_BUILD_STAMP_SRC}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/WriteBuildStamp.cmake
    COMMENT "Generating studio build stamp"
    VERBATIM
)
set_source_files_properties("${STUDIO_BUILD_STAMP_SRC}" PROPERTIES GENERATED TRUE)

add_executable(studio WIN32)
if(WIN32 AND STUDIO_PORTABLE_RUNTIME)
  # Keep the SDK-free main(argc, argv) adapter, but launch as a GUI application.
  # MSVC otherwise selects WinMainCRTStartup for the Windows subsystem. The
  # main CRT startup still initializes argv and inherited MCP stdio pipes.
  target_link_options(studio PRIVATE $<$<LINK_LANG_AND_ID:CXX,MSVC>:/ENTRY:mainCRTStartup>)
endif()
if(STUDIO_SDL3)
  kauai_copy_midi_assets(studio)
endif()
target_sources(studio
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/studio/ape.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/browser.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/esl.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/mcpserv.cpp"
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/src/studio/mminstal.cpp>
    "${PROJECT_SOURCE_DIR}/src/studio/popup.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/portf.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/resource_tables.cpp"
    $<$<BOOL:${STUDIO_SDL3}>:${PROJECT_SOURCE_DIR}/src/studio/dialog_resources.cpp>
    $<$<BOOL:${STUDIO_SDL3}>:${PROJECT_SOURCE_DIR}/src/studio/portfolio_sdl.cpp>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${STUDIO_SDL3}>>>:${PROJECT_SOURCE_DIR}/src/studio/portfolio_win.cpp>
    "${PROJECT_SOURCE_DIR}/src/studio/scnsort.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/splot.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdiobrw.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdioscb.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/studio.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tatr.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tgob.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/utest.cpp"
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/src/studio/timing_win.cpp>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/src/studio/utest.rc>
    $<$<BOOL:${STUDIO_NATIVE_WINDOWS}>:${PROJECT_SOURCE_DIR}/src/studio/utest.rc2>
    "${STUDIO_BUILD_STAMP_SRC}"
)
add_dependencies(studio studio_build_stamp)
target_include_directories(studio
  PUBLIC $<TARGET_PROPERTY:engine,INCLUDE_DIRECTORIES>
  "${PROJECT_SOURCE_DIR}/src")
target_chomp_sources(studio
  "${PROJECT_SOURCE_DIR}/src/studio/utest.cht"
  "${PROJECT_SOURCE_DIR}/src/studio/studio.cht"
  "${PROJECT_SOURCE_DIR}/src/building/bldghd.cht"
  "${PROJECT_SOURCE_DIR}/src/building/building.cht"
  "${PROJECT_SOURCE_DIR}/src/helpaud/helpaud.cht"
  "${PROJECT_SOURCE_DIR}/src/help/help.cht"
  "${PROJECT_SOURCE_DIR}/src/shared/shared.cht"
  "${PROJECT_SOURCE_DIR}/src/shared/sharecd.cht")

target_link_libraries(studio PRIVATE engine studio-content studio-mcp-io)
if(STUDIO_SDL3)
  target_sources(studio PRIVATE src/studio/iso_bootstrap.cpp src/studio/embedded_midi.cpp)
  target_link_libraries(studio PRIVATE studio-iso-cache SDL3::SDL3-static kauai-compressed-wave)
  target_sources(studio PRIVATE src/studio/pack_runtime.cpp src/studio/pack_options.cpp src/studio/v3dmm_runtime.cpp)
  target_link_libraries(studio PRIVATE studio-settings studio-pack-store)
  include(StudioBundledMIDI)
  studio_bundle_midi(studio)
endif()
set_property(TARGET studio PROPERTY OUTPUT_NAME 3dmovie)
if(STUDIO_SDL3)
  set_property(TARGET studio PROPERTY OUTPUT_NAME 3dmovie-sdl)
endif()
target_link_options(studio BEFORE PRIVATE $<$<LINK_LANG_AND_ID:CXX,MSVC>:/MANIFESTUAC:NO>)

# Mirror the freshly linked exe into ${CMAKE_INSTALL_PREFIX} (dist/ for the
# default presets) so iterative dev runs of dist/3dmovie.exe pick up rebuilds
# without needing a separate `cmake --install` step. The chunk/data files are
# only refreshed by a real install -- this only copies the exe.
if(NOT STUDIO_SDL3)
add_custom_command(TARGET studio POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:studio>" "${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:studio>"
  COMMENT "Mirroring 3dmovie.exe to ${CMAKE_INSTALL_PREFIX}"
  VERBATIM)
endif()
target_link_options(studio
  BEFORE PRIVATE
    $<$<AND:$<LINK_LANG_AND_ID:CXX,MSVC>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:libcmt.lib>
    # /MAP emits a text symbol map alongside the exe (3dmovie.map). Useful for
    # resolving raw addresses from the unhandled-exception dialog stack trace
    # back to function names without needing a full PDB-aware debugger.
    $<$<LINK_LANG_AND_ID:CXX,MSVC>:/MAP>)

set_property(
    TARGET studio
    PROPERTY 3DMM_SAMPLES
    ${studio-samples}
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/BONGO.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/BOOOOOO.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/CITYTOUR.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/GRAVEYRD.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/HAUNTED.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/HOSPITAL.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/JUNGLE.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/MESSAGE.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/SPACE.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/SPROG.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/TERROR.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/THEBOOK.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/THELODGE.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/THETHIEF.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/VENUS31.3MM"
    "${PROJECT_SOURCE_DIR}/cd3/SAMPLES/WHERE.3MM"
)

# Please pay attention to the ending / in each DIRECTORY declaration. It
# affects the destination!
install(TARGETS studio RUNTIME DESTINATION .)
# HACK(bruxisma): This is due to UTEST.CHT not generating 3DMOVIE.CHK
#                 Renaming it to 3DMOVIE.CHT would resolve this.
set(studio_utest_pattern ".+[Uu][Tt][Ee][Ss][Tt][.]chk$")
install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,EXCLUDE,${studio_utest_pattern}>
  DESTINATION "Microsoft Kids/3D Movie Maker")
install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,INCLUDE,${studio_utest_pattern}>
  RENAME 3dmovie.chk
  DESTINATION "Microsoft Kids/3D Movie Maker")

# Overlay non-chomped data (BLDGDATA.CHK, *.3TH, *.3CN, MSCREATE.DIR, help
# files, fix.dat, ...) from an existing system install so dist/ becomes a
# self-contained, runnable tree. Files we produce ourselves take precedence:
# the overlay copy only fills in destinations that don't already exist.
# Sequenced before the cd3 SAMPLES install below: that step hard-errors when
# cd3/SAMPLES/ is missing locally, and we want the overlay to land regardless.
if(WIN32)
  set(mskids_overlay_default "C:/Program Files (x86)/Microsoft Kids")
else()
  set(mskids_overlay_default "")
endif()
set(MSKIDS_OVERLAY_DIR "${mskids_overlay_default}"
  CACHE PATH "Existing Microsoft Kids tree to overlay original content from; empty to skip")
install(CODE "
  set(_overlay [[${MSKIDS_OVERLAY_DIR}]])
  if(_overlay AND IS_DIRECTORY \"\${_overlay}\")
    message(STATUS \"Overlaying non-chomped data from \${_overlay}\")
    # Only mirror the subdirs 3DMM actually reads. Skip third-party content
    # (DORAEMON, EXPANSIONS, ...) and skip executables -- our build owns the
    # exe.
    file(GLOB_RECURSE _3dmm_files
      RELATIVE \"\${_overlay}\"
      \"\${_overlay}/3D Movie Maker/*\")
    file(GLOB_RECURSE _user_files
      RELATIVE \"\${_overlay}\"
      \"\${_overlay}/Users/*\")
    set(_overlay_files \${_3dmm_files} \${_user_files} \"MSCREATE.DIR\")
    foreach(_rel IN LISTS _overlay_files)
      string(TOLOWER \"\${_rel}\" _rel_lower)
      # Skip authored archives in any spelling: on a case-sensitive host an
      # uppercase original could otherwise shadow the freshly built lowercase file.
      if(_rel_lower MATCHES \"^3d movie maker/(3dmovie|utest|studio|building|bldghd|shared|sharecd|help|helpaud)[.]chk$\")
        continue()
      endif()
      if(_rel MATCHES \"\\\\.(exe|EXE)$\")
        continue()
      endif()
      set(_src \"\${_overlay}/\${_rel}\")
      if(NOT EXISTS \"\${_src}\")
        continue()
      endif()
      set(_dst \"\${CMAKE_INSTALL_PREFIX}/Microsoft Kids/\${_rel}\")
      if(NOT EXISTS \"\${_dst}\")
        get_filename_component(_dst_dir \"\${_dst}\" DIRECTORY)
        file(MAKE_DIRECTORY \"\${_dst_dir}\")
        file(COPY \"\${_src}\" DESTINATION \"\${_dst_dir}\")
      endif()
    endforeach()
  endif()
")

install(DIRECTORY DESTINATION "Microsoft Kids/Users/Melanie")
get_target_property(studio_sample_files studio 3DMM_SAMPLES)
foreach(sample IN LISTS studio_sample_files)
  if(EXISTS "${sample}")
    install(FILES "${sample}" DESTINATION "Microsoft Kids/Users/Melanie")
  endif()
endforeach()
install(DIRECTORY "${PROJECT_SOURCE_DIR}/cd9/"
  DESTINATION "Microsoft Kids/3D Movie Maker" OPTIONAL)
# Original AVI media is supplied separately from the source assets. Keep the
# existing product-directory lookup and include available clips in local installs.
set(STUDIO_AVI_SOURCE_DIR "${PROJECT_SOURCE_DIR}/content-files" CACHE PATH "Directory containing original Studio AVI clips; empty to skip")
if(STUDIO_AVI_SOURCE_DIR)
  file(GLOB studio_avi_files CONFIGURE_DEPENDS "${STUDIO_AVI_SOURCE_DIR}/*.avi")
  if(studio_avi_files)
    install(FILES ${studio_avi_files} DESTINATION "Microsoft Kids/3D Movie Maker")
  endif()
endif()

# CPack must be included *last*, which is antithetical to how most CMake
# Modules work. We can set all variables for all packaging regardless of
# whether they're supported. This way we can just swap out what is added to
# `CPACK_GENERATOR`

# DO NOT CHANGE THIS. EVER. Generated manually via uuidgen. This needs to be
# the same so that user's can automatically upgrade via MSI
set(CPACK_WIX_UPGRADE_GUID 01c5fef4-82b5-4194-aaf3-bd0247004941)
# Change this as needed.
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT https://github.com/foone/3DMMForever)

if (BUILD_PACKAGES)
  add_custom_target(dist DEPENDS $<TARGET_NAME_IF_EXISTS:package>)
  foreach (generator IN ITEMS ZIP WIX)
    if (3DMM_PACKAGE_${generator})
      list(APPEND CPACK_GENERATOR ${generator})
    endif()
  endforeach()
  include(CPack)
endif()

endif() # BUILD_STUDIO

if(STUDIO_PORTABLE_RUNTIME OR (STUDIO_SDL3 AND NOT WIN32))
  add_executable(picture-portable-test EXCLUDE_FROM_ALL tests/picture_portable_test.cpp)
  target_link_libraries(picture-portable-test PRIVATE kauai)
  add_test(NAME picture-portable COMMAND picture-portable-test)
endif()

# Force every core archive into an independent consumer on each platform.
if(MSVC AND NOT STUDIO_PORTABLE_RUNTIME)
  add_library(core-native-interop-check OBJECT EXCLUDE_FROM_ALL tests/native_interop_test.cpp)
  target_link_libraries(core-native-interop-check PRIVATE engine-core)
endif()
foreach(core IN ITEMS kauai-core brender-core engine-core)
  add_executable(${core}-link-test EXCLUDE_FROM_ALL tests/core_link_test.cpp)
  if(MSVC OR APPLE)
    target_link_libraries(${core}-link-test PRIVATE ${core})
  endif()
  set(core_archives ${core})
  if(core STREQUAL "engine-core")
    target_compile_definitions(${core}-link-test PRIVATE TEST_ENGINE_CORE)
    list(APPEND core_archives brender-core kauai-core)
  elseif(core STREQUAL "brender-core")
    target_compile_definitions(${core}-link-test PRIVATE TEST_BRENDER_CORE)
    list(APPEND core_archives kauai-core)
  endif()
  foreach(archive IN LISTS core_archives)
    if(MSVC)
      target_link_options(${core}-link-test PRIVATE /OPT:NOREF /WHOLEARCHIVE:$<TARGET_FILE:${archive}>)
    elseif(APPLE)
      target_link_options(${core}-link-test PRIVATE "LINKER:-force_load,$<TARGET_FILE:${archive}>")
    else()
      target_link_libraries(${core}-link-test PRIVATE "-Wl,--whole-archive" ${archive} "-Wl,--no-whole-archive")
    endif()
  endforeach()
endforeach()

add_custom_target(core-checks DEPENDS geometry-test codec-test brender-diagnostic-test
  engine-c89-test engine-movie-data-test
  global-cleanup-test
  preview-checks
  mbmp-pixels-test
  actor-render-test extract-bmdl inspect-chunks three-mm three-mm-fixture
  nick-expansion nick-expansion-test
  platform-services-test file-services-test user-services-test settings-store-test filename-test text-services-test
  text-portable-test string-compat-test kauai-core-link-test brender-core-link-test engine-core-link-test)
add_dependencies(core-checks picture-data-test dialog-resource-test dialog-api-check dialog-controller-api-check dialog-model-test legacy-math-test pixel-canvas-test font-services-test cursor-data-test)
add_dependencies(core-checks resource-cache-test)
add_dependencies(core-checks debug-checks-test)
add_dependencies(core-checks kidspace-descriptor-test)
add_dependencies(core-checks mcp-io-test)
add_dependencies(core-checks script-name-test)
add_dependencies(core-checks brender-list-test)
add_dependencies(core-checks portfolio-api-check)
add_dependencies(core-checks video-api-check)
add_dependencies(core-checks graphics-api-test)
add_dependencies(core-checks portfolio-data-test)
add_dependencies(core-checks file-browser-test)
add_dependencies(core-checks dib-frame-test)
add_dependencies(core-checks gob-api-check)
add_dependencies(core-checks app-api-check app-sdl-api-check)
add_dependencies(core-checks avi-movie-test)
if(TARGET avi-corpus-win-test AND EXISTS "${PROJECT_SOURCE_DIR}/content-files/proj1.avi")
  add_dependencies(core-checks avi-corpus-win-test)
endif()
if(TARGET dib-frame-gdi-test)
  add_dependencies(core-checks dib-frame-gdi-test)
  add_dependencies(core-checks video-stream-host-test)
endif()
add_library(sdl-sound-boundary-check OBJECT EXCLUDE_FROM_ALL kauai/src/sdl_sound.cpp)
target_link_libraries(sdl-sound-boundary-check PRIVATE kauai-core kauai-audio-mixer)
add_dependencies(core-checks sdl-sound-boundary-check)
if(KAUAI_MIDI_SYNTH OR BUILD_MARCUS)
  add_dependencies(core-checks midi-synth-test)
endif()
if(TARGET midi-sdl-test)
  add_dependencies(core-checks midi-sdl-test)
endif()
add_dependencies(core-checks sdl-runtime-compile-check studio-runtime-compile-check engine-runtime-compile-check studio-layout-test studio-resources-test studio-content-test studio-startup-test studio-entry-test midi-mixer-test midi-worker-test midi-stream-api-check palette-state-test pixel-buffer-test damage-map-test studio-shortcuts-test audio-mixer-test wave-codec-test recording-data-test ellipse-geometry-test)
if(KAUAI_FREETYPE)
  add_dependencies(core-checks font-freetype-test)
  add_dependencies(core-checks font-host-test)
endif()
if(TARGET font-list-test)
  add_dependencies(core-checks font-list-test)
endif()
if(TARGET settings-store-win-test)
  add_dependencies(core-checks settings-store-win-test)
endif()
if(TARGET settings-ini-test)
  add_dependencies(core-checks settings-ini-test)
endif()
if(TARGET settings-store-posix-test)
  add_dependencies(core-checks settings-store-posix-test)
endif()
add_dependencies(core-checks archive-api-test inspector-gst-fixture)
if(WIN32 AND SOC_X86 AND AudioMan_FOUND)
  add_executable(wave-audioman-compat-test EXCLUDE_FROM_ALL tests/wave_audioman_compat_test.cpp)
  target_sources(wave-audioman-compat-test PRIVATE kauai/src/stub.cpp)
  target_link_libraries(wave-audioman-compat-test PRIVATE kauai-wave-codec 3DMMForever::AudioMan winmm msacm32 ole32)
  target_link_options(wave-audioman-compat-test PRIVATE
    $<$<AND:$<LINK_LANG_AND_ID:CXX,MSVC>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:libcmt.lib>)
  add_dependencies(core-checks wave-audioman-compat-test)
  add_executable(recording-audioman-compat-test EXCLUDE_FROM_ALL tests/recording_audioman_compat_test.cpp kauai/src/stub.cpp)
  target_link_libraries(recording-audioman-compat-test PRIVATE kauai-recording-data 3DMMForever::AudioMan winmm msacm32 ole32)
  target_link_options(recording-audioman-compat-test PRIVATE
    $<$<AND:$<LINK_LANG_AND_ID:CXX,MSVC>,$<CONFIG:DEBUG>>:/NODEFAULTLIB:libcmt.lib>)
  add_dependencies(core-checks recording-audioman-compat-test)
endif()

if(BUILD_TESTING)
  if(TARGET sdl-surface-test)
    add_dependencies(core-checks sdl-surface-test)
    add_test(NAME sdl-surface COMMAND sdl-surface-test)
  endif()
  if(TARGET sdl-host-test)
    add_dependencies(core-checks sdl-host-test)
    add_test(NAME sdl-indexed-presentation COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:sdl-host-test> "${PROJECT_BINARY_DIR}/sdl-indexed-presentation.bmp" --indexed-only)
  endif()
  add_test(NAME global-cleanup COMMAND global-cleanup-test)
  add_test(NAME brender-list COMMAND brender-list-test)
  add_test(NAME script-name COMMAND script-name-test)
  add_test(NAME mcp-io COMMAND mcp-io-test)
  set_tests_properties(mcp-io PROPERTIES TIMEOUT 10)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME nick-expansion-reconstruction COMMAND "${Python3_EXECUTABLE}" -B
      "${PROJECT_SOURCE_DIR}/tests/test_recreate_nick_expansion.py"
      --converter "$<TARGET_FILE:nick-expansion>" --fixtures "$<TARGET_FILE:nick-expansion-test>")
    set_tests_properties(nick-expansion-reconstruction PROPERTIES TIMEOUT 30)
    add_test(NAME mcp-png COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tests/mcp_png_test.py" "$<TARGET_FILE:mcp-io-test>")
    set_tests_properties(mcp-png PROPERTIES TIMEOUT 15)
    add_test(NAME mcp-rig COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tests/mcp_rig_test.py")
    add_test(NAME reference-trace COMMAND "${Python3_EXECUTABLE}" -B
      "${PROJECT_SOURCE_DIR}/tests/reference_trace_test.py")
    set_tests_properties(reference-trace PROPERTIES TIMEOUT 15)
    add_test(NAME playback-regression COMMAND "${Python3_EXECUTABLE}" -B
      "${PROJECT_SOURCE_DIR}/tests/playback_regression_test.py")
    set_tests_properties(playback-regression PROPERTIES TIMEOUT 30)
    set_tests_properties(mcp-rig PROPERTIES TIMEOUT 15
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    if(BUILD_STUDIO)
      if(CHOMP_EXECUTABLE)
        set(chomp_test_command "${CHOMP_EXECUTABLE}")
      elseif(NOT CMAKE_CROSSCOMPILING)
        set(chomp_test_command "$<TARGET_FILE:chomp>")
        add_dependencies(core-checks chomp)
      endif()
      if(chomp_test_command)
        add_test(NAME chomp-pipeline COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tests/chomp_pipeline_test.py"
          "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${chomp_test_command}"
          "${PROJECT_SOURCE_DIR}/cmake/TargetChompSources.cmake"
          "${PROJECT_BINARY_DIR}/chomp-pipeline-test" "--msvc=${MSVC}")
        set_tests_properties(chomp-pipeline PROPERTIES TIMEOUT 60)
      endif()
    endif()
  endif()
  if(TARGET window-services-sdl-test)
    add_dependencies(core-checks window-services-sdl-test)
    add_test(NAME window-services-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software $<TARGET_FILE:window-services-sdl-test>)
    if(NOT STUDIO_SDL3)
      add_dependencies(core-checks sdl-host-test)
      add_test(NAME sdl-host COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software $<TARGET_FILE:sdl-host-test> "${PROJECT_BINARY_DIR}/sdl-host-test.bmp")
    endif()
  endif()
  if(TARGET app-assert-sdl-test)
    add_dependencies(core-checks app-assert-sdl-test)
    add_test(NAME app-assert-sdl COMMAND app-assert-sdl-test "${PROJECT_BINARY_DIR}/assertion-dialog.bmp")
    set_tests_properties(app-assert-sdl PROPERTIES ENVIRONMENT "SDL_RENDER_DRIVER=software" TIMEOUT 30 SKIP_RETURN_CODE 77)
  endif()
  if(TARGET window-geometry-sdl-test)
    add_dependencies(core-checks window-geometry-sdl-test)
    add_test(NAME window-geometry-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=windows SDL_RENDER_DRIVER=software $<TARGET_FILE:window-geometry-sdl-test>)
  endif()
  if(TARGET video-sdl-test)
    add_dependencies(core-checks video-sdl-test)
    add_test(NAME video-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:video-sdl-test>)
  endif()
  if(TARGET video-sdl-host-test)
    add_dependencies(core-checks video-sdl-host-test)
    add_test(NAME video-sdl-host COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:video-sdl-host-test> "${PROJECT_BINARY_DIR}/video-sdl-fixture.avi")
  endif()
  add_test(NAME archive-api COMMAND ${CMAKE_COMMAND}
    -DTEST_EXE=$<TARGET_FILE:archive-api-test> -DFIXTURE_EXE=$<TARGET_FILE:three-mm-fixture>
    -DGST_FIXTURE_EXE=$<TARGET_FILE:inspector-gst-fixture>
    -DTEST_DIR=${PROJECT_BINARY_DIR}/archive-api-test -P ${PROJECT_SOURCE_DIR}/tests/check_archive_api.cmake)
  add_test(NAME picture-data COMMAND picture-data-test)
  add_test(NAME dialog-resource COMMAND dialog-resource-test)
  add_test(NAME dialog-model COMMAND dialog-model-test)
  add_test(NAME pixel-canvas COMMAND pixel-canvas-test)
  add_test(NAME resource-cache COMMAND resource-cache-test)
  add_test(NAME font-services COMMAND font-services-test)
  if(TARGET font-list-test)
    add_test(NAME font-list COMMAND font-list-test)
  endif()
  if(KAUAI_FREETYPE AND KAUAI_FONT_TEST_TTF AND KAUAI_FONT_TEST_FON)
    add_test(NAME font-freetype COMMAND font-freetype-test "${KAUAI_FONT_TEST_TTF}" "${KAUAI_FONT_TEST_FON}" ${KAUAI_FONT_TEST_COLLECTION})
    add_test(NAME font-host COMMAND font-host-test "${KAUAI_FONT_TEST_TTF}" "${KAUAI_FONT_TEST_FON}" "${PROJECT_BINARY_DIR}/font-host-fixture")
  endif()
  add_test(NAME legacy-math COMMAND legacy-math-test)
  add_test(NAME debug-checks COMMAND debug-checks-test)
  add_test(NAME cursor-data COMMAND cursor-data-test)
  if(KAUAI_MIDI_SYNTH)
    add_test(NAME midi-synth COMMAND midi-synth-test "${KAUAI_MIDI_BANK_FILE}")
  endif()
  if(TARGET midi-sdl-test)
    add_test(NAME midi-sdl COMMAND midi-sdl-test)
    set_tests_properties(midi-sdl PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy" TIMEOUT 15)
  endif()
  add_test(NAME midi-mixer COMMAND midi-mixer-test)
  set_tests_properties(midi-mixer PROPERTIES TIMEOUT 10)
  add_test(NAME midi-worker COMMAND midi-worker-test)
  set_tests_properties(midi-worker PROPERTIES TIMEOUT 10)
  add_test(NAME palette-state COMMAND palette-state-test)
  add_test(NAME pixel-buffer COMMAND pixel-buffer-test)
  if(TARGET pixel-port-test)
    add_dependencies(core-checks pixel-port-test)
    add_test(NAME pixel-port COMMAND pixel-port-test)
    if(NOT WIN32)
      add_test(NAME pixel-port-window COMMAND pixel-port-test --sdl-window)
      set_tests_properties(pixel-port-window PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
    endif()
  endif()
  add_test(NAME damage-map COMMAND damage-map-test)
  if(WIN32)
    add_executable(studio-timing-test EXCLUDE_FROM_ALL tests/studio_timing_win_test.cpp src/studio/timing_win.cpp)
    target_include_directories(studio-timing-test PRIVATE "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/kauai/src")
    target_link_libraries(studio-timing-test PRIVATE kauai-core)
    add_dependencies(core-checks studio-timing-test)
    add_test(NAME studio-timing COMMAND studio-timing-test)
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_dependencies(core-checks bren-rasterizer-test)
    add_test(NAME brender-clipped-edges COMMAND bren-rasterizer-test clipped-edges -)
    add_test(NAME brender-flat-shading COMMAND bren-rasterizer-test flat-faces -)
    add_test(NAME brender-divr-parity COMMAND bren-rasterizer-test divr-parity -)
    add_test(NAME brender-atan-parity COMMAND bren-rasterizer-test atan-parity -)
    add_test(NAME brender-integer-parity COMMAND bren-rasterizer-test integer-parity -)
    add_test(NAME brender-textured-parity COMMAND bren-rasterizer-test textured-parity -)
  endif()
  add_test(NAME studio-content COMMAND studio-content-test)
  add_test(NAME studio-iso-cache COMMAND studio-iso-cache-test "${PROJECT_SOURCE_DIR}/tmp/iso-cache-unit")
  add_test(NAME studio-packs COMMAND studio-packs-test "${PROJECT_SOURCE_DIR}/tmp/packs-unit")
  add_dependencies(core-checks studio-packs-test)
  add_test(NAME v3dmm COMMAND v3dmm-test "${PROJECT_SOURCE_DIR}/tmp/v3dmm-unit")
  add_test(NAME content-vfs COMMAND content-vfs-test)
  add_dependencies(core-checks content-vfs-test)
  find_program(VFS_C89_COMPILER NAMES clang gcc)
  if(VFS_C89_COMPILER)
    add_test(NAME content-vfs-c89 COMMAND "${VFS_C89_COMPILER}" -std=c89 -pedantic-errors
      -Werror=declaration-after-statement -Wall -Wextra -fsyntax-only
      "-I${PROJECT_SOURCE_DIR}/libraries/content/include"
      "${PROJECT_SOURCE_DIR}/libraries/content/src/content_vfs.c"
      "${PROJECT_SOURCE_DIR}/tests/content_vfs_test.c")
  endif()
  add_dependencies(core-checks v3dmm-test)
  add_dependencies(core-checks studio-iso-cache-test)
  add_test(NAME studio-layout COMMAND studio-layout-test)
  add_test(NAME studio-resources COMMAND studio-resources-test)
  add_test(NAME kidspace-descriptor COMMAND kidspace-descriptor-test)
  add_test(NAME studio-startup COMMAND studio-startup-test)
  add_test(NAME studio-entry COMMAND studio-entry-test -m "-pMovie Maker" -- "/films/my \"cut\".3mm")
  add_test(NAME studio-shortcuts COMMAND studio-shortcuts-test)
  add_test(NAME audio-mixer COMMAND audio-mixer-test)
  add_test(NAME wave-codec COMMAND wave-codec-test)
  add_test(NAME recording-data COMMAND recording-data-test)
  add_test(NAME ellipse-geometry COMMAND ellipse-geometry-test)
  if(TARGET wave-audioman-compat-test)
    add_test(NAME wave-audioman-compat COMMAND wave-audioman-compat-test)
    add_test(NAME recording-audioman-compat COMMAND recording-audioman-compat-test)
  endif()
  if(STUDIO_NATIVE_WINDOWS)
    add_test(NAME ellipse-geometry-gdi COMMAND ellipse-geometry-gdi-test)
    add_test(NAME dialog-resource-win COMMAND dialog-resource-win-test)
    add_test(NAME midi-stream-host COMMAND midi-stream-host-test)
    set_tests_properties(midi-stream-host PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
    add_test(NAME palette-gdi COMMAND palette-gdi-test)
    add_test(NAME gdi-adapter COMMAND gdi-adapter-test)
    add_test(NAME packed-shape-gdi COMMAND packed-shape-gdi-test)
    add_test(NAME pixel-canvas-gdi COMMAND pixel-canvas-gdi-test)
    add_test(NAME pixel-canvas-indexed-gdi COMMAND pixel-canvas-indexed-gdi-test)
    add_test(NAME masked-transfer-gdi COMMAND masked-transfer-gdi-test)
    add_test(NAME indexed-copy-gdi COMMAND indexed-copy-gdi-test)
    add_test(NAME packed-copy-gdi COMMAND packed-copy-gdi-test)
    add_test(NAME pixel-storage-gdi COMMAND pixel-storage-gdi-test)
    add_test(NAME font-gdi COMMAND font-gdi-test)
  endif()
  add_test(NAME text-services COMMAND text-services-test)
  add_test(NAME text-portable COMMAND text-portable-test)
  add_test(NAME string-compat COMMAND string-compat-test)
  add_test(NAME filename COMMAND filename-test)
  add_test(NAME portfolio-data COMMAND portfolio-data-test ${PROJECT_BINARY_DIR}/portfolio-data-fixture)
  add_test(NAME file-browser COMMAND file-browser-test ${PROJECT_BINARY_DIR}/file-browser-fixture)
  add_test(NAME dib-frame COMMAND dib-frame-test)
  add_test(NAME avi-movie COMMAND avi-movie-test)
  if(TARGET avi-corpus-win-test AND EXISTS "${PROJECT_SOURCE_DIR}/content-files/proj1.avi")
    add_test(NAME avi-corpus-win COMMAND avi-corpus-win-test "${PROJECT_SOURCE_DIR}/content-files")
  endif()
  add_test(NAME graphics-api COMMAND graphics-api-test)
  if(TARGET dib-frame-gdi-test)
    add_test(NAME dib-frame-gdi COMMAND dib-frame-gdi-test)
    add_test(NAME video-stream-host COMMAND video-stream-host-test ${PROJECT_BINARY_DIR}/video-host-fixture.avi)
  endif()
  add_test(NAME file-services COMMAND file-services-test)
  add_test(NAME user-services COMMAND user-services-test)
  add_test(NAME settings-store COMMAND settings-store-test)
  if(TARGET settings-ini-test)
    add_test(NAME settings-ini COMMAND settings-ini-test)
    set_tests_properties(settings-ini PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(TARGET settings-store-win-test)
    add_test(NAME settings-store-win COMMAND settings-store-win-test)
  endif()
  if(TARGET settings-store-posix-test)
    add_test(NAME settings-store-posix COMMAND settings-store-posix-test)
  endif()
  add_test(NAME platform-services COMMAND platform-services-test)
  add_test(NAME three-mm-dump
    COMMAND ${CMAKE_COMMAND} -DDUMP_EXE=$<TARGET_FILE:three-mm>
      -DFIXTURE_EXE=$<TARGET_FILE:three-mm-fixture>
      -DTEST_DIR=${PROJECT_BINARY_DIR}/three-mm-test
      -P ${PROJECT_SOURCE_DIR}/tests/check_three_mm.cmake)
  add_test(NAME geometry COMMAND geometry-test)
  if(TARGET dialog-sdl-test)
    add_dependencies(core-checks file-dialog-sdl-test)
    add_test(NAME file-dialog-sdl COMMAND file-dialog-sdl-test ${PROJECT_BINARY_DIR}/file-dialog-fixture ${PROJECT_BINARY_DIR}/file-dialog.bmp)
    set_tests_properties(file-dialog-sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_dependencies(core-checks dialog-sdl-test)
    add_test(NAME dialog-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:dialog-sdl-test> ${PROJECT_BINARY_DIR}/dialog-sdl-test.bmp)
  endif()
  if(STUDIO_SDL3)
    add_dependencies(core-checks dialog-controller-sdl-test)
    add_test(NAME dialog-controller-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:dialog-controller-sdl-test>)
    add_dependencies(core-checks recording-sdl-test movie-recording-test)
    add_test(NAME recording-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:recording-sdl-test>)
    add_test(NAME movie-recording COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:movie-recording-test>)
    add_dependencies(core-checks sdl-host-test clipboard-sdl-test cursor-sdl-test scroll-sdl-test focus-sdl-test input-sdl-test events-sdl-test audio-sdl-test sound-sdl-test wave-import-sdl-test movie-sound-import-test)
    add_dependencies(core-checks window-lifecycle-sdl-test)
    add_test(NAME window-lifecycle-sdl COMMAND window-lifecycle-sdl-test)
    set_tests_properties(window-lifecycle-sdl PROPERTIES ENVIRONMENT "SDL_RENDER_DRIVER=software")
    add_test(NAME movie-sound-import COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:movie-sound-import-test>)
    add_test(NAME wave-import-sdl COMMAND wave-import-sdl-test)
    add_test(NAME audio-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:audio-sdl-test>)
    add_dependencies(core-checks compressed-wave-test)
    add_test(NAME compressed-wave COMMAND compressed-wave-test "${PROJECT_SOURCE_DIR}/tests/fixtures/audio")
    add_test(NAME sound-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy $<TARGET_FILE:sound-sdl-test>)
    add_test(NAME sdl-host COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:sdl-host-test> ${PROJECT_BINARY_DIR}/sdl-host-test.bmp)
    add_test(NAME clipboard-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:clipboard-sdl-test>)
    add_test(NAME cursor-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:cursor-sdl-test> ${PROJECT_BINARY_DIR}/cursor-sdl-test.bmp)
    add_test(NAME scroll-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=windows SDL_RENDER_DRIVER=software
      $<TARGET_FILE:scroll-sdl-test>)
    add_test(NAME focus-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=windows SDL_RENDER_DRIVER=software
      $<TARGET_FILE:focus-sdl-test>)
    add_test(NAME input-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:input-sdl-test>)
    add_test(NAME events-sdl COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
      $<TARGET_FILE:events-sdl-test>)
  endif()
  add_test(NAME mbmp-pixels COMMAND mbmp-pixels-test)
  add_test(NAME codec COMMAND codec-test)
  add_test(NAME brender-diagnostics
    COMMAND ${CMAKE_COMMAND} -DTEST_EXE=$<TARGET_FILE:brender-diagnostic-test>
      -P ${PROJECT_SOURCE_DIR}/tests/check_diagnostics.cmake)
  if(WIN32)
    add_test(NAME movie-save-load COMMAND movie-save-load-test)
  endif()
  add_custom_target(library-checks DEPENDS core-checks)
  add_dependencies(library-checks movie-save-load-test)
  if(TARGET picture-portable-test)
    add_dependencies(library-checks picture-portable-test)
  endif()
  if(TARGET filename-dialog-sdl-test)
    add_dependencies(library-checks filename-dialog-sdl-test)
  endif()
  if(STUDIO_NATIVE_WINDOWS)
    add_dependencies(library-checks packed-shape-gdi-test midi-stream-host-test palette-gdi-test movie-save-load-test movie-chomp gdi-adapter-test dialog-resource-win-test ellipse-geometry-gdi-test pixel-canvas-gdi-test pixel-canvas-indexed-gdi-test masked-transfer-gdi-test indexed-copy-gdi-test packed-copy-gdi-test pixel-storage-gdi-test font-gdi-test)
  endif()
  if(TARGET core-native-interop-check)
    add_dependencies(library-checks core-native-interop-check)
  endif()
  if(WIN32 AND STUDIO_SDL3 AND BUILD_STUDIO)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      if(STUDIO_NATIVE_WINDOWS)
        add_test(NAME studio-dialog-bundle COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tools/embed-dialog-resources.py" --check
          "${PROJECT_BINARY_DIR}/CMakeFiles/studio.dir/src/studio/utest.rc.res"
          "${PROJECT_SOURCE_DIR}/src/studio/dialog_resources.cpp")
      endif()
      add_dependencies(library-checks studio)
    endif()
  endif()
  foreach(core IN ITEMS kauai-core brender-core engine-core)
    add_test(NAME ${core}-link COMMAND ${core}-link-test)
  endforeach()
  add_test(NAME preview-background-mesh COMMAND preview-background-mesh-test)
  set(TEST_CONTENT_DIR "${PROJECT_SOURCE_DIR}/content-files" CACHE PATH "Original 3DMM content fixtures")
  set(TEST_MAIN_CHK "${PROJECT_SOURCE_DIR}/dist/Microsoft Kids/3D Movie Maker/3dmovie.chk"
    CACHE FILEPATH "Main chunky file with shade table for the actor render test")
  if(EXISTS "${TEST_CONTENT_DIR}/tmpls.3cn" AND EXISTS "${TEST_MAIN_CHK}")
    add_test(NAME actor-render COMMAND actor-render-test "${TEST_MAIN_CHK}"
      "${TEST_CONTENT_DIR}/tmpls.3cn" 0x2010 "${PROJECT_BINARY_DIR}/actor-test.pgm")
    add_test(NAME actor-render-spare-materials COMMAND actor-render-test "${TEST_MAIN_CHK}"
      "${TEST_CONTENT_DIR}/tmpls.3cn" 0x1021 "${PROJECT_BINARY_DIR}/actor-spare-materials.pgm" 15)
  endif()
  if(EXISTS "${TEST_CONTENT_DIR}/tmpls.3cn" AND EXISTS "${TEST_CONTENT_DIR}/bkgds.3cn"
      AND EXISTS "${TEST_CONTENT_DIR}/tdfs.3cn" AND EXISTS "${TEST_CONTENT_DIR}/mtrls.3cn"
      AND EXISTS "${TEST_MAIN_CHK}")
    set(preview_gui_args)
    if(BUILD_INSPECTOR)
      list(APPEND preview_gui_args -DGUI_EXE=$<TARGET_FILE:three-mm-inspector>)
    endif()
    add_test(NAME asset-previews COMMAND ${CMAKE_COMMAND}
      -DAPI_EXE=$<TARGET_FILE:preview-api-test>
      -DSCENE_EXE=$<TARGET_FILE:preview-scene-test>
      -DFIXTURE_EXE=$<TARGET_FILE:preview-scene-fixture>
      -DBACKGROUND_EXE=$<TARGET_FILE:preview-background-test>
      -DMAIN_CHK=${TEST_MAIN_CHK} -DCONTENT_DIR=${TEST_CONTENT_DIR}
      ${preview_gui_args}
      -DTEST_DIR=${PROJECT_BINARY_DIR}/preview-test
      -P ${PROJECT_SOURCE_DIR}/tests/check_previews.cmake)
    if(BUILD_STUDIO AND STUDIO_SDL3 AND NOT STUDIO_NATIVE_WINDOWS
        AND (STUDIO_TEST_TOOLS OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        get_filename_component(studio_test_runtime "${TEST_MAIN_CHK}" DIRECTORY)
        add_dependencies(library-checks studio preview-scene-fixture)
        add_test(NAME studio-movie-workflow COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tests/studio_movie_workflow.py"
          $<TARGET_FILE:studio> "${studio_test_runtime}" "${PROJECT_BINARY_DIR}/movie-workflow"
          --fixture-exe $<TARGET_FILE:preview-scene-fixture> --content-dir "${TEST_CONTENT_DIR}")
        set_tests_properties(studio-movie-workflow PROPERTIES TIMEOUT 180
          ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software;PYTHONDONTWRITEBYTECODE=1")
        add_test(NAME studio-building-workflow COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tests/studio_building_workflow.py"
          $<TARGET_FILE:studio> "${studio_test_runtime}" "${PROJECT_SOURCE_DIR}/tmp/studio-building-workflow")
        set_tests_properties(studio-building-workflow PROPERTIES TIMEOUT 120
          ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software;PYTHONDONTWRITEBYTECODE=1")
      endif()
    endif()
  endif()
  set(TEST_STUDIO_ISO "${PROJECT_SOURCE_DIR}/dist/3DMOVIE100.iso" CACHE FILEPATH "Retail ISO for standalone Studio checks")
  if(BUILD_STUDIO AND STUDIO_SDL3 AND NOT STUDIO_NATIVE_WINDOWS AND EXISTS "${TEST_STUDIO_ISO}"
      AND (STUDIO_TEST_TOOLS OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_dependencies(library-checks studio)
      add_test(NAME studio-iso-workflow COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/studio_iso_workflow.py" $<TARGET_FILE:studio>
        "${TEST_STUDIO_ISO}" "${PROJECT_SOURCE_DIR}/tmp/studio-iso-workflow")
      set_tests_properties(studio-iso-workflow PROPERTIES TIMEOUT 180
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software;PYTHONDONTWRITEBYTECODE=1")
      add_test(NAME studio-authoring-workflow COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/studio_authoring_workflow.py" $<TARGET_FILE:studio>
        "${TEST_STUDIO_ISO}" "${PROJECT_SOURCE_DIR}/tmp/studio-authoring-workflow")
      set_tests_properties(studio-authoring-workflow PROPERTIES TIMEOUT 120
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
      add_test(NAME studio-rig-workflow COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/studio_rig_workflow.py" $<TARGET_FILE:studio>
        "${TEST_STUDIO_ISO}" "${PROJECT_SOURCE_DIR}/tmp/studio-rig-workflow")
      set_tests_properties(studio-rig-workflow PROPERTIES TIMEOUT 120
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
      add_dependencies(library-checks preview-scene-fixture)
      add_test(NAME studio-movie-only-workflow COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/studio_movie_only_workflow.py" $<TARGET_FILE:studio>
        $<TARGET_FILE:preview-scene-fixture> "${TEST_STUDIO_ISO}"
        "${PROJECT_SOURCE_DIR}/tmp/movie-only-workflow")
      set_tests_properties(studio-movie-only-workflow PROPERTIES TIMEOUT 120
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
      add_test(NAME studio-options-workflow COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/studio_options_workflow.py" $<TARGET_FILE:studio>
        $<TARGET_FILE:preview-scene-fixture> "${TEST_STUDIO_ISO}"
        "${PROJECT_SOURCE_DIR}/tmp/studio-options-workflow")
      set_tests_properties(studio-options-workflow PROPERTIES TIMEOUT 180
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
      if(STUDIO_TEST_TOOLS AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        add_dependencies(library-checks preview-scene-fixture)
        add_test(NAME studio-batch-workflow COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tests/studio_batch_workflow.py" $<TARGET_FILE:studio>
          $<TARGET_FILE:preview-scene-fixture> "${TEST_STUDIO_ISO}"
          "${PROJECT_SOURCE_DIR}/tmp/studio-batch-workflow")
        set_tests_properties(studio-batch-workflow PROPERTIES TIMEOUT 180
          ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
      endif()
    endif()
  endif()
endif()
