/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_MODAL_INTERNAL_H
#define KAUAI_SDL_MODAL_INTERNAL_H
#include "sdl_dialog.h"
struct SDL_Renderer;
namespace KauaiSDL
{
// Private extension for SDL/ImGui host widgets, never exposed by core APIs.
struct ModalView
{
    void *context;
    void (*draw)(void *, KauaiDialogs::Session &, float, DialogState &, SDL_Renderer *);
    void (*release)(void *);
};
bool RunModal(KauaiDialogs::Session &, std::uint32_t, void (*)(void *), void *, ModalView *);
} // namespace KauaiSDL
#endif
