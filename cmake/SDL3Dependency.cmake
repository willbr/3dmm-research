# Shared pinned SDL dependency for the Studio host and optional inspector.
include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_LIBC ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.8
  URL_HASH SHA256=429a9f38483f834da5727a63dd7b5127a7c9d06a16439d7c6de4b7ebbfdb6374)
FetchContent_MakeAvailable(SDL3)
