/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "frame.h"
#include "sdl_studio.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class ScrollApp : public ApplicationBase
{
  public:
    void Click(float x, float y, std::uint32_t ticks)
    {
        EVT event;
        event.kind = KauaiInput::Kind::buttonDown;
        event.button = 1;
        event.x = x;
        event.y = y;
        event.timestamp = ticks;
        _DispatchEvt(&event);
    }
    ~ScrollApp()
    {
        ReleasePpo(&_pglmkrgn);
    }
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "SDL scroll assertion: %s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
class RepaintGob : public GraphicsObject
{
  public:
    int draws = 0;
    long clicks = 0, clickX = 0, clickY = 0;
    void MouseDown(long x, long y, long count, ulong) override
    {
        clicks = count;
        clickX = x;
        clickY = y;
    }
    explicit RepaintGob(GraphicsObjectBlock *block) : GraphicsObject(block)
    {
    }
    void Draw(PGraphicsEnvironment, RC *) override
    {
        if (++draws == 1)
        {
            RC again(1, 1, 3, 3);
            InvalRc(&again, kginSysInval);
        }
    }
};
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "SDL control scroll check failed at %d\n", __LINE__);                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto &host = KauaiSDL::Main();
    CHECK(host.Create("SDL control scroll check", 64, 48, true));
    auto window = static_cast<CoreNative::WindowHandle>(host.WindowToken());
#ifdef WIN
    vwig.hwndApp = static_cast<HWND>(host.NativeWindow());
    CHECK(vwig.hwndApp);
    vwig.hdcApp = GetDC(vwig.hwndApp);
    CHECK(vwig.hdcApp);
#endif
    auto app = NewObj ScrollApp;
    CHECK(app && vntl.FInit() && GraphicsObject::FInitScreen(0, kginMark));
    auto root = GraphicsObject::PgobScreen();
    CHECK(root && root->Pgpt());
    auto pixels = reinterpret_cast<std::uint32_t *>(root->Pgpt()->PrgbLockPixels());
    CHECK(pixels && root->Pgpt()->CbRow() == 64 * 4);
    RC rectangle(2, 3, 30, 27);
    const auto inside = [](int x, int y) { return x >= 2 && x < 30 && y >= 3 && y < 27; };
    for (int gin : {ginNil, kginMark, kginSysInval, kginDraw})
    {
        root->ValidRc(nullptr, kginDraw);
        for (int i = 0; i < 64 * 48; ++i)
            pixels[i] = 0x123400 + i * 13;
        const std::vector<std::uint32_t> original(pixels, pixels + 64 * 48);
        RC bad1, bad2;
        root->Scroll(&rectangle, 3, -2, gin, &bad1, &bad2);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const bool moved = inside(x, y) && inside(x - 3, y + 2);
                const auto expected = moved ? original[(y + 2) * 64 + x - 3] : original[y * 64 + x];
                CHECK(pixels[y * 64 + x] == expected);
                CHECK((bad1.FPtIn(x, y) || bad2.FPtIn(x, y)) == (inside(x, y) && !moved));
            }
        RC marked;
        if (gin == kginMark)
            CHECK(app->FGetMarkedRc(window, &marked));
        if (gin == kginSysInval)
            CHECK(root->FGetRcInval(&marked, kginSysInval));
        if (gin == kginDraw)
            CHECK(!app->FGetMarkedRc(window, &marked));
    }
    // Existing damage moves exactly, even while the window is hidden.
    root->ValidRc(nullptr, kginDraw);
    RC dirty(4, 5, 8, 9), invalid;
    root->InvalRc(&dirty, kginSysInval);
    root->Scroll(&rectangle, 1, 1, ginNil);
    CHECK(root->FGetRcInval(&invalid, kginSysInval));
    CHECK(invalid.xpLeft == 5 && invalid.ypTop == 6 && invalid.xpRight == 9 && invalid.ypBottom == 10);
    // Requests raised by a Draw callback belong to the next paint pass.
    RC childRect(10, 10, 20, 20);
    GraphicsObjectBlock block(12345, root, fgobNil, kginSysInval, &childRect);
    auto child = NewObj RepaintGob(&block);
    CHECK(child);
    host.SetDoubleClickTime(100);
    app->Click(11, 12, UINT32_MAX - 10);
    CHECK(child->clicks == 1 && child->clickX == 1 && child->clickY == 2);
    app->Click(12, 13, 5);
    CHECK(child->clicks == 2 && child->clickX == 2 && child->clickY == 3);
    app->Click(11, 12, 105);
    CHECK(child->clicks == 1); // Threshold is exclusive, including tick wrap.
    auto manager = GuiCommandExecutionManager::PgcexNew(4, 4);
    CHECK(manager);
    manager->TrackMouse(child);
    app->ConfineCurs(child);
    CHECK(host.Pointer().captureRequested && host.Pointer().confinementRequested);
    manager->Suspend(true);
    CHECK(host.Pointer().suspended && !host.Pointer().captureActive && !host.Pointer().confinementActive);
    manager->Suspend(false);
    CHECK(!host.Pointer().suspended && host.Pointer().captureRequested);
    app->SetSlowKeyRepeat(true);
    manager->BuryCmh(child);
    CHECK(!manager->PgobTracking() && !host.Pointer().captureRequested && !host.Pointer().confinementRequested);
    CHECK(!host.SlowKeyRepeat());
    manager->TrackMouse(child);
    manager->EndMouseTracking();
    CHECK(!host.Pointer().captureRequested && !manager->PgobTracking());
    ReleasePpo(&manager);
    root->ValidRc(nullptr, kginDraw);
    child->InvalRc(nullptr, kginSysInval);
    KauaiSDLBridge::Paint();
    CHECK(child->draws == 1 && root->FGetRcInval(&invalid, kginSysInval));
    CHECK(invalid.xpLeft == 11 && invalid.ypTop == 11 && invalid.xpRight == 13 && invalid.ypBottom == 13);
    KauaiSDLBridge::Paint();
    CHECK(child->draws == 2 && !root->FGetRcInval(&invalid, kginSysInval));
    ReleasePpo(&child);
    root->ValidRc(nullptr, kginDraw);
    root->Pgpt()->Unlock();
    GraphicsObject::ShutDown();
    ReleasePpo(&app);
#ifdef WIN
    ReleaseDC(vwig.hwndApp, vwig.hdcApp);
    vwig.hwndApp = nullptr;
    vwig.hdcApp = nullptr;
#endif
    host.Close();
    std::puts("SDL control scrolling: framebuffer pixels, exposed areas, deferred/marked invalidation and pending "
              "repaint passed.");
    return 0;
}
