/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PALETTE_STATE_H
#define KAUAI_PALETTE_STATE_H
#include "color.h"
#include <cstddef>
#include <cstdint>
namespace KauaiPixels
{
enum PaletteFlags : std::uint32_t
{
    paletteIdentity = 1,
    paletteInitAnimation = 2,
    paletteAnimate = 4
};
enum class PaletteChange
{
    invalid,
    unchanged,
    changed
};
// Original Studio's truecolor-display palette policy, independent of the host
// display palette. Unspecified entries retain their previous colors.
class PaletteState
{
    Color colors[256] = {};
    std::size_t count = 0;

  public:
    PaletteChange Set(const Color *requested, std::size_t requestedCount, std::uint32_t flags);
    const Color *Colors() const
    {
        return colors;
    }
    std::size_t Count() const
    {
        return count;
    }
};
} // namespace KauaiPixels
#endif
