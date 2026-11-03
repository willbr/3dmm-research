/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include <type_traits>
#include "soc_core.h"
#include "cmd.h"
#include "pic.h"
#include "gfx.h"
#include "appb.h"
#include "midi_stream_host.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error A clean core include must not import the Windows SDK
#endif
#include "win_headers.h"
#include "midi_stream_win.h"
// Core-first and native-first callers must agree on types and ABI. This check
// needs no fake window, GDI object, application or runtime startup.
static_assert(std::is_same<CoreNative::WindowHandle, HWND>::value, "window interop drift");
static_assert(std::is_same<CoreNative::MessageWord, WPARAM>::value, "message word interop drift");
static_assert(std::is_same<CoreNative::MessageLong, LPARAM>::value, "message long interop drift");
static_assert(std::is_same<CoreNative::MessageResult, LRESULT>::value, "message result interop drift");
static_assert(std::is_same<EVT, MSG>::value, "native event definition drift");
class NativeAppInterop : public ApplicationBase
{
  public:
    using WindowProcedure = decltype(&_LuWndProc);
    using MdiProcedure = decltype(&_LuMdiWndProc);
};
static_assert(std::is_same<NativeAppInterop::WindowProcedure, WNDPROC>::value, "window callback calling convention drift");
static_assert(std::is_same<NativeAppInterop::MdiProcedure, WNDPROC>::value, "MDI callback calling convention drift");
static_assert(std::is_same<CoreNative::RegionHandle, HRGN>::value, "region interop drift");
static_assert(std::is_same<CoreNative::PictureHandle, HENHMETAFILE>::value, "picture interop drift");
static_assert(std::is_same<HN, HGLOBAL>::value, "memory-handle interop drift");
static_assert(std::is_same<SystemRectangle, RECT>::value, "rectangle interop drift");
static_assert(std::is_same<SystemPoint, POINT>::value, "point interop drift");
static_assert(std::is_same<CoreNative::FontHandle, HFONT>::value, "font interop drift");
static_assert(std::is_same<CoreNative::DibDrawHandle, HDRAWDIB>::value, "DIB drawing interop drift");
static_assert(std::is_same<CoreNative::BitmapInfo, BITMAPINFOHEADER>::value, "DIB header interop drift");
static_assert(std::is_same<MH, MIDIHDR>::value, "MIDI headers must use the native pointer-width layout");
static_assert(sizeof(KauaiMidi::StreamEvent) == offsetof(MIDIEVENT, dwParms), "MIDI event prefix ABI drift");
static_assert(offsetof(KauaiMidi::StreamEvent, eventWord) == offsetof(MIDIEVENT, dwEvent), "MIDI event word ABI drift");
static_assert(KauaiMidi::shortMessage == MEVT_SHORTMSG && KauaiMidi::tempo == MEVT_TEMPO &&
              KauaiMidi::noOperation == MEVT_NOP, "MIDI event kind ABI drift");
