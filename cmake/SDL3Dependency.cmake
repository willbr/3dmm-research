# Shared vendored SDL dependency for Studio, Marcus and the inspector.
include_guard(GLOBAL)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_LIBC ON CACHE BOOL "" FORCE)
# The archive has no Git metadata; report SDL's revision, not the parent project's.
set(SDL_REVISION "SDL-3.4.8-d9d5536" CACHE STRING "Vendored SDL revision" FORCE)
# Use the checked-in source even when an old FetchContent cache override exists.
get_filename_component(SDL3_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor/SDL3" ABSOLUTE)
set(SDL3_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/sdl3-build")
add_subdirectory("${SDL3_SOURCE_DIR}" "${SDL3_BINARY_DIR}" EXCLUDE_FROM_ALL)
