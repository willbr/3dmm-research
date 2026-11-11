cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ThreeMMEngine LANGUAGES C)
  include(CTest)
endif()

add_library(three-mm-engine-c89 STATIC src/playback.c src/scroll.c)
target_include_directories(three-mm-engine-c89 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(three-mm-engine-c89 PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(three-mm-engine-c89 PRIVATE -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra)
endif()

get_filename_component(engine_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_c89_test.c" ABSOLUTE)
add_executable(engine-c89-test EXCLUDE_FROM_ALL "${engine_test_source}")
target_link_libraries(engine-c89-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-c89-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-c89 COMMAND engine-c89-test)
find_program(ENGINE_C89_COMPILER NAMES clang gcc)
if(ENGINE_C89_COMPILER)
  add_test(NAME engine-c89-syntax COMMAND "${ENGINE_C89_COMPILER}"
    -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror -fsyntax-only
    "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/playback.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scroll.c"
    "${engine_test_source}")
endif()
