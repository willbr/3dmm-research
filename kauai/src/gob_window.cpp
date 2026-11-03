/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "frame.h"
ASSERTNAME

PGraphicsObject GraphicsObject::_pgobScreen;

/***************************************************************************
    Create the screen gob.  If fgobEnsureHwnd is set, ensures that the
    screen gob has an OS window associated with it.
***************************************************************************/
bool GraphicsObject::FInitScreen(ulong grfgob, long ginDef)
{
    const auto window = _HwndApp();
    if ((grfgob & fgobEnsureHwnd) && !window)
        return fFalse;
    PGraphicsObject pgob;
    GraphicsObjectBlock gcb(khidScreen, pvNil);

    switch (ginDef)
    {
    case kginDraw:
    case kginMark:
    case kginSysInval:
        _ginDefGob = ginDef;
        break;
    }

    if (pvNil == (pgob = NewObj GraphicsObject(&gcb)))
        return fFalse;
    Assert(pgob == _pgobScreen, 0);

    if (!pgob->FAttachHwnd(window))
    {
        ReleasePpo(&pgob);
        return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Make the GraphicsObject a wrapper for the given system window.
***************************************************************************/
bool GraphicsObject::FAttachHwnd(CoreNative::WindowHandle hwnd)
{
    if (_hwnd != hNil)
    {
        ReleasePpo(&_pgpt);
        // don't destroy the hwnd - the caller must do that
        _hwnd = hNil;
    }
    if (hwnd != hNil)
    {
        if (pvNil == (_pgpt = GraphicsPort::PgptNewHwnd(hwnd)))
            return fFalse;
        _hwnd = hwnd;
        SetRcFromHwnd();
    }
    return fTrue;
}

/***************************************************************************
    Find the GraphicsObject associated with the given hwnd (if there is one).
***************************************************************************/
PGraphicsObject GraphicsObject::PgobFromHwnd(CoreNative::WindowHandle hwnd)
{
    // NOTE: we used to use SetProp and GetProp for this, but profiling
    // indicated that GetProp is very slow.
    Assert(hwnd != hNil, "nil hwnd");
    GraphicsObjectTreeEnumerator gte;
    ulong grfgte;
    PGraphicsObject pgob;

    gte.Init(_pgobScreen, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (pgob->_hwnd == hwnd)
            return pgob;
    }
    return pvNil;
}
