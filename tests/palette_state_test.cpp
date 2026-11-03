/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "palette_state.h"
#include <cstdio>
#include <cstring>
using namespace KauaiPixels;
#define CHECK(test)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(test))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "Palette state failed at %d\n", __LINE__);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    PaletteState palette;
    Color source[300];
    for (int i = 0; i < 300; ++i)
        source[i] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i), static_cast<std::uint8_t>(i),
                     231};
    CHECK(palette.Count() == 0);
    CHECK(palette.Set(source, 300, paletteIdentity) == PaletteChange::changed && palette.Count() == 256);
    const unsigned reserved[] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
                                 0xC0C0C0, 0xC0DCC0, 0xA6CAF0, 0xFFFBF0, 0xA0A0A4, 0x808080, 0xFF0000,
                                 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    for (int i = 0; i < 20; ++i)
    {
        const auto color = palette.Colors()[i < 10 ? i : i + 236];
        CHECK(((unsigned(color.bRed) << 16) | (unsigned(color.bGreen) << 8) | color.bBlue) == reserved[i]);
        CHECK(color.bZero == 0);
    }
    for (int i = 10; i < 246; ++i)
    {
        const auto color = palette.Colors()[i];
        const int expected = (i == 128 || i == 160 || i == 166 || i == 192) ? i - 1 : i;
        CHECK(color.bRed == expected && color.bBlue == source[i].bBlue && color.bGreen == source[i].bGreen &&
              !color.bZero);
    }
    for (int i = 10; i < 246; ++i)
        source[i].bRed = (i & 1) ? 0 : 255;
    CHECK(palette.Set(source, 256, paletteIdentity) == PaletteChange::changed);
    for (int i = 10; i < 246; ++i)
        CHECK(palette.Colors()[i].bRed == ((i & 1) ? 1 : 254));
    Color previous[256];
    std::memcpy(previous, palette.Colors(), sizeof(previous));
    CHECK(palette.Set(nullptr, 1, 0) == PaletteChange::invalid && palette.Count() == 256);
    CHECK(std::memcmp(previous, palette.Colors(), sizeof(previous)) == 0);
    CHECK(palette.Set(source, 20, paletteAnimate) == PaletteChange::changed && palette.Count() == 20);
    CHECK(palette.Set(nullptr, 0, paletteAnimate | paletteIdentity) == PaletteChange::unchanged &&
          palette.Count() == 20);
    CHECK(palette.Set(source, 300, paletteInitAnimation | paletteAnimate) == PaletteChange::changed &&
          palette.Count() == 256);
    CHECK(palette.Colors()[10].bRed == 255 && palette.Colors()[11].bRed == 0); // No identity adjustment.
    std::memcpy(previous, palette.Colors(), sizeof(previous));
    CHECK(palette.Set(palette.Colors() + 10, 246, 0) == PaletteChange::changed && palette.Count() == 246);
    for (int i = 10; i < 246; ++i)
        CHECK(std::memcmp(&palette.Colors()[i], &previous[i + 10], sizeof(Color)) == 0);
    CHECK(palette.Set(nullptr, 0, paletteIdentity) == PaletteChange::changed && palette.Count() == 0);
    CHECK(palette.Set(source, 300, paletteAnimate | paletteIdentity) == PaletteChange::changed && palette.Count() == 0);
    std::puts(
        "Palette state: static colors, red collisions, animation, retained entries, aliases and invalid input passed.");
}
