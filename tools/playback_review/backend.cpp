#include "backend.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
int review_pick_database(char *path, int capacity)
{
#ifdef _WIN32
    wchar_t filename[4096] = {0};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = L"Open playback regression results";
    dialog.lpstrFilter = L"SQLite results\0*.sqlite;*.db\0All files\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = sizeof(filename) / sizeof(filename[0]);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, capacity, nullptr, nullptr) != 0;
#else
    (void)path;
    (void)capacity;
    return 0;
#endif
}
unsigned long review_window_flags(int hidden)
{
    return (unsigned long)(SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
}
int review_backend_init(void *window, void *renderer)
{
    return ImGui_ImplSDL3_InitForSDLRenderer((SDL_Window *)window, (SDL_Renderer *)renderer) &&
           ImGui_ImplSDLRenderer3_Init((SDL_Renderer *)renderer);
}
void review_backend_event(const void *event)
{
    ImGui_ImplSDL3_ProcessEvent((const SDL_Event *)event);
}
void review_backend_frame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}
void review_backend_render(void *renderer)
{
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), (SDL_Renderer *)renderer);
}
void review_backend_quit()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}
