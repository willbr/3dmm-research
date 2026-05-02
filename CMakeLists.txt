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
option(BUILD_PACKAGES "Build packages for 3DMM" ON)

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
add_compile_definitions(
  $<$<PLATFORM_ID:Windows>:WIN>
  $<$<BOOL:${SOC_X86}>:IN_80386>
  $<$<CONFIG:Debug>:DEBUG>
)

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
  add_library(audioman)
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
target_link_libraries(geometry-test PRIVATE kauai)

# KauaiCodec round-trip tests. On x86 the decoder uses the inline-asm fast
# path generated from kauai/src/kcd2_386.c; on x64 it falls back to the
# portable C path inside _FDecode/_FDecode2. The same encoder (always C)
# feeds both, so this test catches any drift between asm and C decoders
# without requiring 3dmm to launch and grind through chunky-file decoding.
add_executable(codec-test EXCLUDE_FROM_ALL)
target_sources(codec-test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/codec_test.cpp"
)
target_link_libraries(codec-test PRIVATE kauai)

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
if (MSVC)
  # Match brender_zb's /Zp4 packing so struct layouts agree on both x86
  # and x64. Without this the test sees padding that the lib doesn't.
  target_compile_options(bren-rasterizer-test PRIVATE /Zp4 /wd4244)
endif()

add_executable(chomp EXCLUDE_FROM_ALL)
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
    "${PROJECT_SOURCE_DIR}/kauai/src/crf.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/file.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/groups.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/groups2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/lex.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mssio.cpp"
    # region: pure geometric (Region/RegionScanner over rectangles); no
    # UI symbol references and no UI link deps. Verified via grep across
    # the candidate set (commit follows).
    "${PROJECT_SOURCE_DIR}/kauai/src/region.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/scrcom.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/screxe.cpp"
    # spell: spell-checker dictionary; no UI deps. Same audit as region.
    "${PROJECT_SOURCE_DIR}/kauai/src/spell.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/stream.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/util.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilcopy.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilerro.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilglob.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilint.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilmem.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilrnd.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/utilstr.cpp"

    # Windows-specific I/O / memory implementations -- still core, just
    # split per-OS the same way the file-API splits filewin/filemac.
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/filewin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/fniwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/memwin.cpp>

    # Stubs for Visual C++ 2.1 CRT functions
    "${PROJECT_SOURCE_DIR}/kauai/src/stub.cpp"
)

target_compile_definitions(kauai-core PUBLIC
  _LPCVOID_DEFINED
  STRICT)

# kauai-core needs Win32 (for filewin/memwin/fniwin) but NOT the multimedia
# libs (Msacm32/Vfw32/Winmm) -- those are gui-only and stay on the kauai
# target below.
target_link_libraries(kauai-core
  PUBLIC
    $<$<PLATFORM_ID:Windows>:mpr>
    # Winmm: utilint/utilrnd/fniwin use timeGetTime() for system tick
    # readouts. Always linked when the runtime touches them, even in
    # CLI builds.
    $<$<PLATFORM_ID:Windows>:Winmm>)

# --- kauai (gui umbrella): everything else, transitively re-exports core ---
add_library(kauai)
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
    "${PROJECT_SOURCE_DIR}/kauai/src/clok.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/cmd.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/ctl.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/cursor.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/dlg.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/docb.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/frame.rc"
    "${PROJECT_SOURCE_DIR}/kauai/src/gfx.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/gob.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidhelp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidspace.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/kidworld.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mbmp.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mbmpgui.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/midi.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mididev.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/mididev2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/pic.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/rtxt.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/rtxt2.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/scrcomg.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/screxeg.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/sndam.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/sndm.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/text.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/textdoc.cpp"
    "${PROJECT_SOURCE_DIR}/kauai/src/video.cpp"

    # Windows GUI implementations
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/appbwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/dlgwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/gfxwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/menuwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/picwin.cpp>
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/kauai/src/gobwin.cpp>
)

target_compile_definitions(kauai PUBLIC
  _LPCVOID_DEFINED
  STRICT)

target_link_libraries(kauai
  PUBLIC
    kauai-core
    3DMMForever::AudioMan
    $<$<PLATFORM_ID:Windows>:Msacm32>
    $<$<PLATFORM_ID:Windows>:Vfw32>
    $<$<PLATFORM_ID:Windows>:Winmm>
    $<$<PLATFORM_ID:Windows>:mpr>)

add_library(brender)
target_sources(brender
    PRIVATE
    "${PROJECT_SOURCE_DIR}/bren/bwld.cpp"
    "${PROJECT_SOURCE_DIR}/bren/stderr.c"
    "${PROJECT_SOURCE_DIR}/bren/stdfile.c"
    "${PROJECT_SOURCE_DIR}/bren/stdmem.c"
    "${PROJECT_SOURCE_DIR}/bren/tmap.cpp"
    "${PROJECT_SOURCE_DIR}/bren/zbmp.cpp"
)
target_include_directories(brender PUBLIC "${PROJECT_SOURCE_DIR}/BREN/INC")
target_link_libraries(brender
  PUBLIC
    kauai
    BRender::Libraries)

add_library(engine)
target_sources(engine
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/actor.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actredit.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsave.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/bkgd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/body.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/modl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/msnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/mtrl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/scene.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/srec.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagman.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tbox.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdf.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdt.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tmpl.cpp"
)
target_include_directories(engine PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/INC>)
target_link_libraries(engine PUBLIC kauai brender)

# On non-windows WIN32 is a no-op
set(STUDIO_BUILD_STAMP_SRC "${PROJECT_BINARY_DIR}/generated/studio/build_stamp.cpp")
add_custom_target(studio_build_stamp ALL
    BYPRODUCTS "${STUDIO_BUILD_STAMP_SRC}"
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${STUDIO_BUILD_STAMP_SRC}
        -P ${PROJECT_SOURCE_DIR}/cmake/WriteBuildStamp.cmake
    COMMENT "Generating studio build stamp"
    VERBATIM
)
set_source_files_properties("${STUDIO_BUILD_STAMP_SRC}" PROPERTIES GENERATED TRUE)

add_executable(studio WIN32)
target_sources(studio
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/studio/ape.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/browser.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/esl.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/mcpserv.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/mminstal.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/popup.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/portf.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/scnsort.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/splot.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdiobrw.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdioscb.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/studio.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tatr.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tgob.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/utest.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/utest.rc"
    "${PROJECT_SOURCE_DIR}/src/studio/utest.rc2"
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

target_link_libraries(studio PRIVATE engine)
set_property(TARGET studio PROPERTY OUTPUT_NAME 3dmovie)
target_link_options(studio BEFORE PRIVATE $<$<LINK_LANG_AND_ID:CXX,MSVC>:/MANIFESTUAC:NO>)

# Mirror the freshly linked exe into ${CMAKE_INSTALL_PREFIX} (dist/ for the
# default presets) so iterative dev runs of dist/3dmovie.exe pick up rebuilds
# without needing a separate `cmake --install` step. The chunk/data files are
# only refreshed by a real install -- this only copies the exe.
add_custom_command(TARGET studio POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:studio>" "${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:studio>"
  COMMENT "Mirroring 3dmovie.exe to ${CMAKE_INSTALL_PREFIX}"
  VERBATIM)
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
install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,EXCLUDE,.+UTEST[.]chk$>
  DESTINATION "Microsoft Kids/3D Movie Maker")
install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,INCLUDE,.+UTEST[.]chk$>
  RENAME 3dmovie.chk
  DESTINATION "Microsoft Kids/3D Movie Maker")

# Overlay non-chomped data (BLDGDATA.CHK, *.3TH, *.3CN, MSCREATE.DIR, help
# files, fix.dat, ...) from an existing system install so dist/ becomes a
# self-contained, runnable tree. Files we produce ourselves take precedence:
# the overlay copy only fills in destinations that don't already exist.
# Sequenced before the cd3 SAMPLES install below: that step hard-errors when
# cd3/SAMPLES/ is missing locally, and we want the overlay to land regardless.
if(WIN32)
  set(MSKIDS_OVERLAY_DIR "C:/Program Files (x86)/Microsoft Kids"
    CACHE PATH "Existing 3DMM install to overlay non-chomped data from. Set to empty to skip.")
endif()
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

install(FILES $<TARGET_PROPERTY:studio,3DMM_SAMPLES>
  DESTINATION "Microsoft Kids/Users/Melanie")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/cd9/"
  DESTINATION "Microsoft Kids/3D Movie Maker")

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
