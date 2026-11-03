/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "palette_state.h"
#include <algorithm>
namespace KauaiPixels
{
PaletteChange PaletteState::Set(const Color *requested, std::size_t requestedCount, std::uint32_t flags)
{
    if (!requested && requestedCount)
        return PaletteChange::invalid;
    if (flags & paletteInitAnimation)
        flags &= ~paletteAnimate;
    if (!requestedCount)
    {
        if (flags & paletteAnimate)
            return PaletteChange::unchanged;
        count = 0;
        return PaletteChange::changed;
    }
    const auto usedCount = std::min<std::size_t>(requestedCount, 256);
    // Snapshot borrowed input before changing reserved entries; aliases into
    // this palette are permitted too.
    Color source[256];
    std::copy_n(requested, usedCount, source);
    const auto updatedCount = (flags & paletteAnimate) ? std::min(usedCount, count) : usedCount;
    bool reservedRed[256] = {};
    if (flags & paletteIdentity)
    {
        // BGR order. These are the original Windows truecolor static colors,
        // including the reserved first ten and last ten palette slots.
        static constexpr Color standard[20] = {
            {0, 0, 0, 0},       {0, 0, 128, 0},     {0, 128, 0, 0},     {0, 128, 128, 0},   {128, 0, 0, 0},
            {128, 0, 128, 0},   {128, 128, 0, 0},   {192, 192, 192, 0}, {192, 220, 192, 0}, {240, 202, 166, 0},
            {240, 251, 255, 0}, {164, 160, 160, 0}, {128, 128, 128, 0}, {0, 0, 255, 0},     {0, 255, 0, 0},
            {0, 255, 255, 0},   {255, 0, 0, 0},     {255, 0, 255, 0},   {255, 255, 0, 0},   {255, 255, 255, 0}};
        for (std::size_t i = 0; i < 20; ++i)
        {
            colors[i < 10 ? i : i + 236] = standard[i];
            reservedRed[standard[i].bRed] = true;
        }
    }
    for (std::size_t i = 10; i < std::min<std::size_t>(updatedCount, 246); ++i)
    {
        auto color = source[i];
        color.bZero = 0;
        while (color.bRed && reservedRed[color.bRed])
            --color.bRed;
        while (reservedRed[color.bRed])
            ++color.bRed;
        colors[i] = color;
    }
    count = updatedCount;
    return PaletteChange::changed;
}
} // namespace KauaiPixels
