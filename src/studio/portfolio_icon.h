/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PORTFOLIO_ICON_H
#define STUDIO_PORTFOLIO_ICON_H
#include "sdl_file_dialog.h"
namespace StudioPortfolio
{
inline void DocumentIcon(KauaiSDL::DialogImage &image)
{
    // Original 16x16 studio/bmp/doc.ico, including its transparent AND mask.
    static const std::uint32_t palette[] = {0x00000000, 0xff000000, 0xff000080, 0xff008080,
                                            0xff800080, 0xff808080, 0xffc0c0c0, 0xffff00ff};
    static const char pixels[] = "0000000001100000"
                                 "0000000013311000"
                                 "0011110013333100"
                                 "0174411013113310"
                                 "1741441013111331"
                                 "1425141113101131"
                                 "1100141113100131"
                                 "0001744116565131"
                                 "0017444455115511"
                                 "0001114655565551"
                                 "0000014511555111"
                                 "0001214665515561"
                                 "0017174555555551"
                                 "0174441651151151"
                                 "0011110565655610"
                                 "0000000001111100";
    image.width = image.height = 16;
    image.pixels.resize(256);
    for (unsigned i = 0; i < 256; ++i)
        image.pixels[i] = palette[pixels[i] - '0'];
}
} // namespace StudioPortfolio
#endif
