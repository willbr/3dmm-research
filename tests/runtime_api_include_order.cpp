/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <type_traits>
#include "frame.h"
#if !defined(WIN) && (defined(_WINDOWS_) || defined(_INC_WINDOWS))
#error The portable GUI runtime must not import Windows SDK headers
#endif
static_assert(std::is_same<decltype(&Picture::CbOnFile), LegacyLong (Picture::*)()>::value,
              "picture sizes must match the core cache ABI on LP64");
using PictureReader = bool (*)(PChunkyFile, ChunkTagOrType, ChunkNumber, PDataBlock, PBaseCacheableObject *, LegacyLong *);
static_assert(std::is_same<decltype(&FReadMainPic), PictureReader>::value,
              "picture resource readers must use core-sized output words");
static_assert(sizeof(SystemPoint) == 8 && sizeof(SystemRectangle) == 16,
              "GUI geometry must retain 32-bit coordinates");
static_assert(std::is_same<decltype(&Cursor::FReadCurs), PFNRPO>::value,
              "cursor readers must match the portable resource cache callback");
#ifdef KAUAI_SDL3
static_assert(std::is_same<EVT, KauaiInput::Event>::value, "SDL events must not require native messages");
#endif
