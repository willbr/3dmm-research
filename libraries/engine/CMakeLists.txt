cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ThreeMMEngine LANGUAGES C)
  include(CTest)
endif()

add_library(three-mm-engine-c89 STATIC src/playback.c src/scroll.c src/read_stream.c src/movie_data.c src/chunky_file.c src/string_table.c src/text.c src/collection_storage.c src/collections.c src/collection_headers.c src/data_stream.c src/codec.c)
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
get_filename_component(engine_movie_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_movie_data_test.c" ABSOLUTE)
add_executable(engine-movie-data-test EXCLUDE_FROM_ALL "${engine_movie_test_source}")
target_link_libraries(engine-movie-data-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-movie-data-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-movie-data COMMAND engine-movie-data-test)
get_filename_component(engine_chunky_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_chunky_test.c" ABSOLUTE)
add_executable(engine-chunky-test EXCLUDE_FROM_ALL "${engine_chunky_test_source}")
target_link_libraries(engine-chunky-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-chunky-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-chunky COMMAND engine-chunky-test)
get_filename_component(engine_strings_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_strings_test.c" ABSOLUTE)
add_executable(engine-strings-test EXCLUDE_FROM_ALL "${engine_strings_test_source}")
target_link_libraries(engine-strings-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-strings-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-strings COMMAND engine-strings-test)
get_filename_component(engine_storage_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_storage_test.c" ABSOLUTE)
add_executable(engine-storage-test EXCLUDE_FROM_ALL "${engine_storage_test_source}")
target_link_libraries(engine-storage-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-storage-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-storage COMMAND engine-storage-test)
get_filename_component(engine_collections_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_collections_test.c" ABSOLUTE)
add_executable(engine-collections-test EXCLUDE_FROM_ALL "${engine_collections_test_source}")
target_link_libraries(engine-collections-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-collections-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-collections COMMAND engine-collections-test)
get_filename_component(engine_data_stream_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_data_stream_test.c" ABSOLUTE)
add_executable(engine-data-stream-test EXCLUDE_FROM_ALL "${engine_data_stream_test_source}")
target_link_libraries(engine-data-stream-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-data-stream-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-data-stream COMMAND engine-data-stream-test)
find_program(ENGINE_C89_COMPILER NAMES clang gcc)
get_filename_component(engine_codec_test_source "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/engine_codec_test.c" ABSOLUTE)
add_executable(engine-codec-test EXCLUDE_FROM_ALL "${engine_codec_test_source}")
target_link_libraries(engine-codec-test PRIVATE three-mm-engine-c89)
set_target_properties(engine-codec-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME engine-codec COMMAND engine-codec-test)
if(ENGINE_C89_COMPILER)
  add_test(NAME engine-c89-syntax COMMAND "${ENGINE_C89_COMPILER}"
    -std=c89 -pedantic-errors -Werror=declaration-after-statement -Wall -Wextra -Werror -fsyntax-only
    "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/playback.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scroll.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/read_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/movie_data.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chunky_file.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/string_table.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collection_storage.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collections.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/collection_headers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/data_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/codec.c"
    "${engine_test_source}" "${engine_movie_test_source}" "${engine_chunky_test_source}" "${engine_strings_test_source}" "${engine_storage_test_source}" "${engine_collections_test_source}" "${engine_data_stream_test_source}" "${engine_codec_test_source}")
endif()
