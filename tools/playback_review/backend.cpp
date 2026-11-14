#include "backend.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
static int pick_file(char *path, int capacity, int kind)
{
#ifdef _WIN32
    wchar_t filename[4096] = {0};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = kind == 2 ? L"Choose a movie to compare" : kind == 1 ? L"Open comparison image" : L"Open playback regression results";
    dialog.lpstrFilter = kind == 2 ? L"3DMM movies\0*.3mm;*.zip\0All files\0*.*\0" : kind == 1 ? L"PNG images\0*.png\0All files\0*.*\0" : L"SQLite results\0*.sqlite;*.db\0All files\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = sizeof(filename) / sizeof(filename[0]);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, capacity, nullptr, nullptr) != 0;
#else
    (void)path;
    (void)capacity;
    (void)kind;
    return 0;
#endif
}
int review_pick_database(char *path, int capacity)
{
    return pick_file(path, capacity, false);
}
int review_pick_image(char *path, int capacity)
{
    return pick_file(path, capacity, true);
}
int review_pick_movie(char *path, int capacity)
{
    return pick_file(path, capacity, 2);
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
