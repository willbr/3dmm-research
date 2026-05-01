include(FindPackageHandleStandardArgs)

# kauai/elib/wins/AUDIOS.lib is the original 1995 precompiled AudioMan -- x86
# only. Skip it on non-x86 builds and let the top-level CMakeLists fall back
# to the source stub at audioman/audioman.cpp.
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
  find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES AUDIOS
    PATHS "${PROJECT_SOURCE_DIR}/kauai/elib/wins"
    NO_DEFAULT_PATH
    NO_PACKAGE_ROOT_PATH
    NO_CMAKE_PATH
    NO_CMAKE_ENVIRONMENT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)

  find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

  if (${CMAKE_FIND_PACKAGE_NAME}_FOUND AND NOT TARGET 3DMMForever::AudioMan)
    add_library(3DMMForever::AudioMan STATIC IMPORTED)
    set_property(TARGET 3DMMForever::AudioMan
      PROPERTY
        IMPORTED_LOCATION "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}")
    # Precompiled AudioMan library does not support SafeSEH
    target_link_options(3DMMForever::AudioMan INTERFACE
        $<$<LINK_LANG_AND_ID:CXX,MSVC>:/SAFESEH:NO>
    )
    mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
  endif()
else()
  # Non-x86: the precompiled x86 lib is unusable. Report not-found so the
  # caller can build the source stub instead.
  set(${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)
endif()
