/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <SDL3/SDL.h>
bool SendFocusEvent(bool active)
{
    int count = 0;
    auto windows = SDL_GetWindows(&count);
    const auto window = count == 1 ? windows[0] : nullptr;
    SDL_free(windows);
    if (!window)
        return false;
    SDL_Event event = {};
    event.type = active ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = SDL_GetWindowID(window);
    return SDL_PushEvent(&event);
}
bool SendControlKey()
{
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_LCTRL;
    event.key.mod = SDL_KMOD_LCTRL;
    return SDL_PushEvent(&event);
}
bool SendLetterKey(char letter, unsigned modifiers)
{
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + letter - 'A');
    event.key.mod =
        static_cast<SDL_Keymod>(((modifiers & 1) ? SDL_KMOD_LSHIFT : 0) | ((modifiers & 2) ? SDL_KMOD_LCTRL : 0) |
                                ((modifiers & 4) ? SDL_KMOD_LALT : 0));
    return SDL_PushEvent(&event);
}
