/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_SURFACE_H
#define KAUAI_SDL_SURFACE_H
#include "pixel_canvas.h"
#include "damage_map.h"
#include <cstddef>
namespace KauaiSDL
{
// Exact, non-overlapping 8/32-bit transfers. Unsupported legacy conversions
// return false before writing so the caller can use Kauai's original policy.
bool BlitPixels(const KauaiPixels::PackedSurface &destination, const KauaiPixels::PackedSurface &source,
                const KauaiPixels::DamageRect &sourceRect, const KauaiPixels::DamageRect &destinationRect,
                const KauaiPixels::DamageRect *clips = nullptr, std::size_t clipCount = 0);
} // namespace KauaiSDL
#endif
