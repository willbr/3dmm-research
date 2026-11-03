/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "launch_context.h"
#include "frame.h"

// Platform entry adapter for Kauai applications. Keep OS bootstrap separate
// from ApplicationBase so a future SDL/POSIX entry can call FrameMain directly.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR, int show)
{
    const auto text = GetCommandLine();
    static_assert(sizeof(*text) == sizeof(KauaiLaunch::NativeChar), "command-line character width mismatch");
    if (!KauaiLaunch::SetCommandLine(reinterpret_cast<const KauaiLaunch::NativeChar *>(text)))
        return 1;
    vwig.hinst = instance;
    vwig.hinstPrev = previous;
    vwig.wShow = show;
    vwig.lwThreadMain = LwThreadCur();
#ifdef DEBUG
    // Preserve inherited output pipes, including Studio's MCP transport.
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_PIPE)
        ApplicationBase::CreateConsole();
#endif
    FrameMain();
    return 0;
}
