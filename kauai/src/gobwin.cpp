/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Graphic object class.

***************************************************************************/
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "sdl_studio.h"
#include "platform_text.h"
#include "text_utf8.h"
#endif
#include "frame.h"
ASSERTNAME

void InvalHwndRcs(HWND window, SystemRectangle *rectangle)
{
#ifdef KAUAI_SDL3
    if (KauaiSDLBridge::Invalidate(window, rectangle))
        return;
#endif
    InvalidateRect(window, rectangle, fFalse);
}
void ValidHwndRcs(HWND window, SystemRectangle *rectangle)
{
#ifdef KAUAI_SDL3
    if (KauaiSDLBridge::Validate(window, rectangle))
        return;
#endif
    ValidateRect(window, rectangle);
}

CoreNative::WindowHandle GraphicsObject::_HwndApp()
{
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().Active())
        return static_cast<CoreNative::WindowHandle>(KauaiSDL::Main().WindowToken());
#endif
    return vwig.hwndApp;
}

/***************************************************************************
    Return the active MDI window.
***************************************************************************/
HWND GraphicsObject::HwndMdiActive(void)
{
    if (vwig.hwndClient == hNil)
        return hNil;

    return (HWND)SendMessage(vwig.hwndClient, WM_MDIGETACTIVE, 0, 0);
}

/***************************************************************************
    Creates a new MDI window and returns it.  This is normally then
    attached to a gob.
***************************************************************************/
HWND GraphicsObject::_HwndNewMdi(PString pstnTitle)
{
    AssertPo(pstnTitle, 0);
    HWND hwnd, hwndT;
    long lwStyle;

    if (vwig.hwndClient == hNil)
    {
        // create the client first
        CLIENTCREATESTRUCT ccs;
        SystemRectangle rcs;

        ccs.hWindowMenu = hNil;
        ccs.idFirstChild = 1;
        GetClientRect(vwig.hwndApp, &rcs);
        Assert(rcs.left == 0 && rcs.top == 0, 0);
        vwig.hwndClient = CreateWindow(PszLit("MDICLIENT"), NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE, 0, 0,
                                       rcs.right, rcs.bottom, vwig.hwndApp, NULL, vwig.hinst, (LPVOID)&ccs);
        if (vwig.hwndClient == hNil)
            return hNil;
    }

    lwStyle = MDIS_ALLCHILDSTYLES | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU | WS_CAPTION |
              WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    hwndT = HwndMdiActive();
    if (hNil == hwndT || IsZoomed(hwndT))
        lwStyle |= WS_MAXIMIZE;

    hwnd = CreateMDIWindow(PszLit("MDI"), pstnTitle->Psz(), lwStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, vwig.hwndClient, vwig.hinst, 0L);
    if (hNil != hwnd && pvNil != vpmubCur)
        vpmubCur->FAddListCid(cidChooseWnd, (long)hwnd, pstnTitle);
    return hwnd;
}

/***************************************************************************
    Destroy an hwnd.
***************************************************************************/
void GraphicsObject::_DestroyHwnd(HWND hwnd)
{
    if (hwnd == vwig.hwndApp)
    {
        Bug("can't destroy app window");
        return;
    }
    if (GetParent(hwnd) == vwig.hwndClient && vwig.hwndClient != hNil)
    {
        if (pvNil != vpmubCur)
            vpmubCur->FRemoveListCid(cidChooseWnd, (long)hwnd);
        SendMessage(vwig.hwndClient, WM_MDIDESTROY, (WPARAM)hwnd, 0);
    }
    else
        DestroyWindow(hwnd);
}

/***************************************************************************
    Gets the current mouse location in this gob's coordinates (if ppt is
    not nil) and determines if the mouse button is down (if pfDown is
    not nil).
***************************************************************************/
void GraphicsObject::GetPtMouse(PT *ppt, bool *pfDown)
{
    AssertThis(0);
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().Active())
    {
        float x, y; bool down;
        KauaiSDL::Main().Mouse(x, y, down);
        if (ppt)
        {
            ppt->xp = static_cast<long>(x); ppt->yp = static_cast<long>(y);
            MapPt(ppt, cooHwnd, cooLocal);
        }
        if (pfDown) *pfDown = down;
        return;
    }
#endif
    if (ppt != pvNil)
    {
        SystemPoint pts;
        long xp, yp;
        PGraphicsObject pgob;

        xp = yp = 0;
        for (pgob = this; pgob != pvNil && pgob->_hwnd == hNil; pgob = pgob->_pgobPar)
        {
            xp += pgob->_rcCur.xpLeft;
            yp += pgob->_rcCur.ypTop;
        }
        GetCursorPos(&pts);
        if (pgob != pvNil)
            ScreenToClient(pgob->_hwnd, &pts);
        *ppt = PT(pts);
        ppt->xp -= xp;
        ppt->yp -= yp;
    }
    if (pfDown != pvNil)
        *pfDown = GetAsyncKeyState(VK_LBUTTON) < 0;
}

/***************************************************************************
    Makes sure the GraphicsObject is clean (no update is pending).
***************************************************************************/
void GraphicsObject::Clean(void)
{
    AssertThis(0);
    HWND hwnd;
    RC rc, rcT;
    SystemRectangle rcs;

    if (hNil == (hwnd = _HwndGetRc(&rc)))
        return;

    vpappb->InvalMarked(hwnd);
#ifdef KAUAI_SDL3
    if (!KauaiSDLBridge::DamageBounds(hwnd, &rcs))
#endif
        GetUpdateRect(hwnd, &rcs, fFalse);
    rcT = RC(rcs);
    if (rc.FIntersect(&rcT))
#ifdef KAUAI_SDL3
        if (!KauaiSDLBridge::Update(hwnd))
#endif
        UpdateWindow(hwnd);
}

/***************************************************************************
    Set the window name.
***************************************************************************/
void GraphicsObject::SetHwndName(PString pstn)
{
    if (hNil == _hwnd)
    {
        Bug("GraphicsObject doesn't have an hwnd");
        return;
    }
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(_hwnd))
    {
        try
        {
#ifdef UNICODE
            const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
            const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
            const auto bytes = pstn->Cch() * sizeof(achar);
            const auto count = KauaiText::ToUtf16(pstn->Psz(), bytes, encoding, nullptr, 0);
            std::vector<std::uint16_t> units(count);
            std::string title;
            if ((!bytes || count) &&
                (!count || KauaiText::ToUtf16(pstn->Psz(), bytes, encoding, units.data(), count)) &&
                KauaiText::EncodeUtf8(units, title))
                KauaiSDL::Main().SetTitle(title);
        }
        catch (const std::bad_alloc &)
        {
        }
        return;
    }
#endif
    if (pvNil != vpmubCur)
    {
        vpmubCur->FChangeListCid(cidChooseWnd, (long)_hwnd, pvNil, (long)_hwnd, pstn);
    }
    SetWindowText(_hwnd, pstn->Psz());
}

/***************************************************************************
    If this is one of our MDI windows, make it the active MDI window.
***************************************************************************/
void GraphicsObject::MakeHwndActive(HWND hwnd)
{
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(hwnd))
    {
        KauaiSDL::Main().Raise();
        return;
    }
#endif
    if (IsWindow(hwnd) && GetParent(hwnd) == vwig.hwndClient && vwig.hwndClient != hNil)
        SendMessage(vwig.hwndClient, WM_MDIACTIVATE, (WPARAM)hwnd, 0);
}
