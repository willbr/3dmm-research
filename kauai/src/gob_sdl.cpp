/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// POSIX window services for Studio's single SDL root window.
#include "sdl_host.h"
#include "sdl_studio.h"
#include "font_services.h"
#include "frame.h"
ASSERTNAME
CoreNative::WindowHandle GraphicsObject::_HwndApp()
{
    return static_cast<CoreNative::WindowHandle>(KauaiSDL::Main().WindowToken());
}
void InvalHwndRcs(CoreNative::WindowHandle window, SystemRectangle *rectangle)
{
    KauaiSDLBridge::Invalidate(window, rectangle);
}
void ValidHwndRcs(CoreNative::WindowHandle window, SystemRectangle *rectangle)
{
    KauaiSDLBridge::Validate(window, rectangle);
}
void GraphicsObject::GetPtMouse(PT *point, bool *down)
{
    AssertThis(0);
    float x, y;
    bool pressed;
    KauaiSDL::Main().Mouse(x, y, pressed);
    if (point)
    {
        point->xp = static_cast<LegacyLong>(x);
        point->yp = static_cast<LegacyLong>(y);
        MapPt(point, cooHwnd, cooLocal);
    }
    if (down)
        *down = pressed;
}
void GraphicsObject::Clean()
{
    AssertThis(0);
    RC rectangle;
    auto window = _HwndGetRc(&rectangle);
    if (!window)
        return;
    vpappb->InvalMarked(window);
    SystemRectangle pending;
    if (KauaiSDLBridge::DamageBounds(window, &pending))
    {
        RC damage(pending);
        if (rectangle.FIntersect(&damage))
            KauaiSDLBridge::Update(window);
    }
}
void GraphicsObject::SetHwndName(PString name)
{
    AssertPo(name, 0);
    std::string utf8;
    if (KauaiSDL::Main().IsWindow(_hwnd) &&
        KauaiFonts::TextToUtf8({name->Prgch(), static_cast<std::size_t>(name->Cch()),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               utf8))
        KauaiSDL::Main().SetTitle(utf8);
}
void GraphicsObject::MakeHwndActive(CoreNative::WindowHandle window)
{
    if (KauaiSDL::Main().IsWindow(window))
        KauaiSDL::Main().Raise();
}
CoreNative::WindowHandle GraphicsObject::HwndMdiActive()
{
    // Studio's SDL host has one root window and no native MDI client.
    return hNil;
}
CoreNative::WindowHandle GraphicsObject::_HwndNewMdi(PString)
{
    PushErc(ercGfxCantDraw); // Creating native MDI children is unsupported.
    return hNil;
}
void GraphicsObject::_DestroyHwnd(CoreNative::WindowHandle window)
{
    if (KauaiSDL::Main().IsWindow(window))
        Bug("can't destroy app window; detach the root before releasing it");
    // This adapter never owns foreign windows or native MDI children.
}
