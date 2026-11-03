cmake_minimum_required(VERSION 3.22)
project(CoreConsumer LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(BUILD_INSPECTOR OFF CACHE BOOL "" FORCE)
set(BUILD_PACKAGES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(../.. framework EXCLUDE_FROM_ALL)
add_executable(archive-consumer ../archive_api_test.cpp ../archive_api_include_order.cpp)
target_link_libraries(archive-consumer PRIVATE ThreeMM::Archive)
add_executable(preview-consumer ../preview_api_test.cpp ../preview_api_include_order.cpp)
target_link_libraries(preview-consumer PRIVATE ThreeMM::Preview)
# This target is outside the framework's directory: it must receive all
# required definitions and include paths from its library dependencies.
add_executable(core-consumer ../core_link_test.cpp)
target_compile_definitions(core-consumer PRIVATE TEST_ENGINE_CORE)
target_link_libraries(core-consumer PRIVATE engine-core)
if(MSVC)
  target_link_options(core-consumer PRIVATE /OPT:NOREF
    /WHOLEARCHIVE:$<TARGET_FILE:engine-core>
    /WHOLEARCHIVE:$<TARGET_FILE:brender-core>
    /WHOLEARCHIVE:$<TARGET_FILE:kauai-core>)
elseif(APPLE)
  foreach(core IN ITEMS engine-core brender-core kauai-core)
    target_link_options(core-consumer PRIVATE "LINKER:-force_load,$<TARGET_FILE:${core}>")
  endforeach()
endif()
