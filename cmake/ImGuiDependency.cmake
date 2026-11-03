include_guard(GLOBAL)
include(FetchContent)
include(SDL3Dependency)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(imgui
  URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.5
  URL_HASH SHA256=0eb50fe9aeba1a51f96b5843c7f630a32ed2e9362d693c61b87e4fa870cf826d)
FetchContent_MakeAvailable(imgui)
add_library(imgui-sdl3 STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui-sdl3 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui-sdl3 PUBLIC SDL3::SDL3-static)
install(FILES "${PROJECT_SOURCE_DIR}/docs/licenses/imgui-LICENSE.txt" DESTINATION licenses)
