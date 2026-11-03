/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <type_traits>
#include "appb.h"
#include "gob.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The application interface must not import a native window SDK
#endif
static_assert(std::is_base_of<CommandHandler, ApplicationBase>::value, "application command interface drift");
static_assert(std::is_same<decltype(&ApplicationBase::FGetMarkedRc),
                           bool (ApplicationBase::*)(CoreNative::WindowHandle, RC *)>::value,
              "window damage must use opaque window identities and core rectangles");
#ifdef KAUAI_SDL3
static_assert(std::is_same<EVT, KauaiInput::Event>::value, "SDL application events must be host events");
#else
static_assert(std::is_same<EVT, CoreNative::MessageEvent>::value, "native messages must remain opaque");
#endif
