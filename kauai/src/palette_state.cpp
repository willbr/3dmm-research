/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "palette_state.h"
#include "mm/indexed_display.h"

namespace KauaiPixels
{
PaletteChange PaletteState::Set(const Color *requested, std::size_t requestedCount, std::uint32_t flags)
{
    static_assert(sizeof(Color) == 4, "Display palette layout");
    static_assert(paletteIdentity == MM_DISPLAY_PALETTE_IDENTITY &&
                      paletteInitAnimation == MM_DISPLAY_PALETTE_INIT_ANIMATION &&
                      paletteAnimate == MM_DISPLAY_PALETTE_ANIMATE,
                  "Display palette flags");
    static_assert(int(PaletteChange::invalid) == MM_DISPLAY_PALETTE_INVALID &&
                      int(PaletteChange::unchanged) == MM_DISPLAY_PALETTE_UNCHANGED &&
                      int(PaletteChange::changed) == MM_DISPLAY_PALETTE_CHANGED,
                  "Display palette results");
    return static_cast<PaletteChange>(mm_display_palette_set(reinterpret_cast<unsigned char *>(colors), &count,
                                                            reinterpret_cast<const unsigned char *>(requested),
                                                            requestedCount, flags));
}
} // namespace KauaiPixels
