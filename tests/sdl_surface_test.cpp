/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_surface.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>
#include "kauai_core.h"
#include "region.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    std::abort();
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL surface check: %d\n", __LINE__);                                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    using namespace KauaiPixels;
    Color palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = {byte((i % 29) * 7), byte((i % 31) * 8), byte((i % 37) * 6), 0};
    palette[9] = palette[3]; // Equal colors must not collapse distinct indices.
    for (int sourceDepth : {8, 32})
        for (int destinationDepth : {8, 32})
        {
            if (sourceDepth == 32 && destinationDepth == 8)
                continue;
            const int width = 9, height = 7, sourcePitch = width * sourceDepth / 8 + 4;
            const int destinationPitch = width * destinationDepth / 8 + 4;
            std::vector<byte> source(sourcePitch * height), expected(destinationPitch * height, 0xA5),
                actual = expected;
            for (std::size_t i = 0; i < source.size(); ++i)
                source[i] = byte(i * 17 + 3);
            if (sourceDepth == 8)
            {
                source[6] = 3;
                source[7] = 9;
            }
            PackedSurface input{source.data(), width, height, sourcePitch, sourceDepth, palette, 256};
            for (const auto &coordinates : {std::pair<RC, RC>{{0, 0, 9, 7}, {0, 0, 9, 7}},
                                            {{1, 1, 8, 7}, {-2, -1, 5, 5}},
                                            {{1, 1, 8, 6}, {4, 3, 11, 8}}})
            {
                std::fill(expected.begin(), expected.end(), 0xA5);
                actual = expected;
                const auto src = coordinates.first, dst = coordinates.second;
                auto region = Region::PregnNew();
                RC first(0, 1, 4, 6), second(6, 0, 9, 7);
                CHECK(region && region->FUnionRc(&first) && region->FUnionRc(&second));
                PackedSurface reference{expected.data(),  width,   height, destinationPitch,
                                        destinationDepth, palette, 256};
                PackedSurface output{actual.data(), width, height, destinationPitch, destinationDepth, palette, 256};
                CHECK(CopyPacked(reference, input, src, dst, nullptr, region));
                const DamageRect clips[] = {{0, 1, 4, 6}, {6, 0, 9, 7}};
                CHECK(KauaiSDL::BlitPixels(output, input, {src.xpLeft, src.ypTop, src.xpRight, src.ypBottom},
                                           {dst.xpLeft, dst.ypTop, dst.xpRight, dst.ypBottom}, clips, 2));
                CHECK(actual == expected);
                CHECK(!KauaiSDL::BlitPixels(output, output, {0, 0, 9, 7}, {0, 0, 9, 7}));
                CHECK(!KauaiSDL::BlitPixels(output, input, {0, 0, 8, 7}, {0, 0, 9, 7}));
                ReleasePpo(&region);
            }
        }
    std::puts("SDL surface copies: exact colors, unused bits, duplicate indices, clipping and padding passed.");
    return 0;
}
