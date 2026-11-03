include_guard(GLOBAL)
include(FetchContent)
FetchContent_Declare(tinysoundfont
  URL https://codeload.github.com/schellingb/TinySoundFont/tar.gz/853a0a171759f1ddba0de1442133a75912bbeffa
  URL_HASH SHA256=76c356df524f71f2a34f67fa0cb2cb946036fe7a65d2930a849569c5e742fa57)
FetchContent_GetProperties(tinysoundfont)
if(NOT tinysoundfont_POPULATED)
  FetchContent_Populate(tinysoundfont)
endif()
function(kauai_copy_midi_assets target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets" "$<TARGET_FILE_DIR:${target}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${KAUAI_MIDI_BANK_FILE}" "$<TARGET_FILE_DIR:${target}>/assets/GeneralUserGS.sf2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${PROJECT_SOURCE_DIR}/docs/licenses/tinysoundfont-LICENSE.txt"
      "${PROJECT_SOURCE_DIR}/docs/licenses/tinysoundfont-PATCHES.md"
      "${PROJECT_SOURCE_DIR}/docs/licenses/generaluser-gs-LICENSE.txt"
      "$<TARGET_FILE_DIR:${target}>/licenses"
    VERBATIM)
endfunction()
file(READ "${tinysoundfont_SOURCE_DIR}/tsf.h" tsf_header)
# A negative coarse sample offset is legal SF2 data. Signed left shift is
# undefined in C++14; multiply in the safe signed range, then wrap unsigned.
string(REPLACE "amount->shortAmount<<15" "(unsigned int)((int)amount->shortAmount * 32768)" tsf_header "${tsf_header}")
set(KAUAI_TSF_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/tinysoundfont")
file(MAKE_DIRECTORY "${KAUAI_TSF_INCLUDE_DIR}")
file(WRITE "${KAUAI_TSF_INCLUDE_DIR}/tsf.h" "${tsf_header}")
set(KAUAI_MIDI_BANK_FILE "" CACHE FILEPATH "General MIDI SoundFont for software MIDI playback")
if(NOT KAUAI_MIDI_BANK_FILE)
  set(KAUAI_MIDI_BANK_FILE "${PROJECT_BINARY_DIR}/assets/GeneralUserGS.sf2")
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/assets")
  # Immutable mirror of the legacy SoftSynth bank; license accompanies the app.
  file(DOWNLOAD
    "https://raw.githubusercontent.com/CoderLine/alphaSynth/7514e992fb8bf9840067ad447a4932f885fe82e2/Samples/Data/GeneralUser%20GS%20SoftSynth%20v1.44.sf2"
    "${KAUAI_MIDI_BANK_FILE}"
    EXPECTED_HASH SHA256=00725296486f652ba42bdbf2098254762ca80239353d04b55a27454fc8edb8a8
    TLS_VERIFY ON)
endif()
