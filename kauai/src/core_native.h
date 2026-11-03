/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef CORE_NATIVE_H
#define CORE_NATIVE_H
#include <cstdint>

// Forward declarations preserve the existing Windows ABI without importing the
// SDK. These are opaque interop values, never serialized movie data. Only a
// native adapter may dereference them or submit them to the operating system.
#ifdef _WIN32
struct HWND__;
struct HRGN__;
struct HENHMETAFILE__;
struct HFONT__;
struct tagBITMAPINFOHEADER;
struct tagRECT;
struct tagPOINT;
struct tagMSG;
namespace CoreNative
{
using WindowHandle = ::HWND__ *;
using RegionHandle = ::HRGN__ *;
using PictureHandle = ::HENHMETAFILE__ *;
using FontHandle = ::HFONT__ *;
using BitmapInfo = ::tagBITMAPINFOHEADER;
using Rectangle = ::tagRECT;
using Point = ::tagPOINT;
using MessageEvent = ::tagMSG;
} // namespace CoreNative
#else
namespace CoreNative
{
struct Window;
struct Region;
struct Picture;
struct Font;
struct BitmapInfo;
struct MessageEvent;
using WindowHandle = Window *;
using RegionHandle = Region *;
using PictureHandle = Picture *;
using FontHandle = Font *;
struct Rectangle
{
    std::int32_t left, top, right, bottom;
};
struct Point
{
    std::int32_t x, y;
};
} // namespace CoreNative
#endif
namespace CoreNative
{
using MemoryHandle = void *;
using DibDrawHandle = void *;
using MessageWord = std::uintptr_t;
// Windows' LONG_PTR is long on x86, whereas MSVC's intptr_t is int. Keep
// native virtual overrides and callback type identity, not only their size.
#if defined(_WIN32) && !defined(_WIN64)
using MessageResult = long;
#else
using MessageResult = std::intptr_t;
#endif
using MessageLong = MessageResult;
}
// Compiler calling convention for the optional native callback adapter. No
// SDK macro is needed by public application declarations.
#if defined(_WIN32) && defined(_MSC_VER)
#define KAUAI_NATIVE_CALLBACK __stdcall
#elif defined(_WIN32) && defined(__i386__)
#define KAUAI_NATIVE_CALLBACK __attribute__((stdcall))
#else
#define KAUAI_NATIVE_CALLBACK
#endif
#endif
