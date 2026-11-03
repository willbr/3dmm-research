/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <SDL3/SDL_events.h>
bool SendExternalClipboardEvent()
{
    SDL_Event changed = {};
    changed.type = SDL_EVENT_CLIPBOARD_UPDATE;
    changed.clipboard.owner = false;
    return SDL_PushEvent(&changed);
}
