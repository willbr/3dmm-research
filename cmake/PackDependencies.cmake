# Pinned, portable readers for local pack manifests and image-fixup ZIP files.
include_guard(GLOBAL)
include(FetchContent)
function(studio_pack_dependencies)
  set(BUILD_EXAMPLES OFF)
  set(BUILD_TESTS OFF)
  set(INSTALL_PROJECT OFF)
  set(JSON_BuildTests OFF)
  set(JSON_Install OFF)
  FetchContent_Declare(pack_miniz
    URL https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.1.2
    URL_HASH SHA256=98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_Declare(pack_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(pack_miniz pack_json)
  if(STUDIO_SDL3)
    install(FILES "${pack_miniz_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME miniz-LICENSE.txt)
    install(FILES "${pack_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION licenses RENAME nlohmann-json-LICENSE.txt)
  endif()
endfunction()
studio_pack_dependencies()
