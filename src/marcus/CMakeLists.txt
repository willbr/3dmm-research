include(SDL3Dependency)
include(FreeTypeDependency)

add_library(marcus-player STATIC player.cpp)
target_include_directories(marcus-player PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(marcus-player PRIVATE ThreeMM::Preview)

add_executable(marcus WIN32 main.c ui.c settings.c)
set_target_properties(marcus PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(marcus PRIVATE MARCUS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(marcus PRIVATE marcus-player SDL3::SDL3-static freetype)
# SDL's headers use platform integer/bool extensions. Keep diagnostics on our
# C89 sources strict while treating external declarations as system headers.
target_include_directories(marcus SYSTEM PRIVATE
  "$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>")
if(NOT WIN32)
  target_link_libraries(marcus PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(marcus PRIVATE -pedantic-errors -Werror=declaration-after-statement)
endif()
if(BUILD_TESTING)
  add_executable(marcus-settings-test EXCLUDE_FROM_ALL ../../tests/marcus_settings_test.c settings.c)
  set_target_properties(marcus-settings-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_include_directories(marcus-settings-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_include_directories(marcus-settings-test SYSTEM PRIVATE "$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>")
  target_link_libraries(marcus-settings-test PRIVATE SDL3::SDL3-static)
  add_test(NAME marcus-shortcuts COMMAND marcus-settings-test "${CMAKE_CURRENT_BINARY_DIR}/shortcuts-test.cfg")
  find_program(MARCUS_C89_COMPILER NAMES clang gcc)
  if(MARCUS_C89_COMPILER)
    add_test(NAME marcus-c89 COMMAND ${CMAKE_COMMAND}
      "-DCOMPILER=${MARCUS_C89_COMPILER}"
      "-DSDL_INCLUDES=$<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>"
      "-DFREETYPE_INCLUDES=$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>"
      -DSOURCE=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tests/check_marcus_c89.cmake)
  endif()
  add_executable(marcus-player-test EXCLUDE_FROM_ALL ../../tests/marcus_player_test.c)
  set_target_properties(marcus-player-test PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(marcus-player-test PRIVATE marcus-player)
  add_test(NAME marcus-empty COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software $<TARGET_FILE:marcus> --smoke-test)
  if(EXISTS "${PROJECT_SOURCE_DIR}/content-files/bkgds.3cn")
    set(marcus_resources "${PROJECT_SOURCE_DIR}/build/studio-portable-x64/3dmovie/utest.chk"
      CACHE FILEPATH "3dmovie.chk or built utest.chk for Marcus integration tests")
    if(EXISTS "${marcus_resources}")
      add_test(NAME marcus-player COMMAND ${CMAKE_COMMAND}
        -DPLAYER_EXE=$<TARGET_FILE:marcus> -DAPI_EXE=$<TARGET_FILE:marcus-player-test>
        -DFIXTURE_EXE=$<TARGET_FILE:preview-scene-fixture>
        -DRESOURCES=${marcus_resources} -DCONTENT=${PROJECT_SOURCE_DIR}/content-files
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${PROJECT_SOURCE_DIR}/tests/check_marcus.cmake)
    endif()
  endif()
endif()
