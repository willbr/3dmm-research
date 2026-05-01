# Resolves BRender::Libraries to the in-tree brender_fw + brender_zb targets
# defined by bren/lib/CMakeLists.txt. The historical 3-way split (BRFMMXR,
# BRFWMXR, BRZBMXR) is preserved as aliases for any consumer that names them
# directly, but they all map to the combined target set.

include(FindPackageHandleStandardArgs)

if (NOT TARGET brender_fw OR NOT TARGET brender_zb)
  message(FATAL_ERROR
    "FindBRender.cmake expects the in-tree brender_fw / brender_zb targets. "
    "Make sure add_subdirectory(bren/lib) runs before find_package(BRender).")
endif()

if (NOT TARGET BRender::Libraries)
  add_library(BRender::Libraries INTERFACE IMPORTED)
  target_link_libraries(BRender::Libraries INTERFACE brender_fw brender_zb)

  # Compat aliases for any consumer that linked to the old per-lib names.
  # In 1.1.2 the fixed-math sources are part of FW (FIXED.C, FIXED386.ASM,
  # MATRIX*.C, SCALAR.C, VECTOR.C, QUAT.C all live in FW/), so BRFMMXR and
  # BRFWMXR both alias to brender_fw.
  add_library(BRender::BRFMMXR ALIAS brender_fw)
  add_library(BRender::BRFWMXR ALIAS brender_fw)
  add_library(BRender::BRZBMXR ALIAS brender_zb)
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_FOUND TRUE)
find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
  REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_FOUND)
