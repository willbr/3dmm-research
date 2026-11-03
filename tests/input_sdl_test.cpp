/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL input policy failed: %d\n", __LINE__);                                           \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static void Key(SDL_Scancode scan, bool down, bool repeat, std::uint64_t milliseconds)
{
    SDL_Event event = {};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.repeat = repeat;
    event.key.scancode = scan;
    event.key.timestamp = milliseconds * 1000000;
    CHECK(SDL_PushEvent(&event));
}
static void Text(const char *text)
{
    SDL_Event event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    CHECK(SDL_PushEvent(&event));
}
static void Focus(bool active)
{
    int count = 0;
    auto windows = SDL_GetWindows(&count);
    CHECK(windows && count == 1);
    SDL_Event event = {};
    event.type = active ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = SDL_GetWindowID(windows[0]);
    SDL_free(windows);
    CHECK(SDL_PushEvent(&event));
}
static void Expect(KauaiSDL::Host &host, const char *keys, const char *text, int ups = 0)
{
    std::string actualKeys, actualText;
    int actualUps = 0;
    KauaiSDL::Event event;
    while (host.Poll(event))
    {
        if (event.kind == KauaiSDL::EventKind::keyDown)
            actualKeys += char(event.key);
        if (event.kind == KauaiSDL::EventKind::keyUp)
            ++actualUps;
        if (event.kind == KauaiSDL::EventKind::text)
            actualText += event.text;
    }
    CHECK(actualKeys == keys && actualText == text && actualUps == ups);
}
int main()
{
    KauaiSDL::Host host;
    CHECK(host.Create("SDL input policy check", 64, 48, true));
    Expect(host, "", "");
    CHECK(!host.SlowKeyRepeat());
    Key(SDL_SCANCODE_A, true, false, 100);
    Text("a");
    Key(SDL_SCANCODE_A, true, true, 101);
    Text("a");
    Expect(host, "AA", "aa");
    host.SetSlowKeyRepeat(true);
    Key(SDL_SCANCODE_A, true, true, 600);
    Text("a");
    Key(SDL_SCANCODE_B, true, false, 650);
    Text("b");
    Key(SDL_SCANCODE_A, true, true, 1100);
    Text("a");
    Key(SDL_SCANCODE_A, true, true, 1499);
    Text("a");
    Key(SDL_SCANCODE_A, true, true, 1500);
    Text("a");
    Key(SDL_SCANCODE_B, true, true, 1650);
    Text("b");
    Expect(host, "BAAB", "baab");
    Key(SDL_SCANCODE_A, false, false, 1651);
    Key(SDL_SCANCODE_A, true, false, 1700);
    Text("a");
    Key(SDL_SCANCODE_A, true, true, 2000);
    Text("a");
    CHECK(host.PushText("explicit"));
    Expect(host, "A", "aexplicit", 1);
    host.SetSlowKeyRepeat(false);
    Key(SDL_SCANCODE_A, true, true, 2001);
    Text("a");
    Expect(host, "A", "a");
    // Flush and focus loss must not retain a previous repeat clock.
    host.SetSlowKeyRepeat(true);
    host.FlushInput(false, true);
    Key(SDL_SCANCODE_A, true, false, 2100);
    Key(SDL_SCANCODE_A, true, true, 3099);
    Key(SDL_SCANCODE_A, true, true, 3100);
    Expect(host, "AA", "");
    Focus(false);
    Focus(true);
    Key(SDL_SCANCODE_A, true, true, 3200);
    Text("a");
    Expect(host, "A", "a");
    CHECK(host.CapturePointer(true) && host.ConfinePointer(true));
    CHECK(host.Pointer().captureRequested && host.Pointer().confinementRequested);
    host.SuspendPointer(true);
    CHECK(host.Pointer().suspended && !host.Pointer().captureActive && !host.Pointer().confinementActive);
    host.SuspendPointer(false);
    CHECK(!host.Pointer().suspended && host.Pointer().captureRequested && host.Pointer().confinementRequested);
    host.SuspendFocusEvents(true);
    CHECK(host.Pointer().suspended && !host.Pointer().captureActive && !host.Pointer().confinementActive);
    host.SuspendFocusEvents(false);
    CHECK(host.CapturePointer(false) && host.ConfinePointer(false));
    host.SetSlowKeyRepeat(true);
    host.Close();
    CHECK(!host.Pointer().captureRequested && !host.Pointer().confinementRequested && !host.SlowKeyRepeat());
    CHECK(host.Create("SDL input policy restart", 32, 32, true));
    CHECK(!host.Pointer().captureRequested && !host.Pointer().confinementRequested && !host.SlowKeyRepeat());
    std::puts("SDL input: local repeat delay/rate, independent keys, release/reset, text gating, capture/confinement "
              "policy and restart passed.");
}
