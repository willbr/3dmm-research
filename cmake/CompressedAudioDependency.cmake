include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(libgsm
  URL https://www.quut.com/gsm/gsm-1.0.24.tar.gz
  URL_HASH SHA256=a3c40c6471928383f4abfcb2e8f24012a1f562be2f17b8d672145d5986681a92)
FetchContent_Declare(dr_libs
  URL https://codeload.github.com/mackron/dr_libs/tar.gz/35b5c392cc15456736c440a68dbe073178bbffe0
  URL_HASH SHA256=d46fa0a831a65b16fcf2b169e6b739c66536437ee78049893788582eb93e39de
  SOURCE_SUBDIR kauai-source-only)
FetchContent_MakeAvailable(libgsm dr_libs)

# The GSM algorithm requires 32-bit arithmetic on LLP64 and LP64 alike.
# Keep the upstream tree untouched and override its private header in the build.
set(gsm_generated "${PROJECT_BINARY_DIR}/generated/libgsm")
file(MAKE_DIRECTORY "${gsm_generated}")
file(READ "${libgsm_SOURCE_DIR}/inc/private.h" gsm_private)
string(REGEX REPLACE "typedef long[ \t]+longword;" "typedef int32_t longword;" gsm_private "${gsm_private}")
string(REGEX REPLACE "typedef unsigned long[ \t]+ulongword;" "typedef uint32_t ulongword;" gsm_private "${gsm_private}")
file(CONFIGURE OUTPUT "${gsm_generated}/private.h" CONTENT "#include <stdint.h>\n@gsm_private@" @ONLY)
set(gsm_sources)
foreach(source add decode long_term rpe gsm_destroy gsm_decode gsm_create gsm_option short_term table)
  list(APPEND gsm_sources "${libgsm_SOURCE_DIR}/src/${source}.c")
endforeach()
add_library(kauai-gsm STATIC ${gsm_sources})
target_include_directories(kauai-gsm PRIVATE "${gsm_generated}" PUBLIC "${libgsm_SOURCE_DIR}/inc")
target_compile_definitions(kauai-gsm PRIVATE WAV49 SASR HAS_STDLIB_H HAS_STRING_H NDEBUG)
set_target_properties(kauai-gsm PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

file(READ "${libgsm_SOURCE_DIR}/COPYRIGHT" gsm_license)
file(READ "${dr_libs_SOURCE_DIR}/LICENSE" mp3_license)
string(APPEND mp3_license "\nIncludes minimp3 (https://github.com/lieff/minimp3), dedicated to the public domain under CC0.\n")
configure_file("${PROJECT_SOURCE_DIR}/cmake/compressed_audio_licenses.h.in"
  "${PROJECT_BINARY_DIR}/generated/audio-codecs/compressed_audio_licenses.h" @ONLY)
add_library(kauai-compressed-wave STATIC "${PROJECT_SOURCE_DIR}/kauai/src/compressed_wave.cpp"
  "${PROJECT_SOURCE_DIR}/kauai/src/mp3_decoder.c")
target_include_directories(kauai-compressed-wave PUBLIC "${PROJECT_SOURCE_DIR}/kauai/src"
  PRIVATE "${dr_libs_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/generated/audio-codecs")
target_link_libraries(kauai-compressed-wave PRIVATE kauai-gsm)
set_target_properties(kauai-compressed-wave PROPERTIES POSITION_INDEPENDENT_CODE ON)
