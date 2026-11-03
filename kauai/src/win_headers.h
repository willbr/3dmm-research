/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef KAUAI_WIN_HEADERS_H
#define KAUAI_WIN_HEADERS_H
// Legacy native/UI implementation convenience header. Public core entry points
// never include this file. Keep the original SDK compatibility definitions.
#ifdef WIN
#ifndef CONST
#define CONST
#endif
#ifdef UNICODE
#define _UNICODE
#endif
#define PZString PSZS
#include <windows.h>
#include <windowsx.h>
#include <vfw.h>
#undef PZString
#define MIR(foo) MAKEINTRESOURCE(foo)
typedef HBITMAP HBMP;
typedef HENHMETAFILE HPIC;
typedef HPALETTE HPAL;
typedef HCURSOR HCRS;
#define hBadWin INVALID_HANDLE_VALUE
#endif
#endif
