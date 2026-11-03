include_guard(GLOBAL)
include(FetchContent)
function(kauai_add_freetype)
  set(BUILD_SHARED_LIBS OFF)
  set(SKIP_INSTALL_ALL ON)
  foreach(component ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${component} ON CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(freetype
    URL https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-14-3
    URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261)
  FetchContent_MakeAvailable(freetype)
endfunction()
kauai_add_freetype()
