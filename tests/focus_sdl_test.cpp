/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <cstdio>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
bool SendFocusEvent(bool active);
bool SendControlKey();
class FocusApp : public ApplicationBase
{
  public:
    int activations = 0;
    void _Activate(bool active) override
    {
        ++activations;
        ApplicationBase::_Activate(active);
    }
    void NativeActivation(bool active)
    {
        LRESULT result;
        _FCommonWndProc(vwig.hwndApp, WM_ACTIVATEAPP, active, 0, &result);
    }
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL focus check failed: %d\n", __LINE__);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static void Pump(FocusApp *app)
{
    for (int i = 0; i < 20; ++i)
        app->PumpHostEvent();
}
int main()
{
    auto &host = KauaiSDL::Main();
    CHECK(host.Create("SDL activation check", 64, 48, true));
    vwig.hwndApp = static_cast<HWND>(host.NativeWindow());
    auto app = NewObj FocusApp;
    CHECK(app && vwig.hwndApp);
    Pump(app);
    CHECK(SendFocusEvent(true));
    Pump(app);
    CHECK(app->FForeground());
    const int before = app->activations;
    app->NativeActivation(false);
    CHECK(app->FForeground() && app->activations == before);
    CHECK(SendFocusEvent(true));
    Pump(app);
    CHECK(app->activations == before);
    CHECK(SendControlKey() && host.PushPointer(3, 4, true));
    KauaiSDL::Event event;
    for (int i = 0; i < 20 && host.Poll(event); ++i)
    {
    }
    float x, y;
    bool down;
    host.Mouse(x, y, down);
    CHECK(down && (host.Modifiers() & KauaiSDL::control));
    CHECK(SendFocusEvent(false));
    Pump(app);
    host.Mouse(x, y, down);
    CHECK(!app->FForeground() && !down && !host.Modifiers());
    CHECK(SendFocusEvent(true));
    Pump(app);
    CHECK(app->FForeground());
    // A native modal loop retains application activation even when its owner
    // loses keyboard focus. Its queued owner focus events must not replay.
    CHECK(host.PushPointer(3, 4, true) && SendControlKey());
    for (int i = 0; i < 20 && host.Poll(event); ++i)
    {
    }
    EnableWindow(vwig.hwndApp, FALSE);
    host.SuspendFocusEvents(true); // This fixture does not subclass SDL's HWND.
    host.Mouse(x, y, down);
    CHECK(!down && !host.Modifiers());
    CHECK(SendFocusEvent(false));
    Pump(app);
    CHECK(app->FForeground());
    app->NativeActivation(false);
    CHECK(!app->FForeground());
    app->NativeActivation(true);
    CHECK(app->FForeground());
    CHECK(SendFocusEvent(false));
    CHECK(host.PushText("after-modal"));
    EnableWindow(vwig.hwndApp, TRUE);
    host.SuspendFocusEvents(false);
    bool retainedText = false;
    for (int i = 0; i < 20 && host.Poll(event); ++i)
        if (event.kind == KauaiSDL::EventKind::text && event.text == "after-modal")
            retainedText = true;
    CHECK(retainedText);
    const int afterModal = app->activations;
    Pump(app);
    CHECK(app->FForeground() && app->activations == afterModal);
    ReleasePpo(&app);
    vwig.hwndApp = nullptr;
    host.Close();
    std::puts(
        "SDL activation: queued focus, duplicate suppression, drag/modifier reset and native modal handoff passed.");
}
