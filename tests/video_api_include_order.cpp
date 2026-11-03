/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL3
#define KAUAI_SDL3 1
#endif
#include "video_sdl.h"
#include "gfx.h"
#include "video.h"
#undef size
#include <type_traits>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The video API must not import the Windows SDK
#endif
static_assert(std::is_base_of<Video, VideoStream>::value, "stream video API drift");
static_assert(std::is_base_of<Video, VideoWindow>::value, "window video API drift");
static_assert(sizeof(KauaiVideo::StreamInfo) == 12, "video metadata must retain 32-bit words");
static_assert(std::is_same<decltype(&GraphicsEnvironment::DrawVideoFrame),
                           bool (GraphicsEnvironment::*)(const KauaiPixels::DibFrame &, RC *)>::value,
              "SDL video presentation must accept owned core pixels without native handles");
