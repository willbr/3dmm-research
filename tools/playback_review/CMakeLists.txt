cmake_minimum_required(VERSION 3.22)
project(playback-review LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(SDL3Dependency)
include(FetchContent)
FetchContent_Declare(review_cimgui
  GIT_REPOSITORY https://github.com/cimgui/cimgui.git
  GIT_TAG 2a6a93fccd10f275ee9206167dc9698f6f00fac1
  GIT_SUBMODULES imgui
  SOURCE_SUBDIR unused)
FetchContent_Declare(review_sqlite
  URL https://sqlite.org/2025/sqlite-amalgamation-3500400.zip
  URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)
FetchContent_MakeAvailable(review_cimgui review_sqlite)
set(gui "${review_cimgui_SOURCE_DIR}")
add_library(review-imgui STATIC "${gui}/cimgui.cpp"
  "${gui}/imgui/imgui.cpp" "${gui}/imgui/imgui_draw.cpp"
  "${gui}/imgui/imgui_tables.cpp" "${gui}/imgui/imgui_widgets.cpp" "${gui}/imgui/imgui_demo.cpp"
  "${gui}/imgui/backends/imgui_impl_sdl3.cpp"
  "${gui}/imgui/backends/imgui_impl_sdlrenderer3.cpp" backend.cpp)
target_compile_features(review-imgui PRIVATE cxx_std_11)
target_compile_definitions(review-imgui PUBLIC CIMGUI_NO_EXPORT)
target_include_directories(review-imgui SYSTEM PUBLIC "${gui}" "${gui}/imgui")
target_link_libraries(review-imgui PUBLIC SDL3::SDL3-static)
if(WIN32)
  target_link_libraries(review-imgui PRIVATE comdlg32)
endif()
add_library(review-sqlite STATIC "${review_sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(review-sqlite SYSTEM PUBLIC "${review_sqlite_SOURCE_DIR}")
add_library(review-store STATIC store.c)
target_link_libraries(review-store PUBLIC review-sqlite)
add_executable(playback-review main.c)
target_link_libraries(playback-review PRIVATE review-store review-imgui)
target_include_directories(playback-review SYSTEM PRIVATE
  "$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(review-store PRIVATE -pedantic-errors -Werror=declaration-after-statement)
  target_compile_options(playback-review PRIVATE -pedantic-errors -Werror=declaration-after-statement)
endif()
enable_testing()
add_executable(review-store-test store_test.c)
target_link_libraries(review-store-test PRIVATE review-store)
add_test(NAME review-store COMMAND review-store-test)
find_program(REVIEW_C89_COMPILER NAMES clang gcc)
if(REVIEW_C89_COMPILER)
  add_test(NAME review-c89 COMMAND "${REVIEW_C89_COMPILER}" -std=c89 -pedantic-errors
    -Werror=declaration-after-statement -D_CRT_SECURE_NO_WARNINGS -fsyntax-only
    -isystem "${gui}" -isystem "${SDL3_SOURCE_DIR}/include"
    -isystem "${review_sqlite_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c" "${CMAKE_CURRENT_SOURCE_DIR}/store.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/store_test.c")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/licenses")
configure_file("${gui}/LICENSE" licenses/cimgui-LICENSE.txt COPYONLY)
configure_file("${gui}/imgui/LICENSE.txt" licenses/imgui-LICENSE.txt COPYONLY)
configure_file("${SDL3_SOURCE_DIR}/LICENSE.txt" licenses/SDL-LICENSE.txt COPYONLY)
