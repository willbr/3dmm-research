/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_GDI_H
#define KAUAI_FONT_GDI_H
#include "font_host.h"
#include <memory>
namespace KauaiFonts
{
// Private Windows bootstrap. The face clones the font; it never retains a
// borrowed HDC or requires GraphicsPort to draw or measure its text.
std::unique_ptr<Face> CreateGdiFace(void *nativeFont);
std::unique_ptr<Face> CreateStudioFace(void *nativeFont);
} // namespace KauaiFonts
#endif
