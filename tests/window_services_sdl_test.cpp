/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Window services failed at %d: %s (%s)\n", __LINE__, #v, host.Error());               \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    KauaiSDL::Host host;
    std::string title = "retained";
    float x = 91, y = 92;
    KauaiSDL::WindowStatus state;
    state.maximized = true;
    CHECK(!host.WindowState(state) && state.maximized && !host.SetMaximized(true) && !host.EnsureInteractive());
    CHECK(!host.Title(title) && title == "retained" && !host.SetTitle("closed") && !host.Raise());
    CHECK(!host.CanvasToDesktop(0, 0, x, y) && x == 91 && y == 92);
    CHECK(host.WindowToken() == nullptr && !host.IsWindow(nullptr));
    CHECK(host.Create("Window services", 64, 48, true));
    CHECK(host.WindowToken() && host.IsWindow(host.WindowToken()));
    CHECK(!host.NativeWindow() || host.WindowToken() == host.NativeWindow());
    CHECK(!host.IsWindow(&host));
    auto window = SDL_GetWindowFromID(host.WindowID());
    CHECK(window);
    CHECK(host.WindowState(state) && !state.shown && !state.minimized && !state.maximized && !state.fullscreen);
    CHECK(host.EnsureInteractive() && SDL_SyncWindow(window));
    CHECK(host.WindowState(state) && state.shown);
    CHECK(host.SetTitle("Caf\xC3\xA9 \xF0\x9F\x8E\xAC") && host.Title(title) && title == SDL_GetWindowTitle(window));
    CHECK(title == "Caf\xC3\xA9 \xF0\x9F\x8E\xAC" && !host.SetTitle(std::string("a\0b", 3)));
    for (auto dimensions : {std::pair<int, int>{64, 48}, {173, 115}, {197, 163}, {101, 73}})
    {
        CHECK(host.SetWindowSize(dimensions.first, dimensions.second));
        SDL_PumpEvents();
        int width, height, left, top;
        CHECK(SDL_GetWindowSize(window, &width, &height) && SDL_GetWindowPosition(window, &left, &top));
        KauaiSDL::PresentationStatus view;
        CHECK(host.Presentation(view));
        const auto scale = (std::min)(view.outputWidth / 64, view.outputHeight / 48);
        CHECK(scale >= 1 && view.width == scale * 64 && view.height == scale * 48);
        for (float sx : {-12.0f, 0.0f, 1.0f, 23.5f, 63.0f, 64.0f, 80.0f})
            for (float sy : {-9.0f, 0.0f, 1.0f, 19.25f, 47.0f, 48.0f, 61.0f})
            {
                CHECK(host.CanvasToDesktop(sx, sy, x, y));
                // Independent geometry from the actual output viewport and
                // window size, rather than a second coordinate-conversion call.
                const auto expectedX = left + (view.x + sx * scale) * width / view.outputWidth;
                const auto expectedY = top + (view.y + sy * scale) * height / view.outputHeight;
                CHECK(std::abs(x - expectedX) < 0.001f && std::abs(y - expectedY) < 0.001f);
                float cx, cy;
                CHECK(host.DesktopToCanvas(x, y, cx, cy) && std::abs(cx - sx) < 0.001f && std::abs(cy - sy) < 0.001f);
            }
    }
    x = 91;
    y = 92;
    CHECK(!host.CanvasToDesktop(std::numeric_limits<float>::infinity(), 0, x, y) && x == 91 && y == 92);
    CHECK(!host.DesktopToCanvas(0, std::numeric_limits<float>::quiet_NaN(), x, y) && x == 91 && y == 92);
    host.Close();
    CHECK(host.WindowToken() == nullptr && !host.IsWindow(nullptr));
    CHECK(!host.WindowState(state) && state.shown && !host.SetMaximized(false));
    CHECK(!host.DesktopToCanvas(0, 0, x, y) && x == 91 && y == 92);
    CHECK(host.Create("Restarted", 64, 48, true) && host.Title(title) && title == "Restarted");
    std::puts("SDL window services: UTF-8 title, rejected NUL, scaled/letterboxed desktop mapping, inverse points, "
              "invalid input and restart passed.");
}
