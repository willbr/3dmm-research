include_guard(GLOBAL)

set(CHOMP_EXECUTABLE "" CACHE FILEPATH "Host chomp executable (required when cross-compiling without an emulator)")

# any values after ${target} are treated as sources
function(target_chomp_sources target)
  if(CHOMP_EXECUTABLE)
    if(NOT EXISTS "${CHOMP_EXECUTABLE}")
      message(FATAL_ERROR "CHOMP_EXECUTABLE does not exist: ${CHOMP_EXECUTABLE}")
    endif()
    set(chomp-command "${CHOMP_EXECUTABLE}")
  elseif(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(FATAL_ERROR "Cross-compiling Studio content requires CHOMP_EXECUTABLE pointing to a host-built chomp, or CMAKE_CROSSCOMPILING_EMULATOR")
  else()
    set(chomp-command chomp)
  endif()
  set(include-directories $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
  set(compile-definitions $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>)
  string(CONCAT include-directories $<
    $<BOOL:${include-directories}>:
    -I$<JOIN:$<REMOVE_DUPLICATES:${include-directories}>,$<SEMICOLON>-I>
  >)
  string(CONCAT compile-definitions $<
    $<BOOL:${compile-definitions}>:
    -D$<JOIN:$<REMOVE_DUPLICATES:${compile-definitions}>,$<SEMICOLON>-D>
  >)
  foreach(source IN LISTS ARGN)
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    cmake_path(GET source FILENAME filename)
    cmake_path(REPLACE_EXTENSION filename LAST_ONLY ".chk" OUTPUT_VARIABLE output)
    # Because of how files are laid out, we need to do the following line twice
    # but operate on the resulting path
    cmake_path(GET source PARENT_PATH parent)
    set_property(TARGET ${target} APPEND PROPERTY INCLUDE_DIRECTORIES "${parent}")
    cmake_path(GET parent PARENT_PATH parent)
    set_property(TARGET ${target} APPEND PROPERTY INCLUDE_DIRECTORIES "${parent}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/3dmovie/${output}")
    set(processed "${CMAKE_CURRENT_BINARY_DIR}/chomp/${target}/${filename}.i")
    if(MSVC)
      set(preprocess-options /nologo /P /TP "/Fi${processed}.raw")
    else()
      # .cht is preprocessor input, not a compiler-recognized C++ suffix.
      set(preprocess-options -E -x c++ -o "${processed}.raw")
    endif()
    add_custom_command(
      OUTPUT "${processed}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/chomp/${target}"
      COMMAND "${CMAKE_CXX_COMPILER}"
        ${preprocess-options}
        "${include-directories}"
        "${compile-definitions}"
        "${source}"
      COMMAND "${CMAKE_COMMAND}" "-DINPUT=${processed}.raw" "-DOUTPUT=${processed}" "-DDEPFILE=${processed}.d"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/NormalizeChomp.cmake"
      DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/NormalizeChomp.cmake"
      BYPRODUCTS "${processed}.raw"
      DEPFILE "${processed}.d"
      MAIN_DEPENDENCY "${source}"
      COMMENT "Preprocessing ${source}"
      COMMAND_EXPAND_LISTS
      USES_TERMINAL
      VERBATIM)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/3dmovie"
      COMMAND ${chomp-command} -c "${processed}" "${output}"
      DEPENDS ${chomp-command}
      COMMENT "Chompin' ${processed}"
      MAIN_DEPENDENCY "${processed}"
      WORKING_DIRECTORY "${parent}"
      COMMAND_EXPAND_LISTS
      VERBATIM)
    set_property(TARGET "${target}" APPEND PROPERTY CHOMPED_CHUNKS "${output}")
  endforeach()
  add_custom_target(${target}-chomp-chunks
    DEPENDS $<TARGET_PROPERTY:${target},CHOMPED_CHUNKS>)
  add_dependencies(${target} ${target}-chomp-chunks)
endfunction()
