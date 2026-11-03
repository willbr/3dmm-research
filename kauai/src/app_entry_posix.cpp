/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "launch_context.h"

// SDL host services perform their own initialization when the application
// creates them. This entry adapter needs no window or platform SDK.
void FrameMain();
int main(int argc, char **argv)
{
    if (!KauaiLaunch::SetArguments(argc, argv))
        return 1;
    FrameMain();
    return 0;
}
