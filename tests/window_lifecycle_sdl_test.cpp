/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include "font_host.h"
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL_video.h>
#include <cstring>
#include <cstdio>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class WindowApp : public ApplicationBase
{
  public:
    bool Open()
    {
        return _FInitSDLWindow("SDL lifecycle", 64, 48);
    }
    void Close()
    {
        _CloseSDLWindow();
    }
};
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "SDL window lifecycle failed at %d\n", __LINE__);                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto app = NewObj WindowApp;
    CHECK(app);
    auto &host = KauaiSDL::Main();
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        _putenv_s("KAUAI_FONT_PROVIDER", cycle < 2 ? "freetype" : "gdi");
        const bool nativeFonts = std::strcmp(KauaiFonts::StudioFontProvider(), "freetype") != 0;
        CHECK(app->Open());
        const auto window = static_cast<HWND>(host.NativeWindow());
        CHECK(window && vwig.hwndApp == window && bool(vwig.hdcApp) == nativeFonts);
        // Kauai must not replace SDL's native window procedure.
        // SDL registers a Unicode class; use W accessors to compare actual
        // procedures rather than distinct ANSI compatibility thunks.
        CHECK(GetWindowLongPtrW(window, GWLP_WNDPROC) == GetClassLongPtrW(window, GCLP_WNDPROC));
        auto sdlWindow = SDL_GetWindowFromID(host.WindowID());
        KauaiSDL::WindowStatus state;
        long property = -1;
        CHECK(sdlWindow && host.WindowState(state) && state.shown && !state.minimized);
        CHECK(app->FSetProp(kpridMaximized, fTrue) && SDL_SyncWindow(sdlWindow));
        CHECK(host.WindowState(state) && state.maximized && IsZoomed(window));
        CHECK(app->FGetProp(kpridMaximized, &property) && property == 1);
        CHECK(host.EnsureInteractive() && SDL_SyncWindow(sdlWindow));
        CHECK(host.WindowState(state) && state.maximized && IsZoomed(window));
        // Simulate the user minimizing through the desktop. Query the actual
        // state, then restore through SDL without relying on a WM_SIZE hook.
        ShowWindow(window, SW_MINIMIZE);
        CHECK(host.WindowState(state) && state.minimized && IsIconic(window));
        CHECK(host.EnsureInteractive() && SDL_SyncWindow(sdlWindow));
        CHECK(host.WindowState(state) && !state.minimized && !IsIconic(window) && state.shown);
        CHECK(app->FSetProp(kpridMaximized, fFalse) && SDL_SyncWindow(sdlWindow));
        CHECK(app->FGetProp(kpridMaximized, &property) && property == 0 && !IsZoomed(window));
        CHECK(app->FSetProp(kpridFullScreen, fTrue) && SDL_SyncWindow(sdlWindow));
        CHECK(app->FGetProp(kpridFullScreen, &property) && property == 1);
        CHECK(host.WindowState(state) && state.fullscreen);
        // Host-driven changes must also be reflected by the application API.
        CHECK(host.SetFullscreen(false) && SDL_SyncWindow(sdlWindow));
        CHECK(app->FGetProp(kpridFullScreen, &property) && property == 0);
        KauaiSDL::Event event;
        for (int i = 0; i < 30; ++i)
            if (!host.Poll(event, 1))
                break;
        std::vector<KauaiPixels::DamageRect> damage;
        host.TakeDamage(damage);
        RECT rectangle = {0, 0, 20, 20};
        CHECK(InvalidateRect(window, &rectangle, FALSE));
        SendMessage(window, WM_PAINT, 0, 0);
        bool exposed = false;
        for (int i = 0; i < 30 && !exposed; ++i)
            if (host.Poll(event, 10) && event.kind == KauaiSDL::EventKind::exposed)
                exposed = true;
        CHECK(exposed && host.TakeDamage(damage) && !damage.empty());
        // SDL turns the OS close request into an event. The application still
        // owns the save/discard decision and the window remains alive meanwhile.
        SendMessage(window, WM_CLOSE, 0, 0);
        bool quit = false;
        for (int i = 0; i < 30 && !quit; ++i)
            if (host.Poll(event, 10) && event.kind == KauaiSDL::EventKind::quit)
                quit = true;
        CHECK(quit && IsWindow(window));
        app->Close();
        CHECK(!host.Active() && !vwig.hwndApp && !vwig.hdcApp && !IsWindow(window));
        app->Close(); // Partial/duplicate cleanup is safe.
        CHECK(!app->FSetProp(kpridMaximized, fTrue) && !app->FSetProp(kpridFullScreen, fTrue));
    }
    ReleasePpo(&app);
    std::puts("SDL lifecycle: unmodified SDL window procedure, native exposure/close translated into SDL events, "
              "SDL maximize/fullscreen properties, minimized restore, provider-dependent screen DC and repeated "
              "shutdown passed.");
}
