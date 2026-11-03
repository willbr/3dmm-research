/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <cmath>
#include <cstdio>
#include <utility>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Window geometry failed at %d: %s\n", __LINE__, #v);                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto &host = KauaiSDL::Main();
    CHECK(host.Create("Window geometry", 64, 48, true));
    vwig.hwndApp = static_cast<HWND>(host.NativeWindow());
    CHECK(vwig.hwndApp);
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
    auto app = NewObj ApplicationBase;
    CHECK(app && vntl.FInit() && GraphicsObject::FInitScreen(0, kginMark));
    auto root = GraphicsObject::PgobScreen();
    CHECK(root);
    // Keep this small test window away from other applications' centered
    // topmost dialogs. Native global hit testing correctly respects occlusion.
    RECT workArea;
    CHECK(SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0));
    CHECK(SetWindowPos(vwig.hwndApp, HWND_TOPMOST, workArea.left + 32, workArea.top + 32, 0, 0,
                       SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW));
    RC parentBounds(10, 7, 40, 35), childBounds(3, 4, 18, 19);
    GraphicsObjectBlock parentBlock(7123, root, fgobNil, kginMark, &parentBounds);
    auto parent = NewObj GraphicsObject(&parentBlock);
    CHECK(parent);
    GraphicsObjectBlock childBlock(7124, parent, fgobNil, kginMark, &childBounds);
    auto child = NewObj GraphicsObject(&childBlock);
    CHECK(child);
    String name = PszLit("Caf\xE9 window");
    root->SetHwndName(&name);
    std::string title;
    CHECK(host.Title(title) && title == "Caf\xC3\xA9 window");
    for (auto dimensions : {std::pair<int, int>{64, 48}, {173, 115}, {197, 163}, {101, 73}})
    {
        CHECK(host.SetWindowSize(dimensions.first, dimensions.second));
        root->SetRcFromHwnd();
        RC rc;
        root->GetRc(&rc, cooLocal);
        RC canvas(0, 0, 64, 48);
        CHECK(rc == canvas);
        for (long source : {cooLocal, cooParent, cooGpt, cooHwnd})
            for (long target : {cooLocal, cooParent, cooGpt, cooHwnd})
                for (PT point : {PT(-2, 3), PT(0, 0), PT(1, 1), PT(14, 14)})
                {
                    PT expected = point;
                    child->MapPt(&expected, source, target);
                    PT global = point;
                    child->MapPt(&global, source, cooGlobal);
                    PT actual = global;
                    child->MapPt(&actual, cooGlobal, target);
                    if (!(actual == expected))
                        std::fprintf(stderr,
                                     "%dx%d map %ld->%ld point %ld,%ld global %ld,%ld expected %ld,%ld got %ld,%ld\n",
                                     dimensions.first, dimensions.second, source, target, point.xp, point.yp, global.xp,
                                     global.yp, expected.xp, expected.yp, actual.xp, actual.yp);
                    CHECK(actual == expected);
                    PT logical = point;
                    child->MapPt(&logical, source, cooHwnd);
                    float x, y;
                    CHECK(host.CanvasToDesktop(logical.xp, logical.yp, x, y));
                    CHECK(global.xp == std::ceil(x) && global.yp == std::ceil(y));
                }
        RC area(0, 0, 15, 15), global = area;
        child->MapRc(&global, cooLocal, cooGlobal);
        child->MapRc(&global, cooGlobal, cooLocal);
        CHECK(global == area);
        RC overflow(0, 0, INT32_MAX, INT32_MAX), retained = overflow;
        child->MapRc(&overflow, cooLocal, cooGlobal);
        CHECK(overflow == retained);
        PT hit(2, 3);
        child->MapPt(&hit, cooLocal, cooGlobal);
        PT local, expectedHit(2, 3);
        const auto found = GraphicsObject::PgobFromPtGlobal(hit.xp, hit.yp, &local);
        if (found != child || !(local == expectedHit))
        {
            POINT nativePoint = {hit.xp, hit.yp};
            POINT origin = {};
            ClientToScreen(vwig.hwndApp, &origin);
            char windowClass[128] = {};
            GetClassNameA(WindowFromPoint(nativePoint), windowClass, 128);
            std::fprintf(stderr, "Occluder %s, desktop %dx%d\n", windowClass, GetSystemMetrics(SM_CXSCREEN),
                         GetSystemMetrics(SM_CYSCREEN));
            std::fprintf(stderr,
                         "Hit %dx%d global %ld,%ld local %ld,%ld client origin %ld,%ld window %p actual %p visible %d "
                         "iconic %d\n",
                         dimensions.first, dimensions.second, hit.xp, hit.yp, local.xp, local.yp, origin.x, origin.y,
                         static_cast<void *>(vwig.hwndApp), static_cast<void *>(WindowFromPoint(nativePoint)),
                         IsWindowVisible(vwig.hwndApp), IsIconic(vwig.hwndApp));
        }
        CHECK(found == child && local == expectedHit);
        PT border(-1, 0);
        root->MapPt(&border, cooLocal, cooGlobal);
        CHECK(!GraphicsObject::PgobFromPtGlobal(border.xp, border.yp, &local) && local == border);
    }
    child->Release();
    parent->Release();
    GraphicsObject::ShutDown();
    ReleasePpo(&app);
    host.Close();
    vwig.hwndApp = nullptr;
    std::puts("SDL gob geometry: nested local/parent/port/window/global mapping, scaled rectangles, fixed canvas "
              "bounds and encoded title passed.");
}
