/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_APPB_NATIVE_WIN_H
#define KAUAI_APPB_NATIVE_WIN_H
// Windows runtime/bootstrap state. Opt in through frame.h in GUI/backend
// implementations; the public application interface must not include this.
#include "win_headers.h"
struct WindowsAppGlobals
{
    HINSTANCE hinst;
    HINSTANCE hinstPrev;
    int wShow;
    HWND hwndApp;
    HDC hdcApp;
    HWND hwndClient;
    HACCEL haccel;
    HWND hwndNextViewer;
    long lwThreadMain;
};
extern WindowsAppGlobals vwig;
#endif
