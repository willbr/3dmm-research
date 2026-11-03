/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_STUDIO_H
#define KAUAI_SDL_STUDIO_H
// SDL presentation and damage dispatch for the original Kauai Studio UI.
namespace KauaiSDLBridge
{
void Present();
void Paint();
// Private native-token translation for canvas damage; false selects the
// existing native-window path. Rectangles are borrowed SystemRectangles.
bool Invalidate(void *window, const void *rectangle);
bool Validate(void *window, const void *rectangle);
bool DamageBounds(void *window, void *rectangle);
bool Update(void *window);
bool ScrollDamage(void *window, const void *rectangle, int dx, int dy);
} // namespace KauaiSDLBridge
#endif
