/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <type_traits>
#include <utility>
#include "gob.h"
#include "gfx.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The graphics-object API must not import a native window SDK
#endif
static_assert(std::is_base_of<CommandHandler, GraphicsObject>::value, "object command interface changed");
static_assert(std::is_same<decltype(std::declval<GraphicsObject &>().Hwnd()), CoreNative::WindowHandle>::value,
              "object window identities must remain opaque interop values");
static_assert(
    std::is_same<decltype(&GraphicsObject::FAttachHwnd), bool (GraphicsObject::*)(CoreNative::WindowHandle)>::value,
    "window attachment must not expose the SDK");
