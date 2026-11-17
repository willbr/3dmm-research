include_guard(GLOBAL)
include(ImGuiDependency)
include(FreeTypeDependency)
include(FetchContent)

# Generated bindings for the same Dear ImGui 1.92.5 used by imgui-sdl3.
# The generated wrappers and demo helpers link the shared ImGui core/backends;
# Marcus keeps the existing gizmo context.
FetchContent_Declare(marcus_cimgui
  GIT_REPOSITORY https://github.com/cimgui/cimgui.git
  GIT_TAG bfd30140a9c5832b5e0dcf179d6e1e5c69373d5a
  GIT_SUBMODULES imgui
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(marcus_cimgui)
foreach(header IN ITEMS imgui.h imgui_internal.h imconfig.h)
  file(READ "${imgui_SOURCE_DIR}/${header}" shared_header)
  file(READ "${marcus_cimgui_SOURCE_DIR}/imgui/${header}" binding_header)
  string(REPLACE "\r\n" "\n" shared_header "${shared_header}")
  string(REPLACE "\r\n" "\n" binding_header "${binding_header}")
  if(NOT shared_header STREQUAL binding_header)
    message(FATAL_ERROR "cimgui and imgui-sdl3 must use identical ${header}")
  endif()
endforeach()
# The generated API also exports the standard demo/style helpers.
add_library(cimgui-sdl3 STATIC "${marcus_cimgui_SOURCE_DIR}/cimgui.cpp" "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${marcus_cimgui_SOURCE_DIR}/imgui/misc/freetype/imgui_freetype.cpp")
target_compile_features(cimgui-sdl3 PRIVATE cxx_std_11)
target_compile_definitions(cimgui-sdl3 PUBLIC CIMGUI_NO_EXPORT IMGUI_ENABLE_FREETYPE IMGUI_ENABLE_STB_TRUETYPE)
# Generated declarations use bool/64-bit extensions. Application code remains C89.
target_include_directories(cimgui-sdl3 SYSTEM PUBLIC "${marcus_cimgui_SOURCE_DIR}")
# The generated wrapper includes its own imgui.h by relative path. Keep its
# FreeType header on that same verified copy to avoid duplicate declarations.
target_include_directories(cimgui-sdl3 BEFORE PRIVATE "${marcus_cimgui_SOURCE_DIR}/imgui")
# Keep FreeType opt-in for Marcus's atlas; the shared ImGui core keeps stb.
target_link_libraries(cimgui-sdl3 PUBLIC imgui-sdl3 PRIVATE freetype)
install(FILES "${marcus_cimgui_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME cimgui-LICENSE.txt)
