/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "launch_context.h"
#include "studio_startup.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The POSIX entry adapter must not import the Windows SDK
#endif

// The production main adapter calls this stub; no application/window is created.
void FrameMain()
{
    StudioStartup::Options<char> options;
    if (!KauaiLaunch::HasArguments() || *KauaiLaunch::CommandLine() || KauaiLaunch::ArgumentCount() != 5 ||
        !StudioStartup::ParseArguments(KauaiLaunch::ArgumentCount(), KauaiLaunch::Arguments(), options) ||
        !options.dontMinimize || options.longName != "Movie Maker" || !options.documentIsLiteral || !options.document ||
        std::strcmp(options.document, "/films/my \"cut\".3mm"))
        std::exit(1);
    std::puts("POSIX entry: argv reached FrameMain intact without a window or native SDK.");
}
