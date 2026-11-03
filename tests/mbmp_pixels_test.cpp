/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include <vector>
#include "kauai_core.h"
#include "region.h"
#include "mbmp.h"
#include <cstdio>
#include <cstring>
ASSERTNAME
static bool assertion = false;
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    assertion = true;
    return false;
}
int main()
{
    byte source[] = {1, 2, 0, 4, 5, 0, 7, 8, 9, 10, 11, 12};
    RC bounds(0, 0, 4, 3);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(source, 4, 3, &bounds, 4, 3, 0);
    if (!bitmap)
        return 1;
    bitmap->GetRc(&bounds);
    if (bounds.xpLeft != -4 || bounds.ypTop != -3 || bounds.Dxp() != 4 || bounds.Dyp() != 3)
        return 1;
    byte decoded[12] = {};
    bitmap->Draw(decoded, 4, 3, 4, 3);
    if (std::memcmp(decoded, source, sizeof(source)))
        return 1;
    RC clip(1, 1, 3, 3);
    auto region = Region::PregnNew(&clip);
    std::memset(decoded, 77, sizeof(decoded));
    bitmap->Draw(decoded, 4, 3, 4, 3, nullptr, region);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const int i = y * 4 + x;
            const byte expected = x >= 1 && x < 3 && y >= 1 && source[i] ? source[i] : 77;
            if (decoded[i] != expected)
                return 1;
        }
    byte mask[3] = {};
    bitmap->DrawMask(mask, 1, 3, 4, 3);
    if (mask[0] != 0xd0 || mask[1] != 0xb0 || mask[2] != 0xf0)
        return 1;
    Color palette[256] = {};
    for (int index = 0; index < 256; ++index)
        palette[index] = {static_cast<byte>(index * 3), static_cast<byte>(index * 5), static_cast<byte>(index * 7),
                          255};
    RC target(3, 2, 11, 8), clipping(4, 2, 10, 8), hole(6, 4, 8, 6);
    auto spriteClip = Region::PregnNew(&clipping);
    if (!spriteClip || !spriteClip->FDiffRc(&hole))
        return 1;
    std::uint32_t sprite[15 * 11];
    KauaiPixels::Surface32 surface = {sprite, 13, 11, 15 * 4};
    RC original(0, 0, 4, 3);
    auto solid = MaskedBitmapMBMP::PmbmpNew(source, 4, 3, &original, 4, 3, 0, fmbmpMask, 17);
    if (!solid)
        return 1;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto &pixel : sprite)
            pixel = 0x123456;
        if (!(pass ? solid : bitmap)->FDraw32(surface, target, palette, 256, &clipping, spriteClip))
            return 1;
        for (int y = 0; y < 11; ++y)
            for (int x = 0; x < 15; ++x)
            {
                std::uint32_t expected = 0x123456;
                if (x >= 4 && x < 10 && y >= 2 && y < 8 && !(x >= 6 && x < 8 && y >= 4 && y < 6))
                {
                    const auto index = source[((y - 2) / 2) * 4 + (x - 3) / 2];
                    if (index)
                    {
                        const auto color = palette[pass ? 17 : index];
                        expected = (std::uint32_t(color.bRed) << 16) | (std::uint32_t(color.bGreen) << 8) | color.bBlue;
                    }
                }
                if (sprite[y * 15 + x] != expected)
                    return 1;
            }
    }
    Color gray[256], reversed[256];
    for (int index = 0; index < 256; ++index)
    {
        gray[index] = {static_cast<byte>(index), static_cast<byte>(index), static_cast<byte>(index), 255};
        reversed[index] = {static_cast<byte>(255 - index), static_cast<byte>(255 - index),
                           static_cast<byte>(255 - index), 0};
    }
    KauaiPixels::MaskedTransfer8 transfer;
    if (!KauaiPixels::ResolveMaskedTransfer8(gray, 256, reversed, 256, transfer) ||
        KauaiPixels::ResolveMaskedTransfer8(gray, 0, reversed, 256, transfer))
        return 1;
    byte transferred[15 * 11];
    KauaiPixels::Surface8 indexedTransfer = {transferred, 13, 11, 15};
    for (int pass = 0; pass < 2; ++pass)
    {
        std::memset(transferred, 125, sizeof(transferred));
        if (!(pass ? solid : bitmap)->FDraw8(indexedTransfer, target, transfer, &clipping, spriteClip))
            return 1;
        for (int y = 0; y < 11; ++y)
            for (int x = 0; x < 15; ++x)
            {
                byte expected = 125;
                if (x >= 4 && x < 10 && y >= 2 && y < 8 && !(x >= 6 && x < 8 && y >= 4 && y < 6))
                {
                    const auto index = source[((y - 2) / 2) * 4 + (x - 3) / 2];
                    // Reversed palette: white is index zero, black is 255.
                    // The color pass ANDs the translated indices.
                    expected = index ? static_cast<byte>(125 & (255 - (pass ? 17 : index))) : 0;
                }
                if (transferred[y * 15 + x] != expected)
                    return 1;
            }
    }
    // Entirely transparent encoded rows still participate in a resolved
    // legacy transfer, while the direct masked renderer skips them.
    const std::int16_t emptyRows[3] = {};
    KauaiPixels::RleBitmap8 empty = {nullptr, 0, emptyRows, 4, 3, false, 0};
    std::memset(transferred, 125, sizeof(transferred));
    if (!KauaiPixels::DrawRle(indexedTransfer, empty, target, transfer, &clipping, spriteClip))
        return 1;
    if (transferred[2 * 15 + 4] != 0 || transferred[4 * 15 + 6] != 125 || transferred[15 + 4] != 125)
        return 1;
    const std::uint8_t invalidRun[] = {255};
    const std::int16_t invalidRows[3] = {1, 0, 0};
    empty.data = invalidRun;
    empty.bytes = 1;
    empty.rowBytes = invalidRows;
    std::memset(transferred, 125, sizeof(transferred));
    if (KauaiPixels::DrawRle(indexedTransfer, empty, target, transfer, &clipping, spriteClip))
        return 1;
    for (auto value : transferred)
        if (value != 125)
            return 1;
    // Reduction combines through the selected sample, not through the full
    // source interval. The final column and final row below are unused.
    byte reductionPixels[] = {254, 253, 251, 247, 239, 223, 191, 127, 254, 253, 251, 247, 1, 1, 1, 1, 1, 1};
    RC reductionBounds(0, 0, 6, 3), reducedTarget(3, 2, 5, 3);
    const byte expectedReduction[3][2] = {{65, 29}, {194, 29}, {238, 108}};
    for (int pass = 0; pass < 3; ++pass)
    {
        if (pass == 1)
            reductionPixels[0] = 0;
        auto reduced =
            MaskedBitmapMBMP::PmbmpNew(reductionPixels, 6, 3, &reductionBounds, 0, 0, 0, pass == 2 ? fmbmpMask : 0, 17);
        std::memset(transferred, 125, sizeof(transferred));
        if (!reduced || !reduced->FDraw8(indexedTransfer, reducedTarget, transfer, nullptr, nullptr, true))
            return 1;
        for (int y = 0; y < 11; ++y)
            for (int x = 0; x < 15; ++x)
            {
                const byte expected = y == 2 && x >= 3 && x < 5 ? expectedReduction[pass][x - 3] : 125;
                if (transferred[y * 15 + x] != expected)
                {
                    std::fprintf(stderr, "Reduction pass %d at %d,%d: expected %u, got %u\n", pass, x, y, expected,
                                 transferred[y * 15 + x]);
                    return 1;
                }
            }
        ReleasePpo(&reduced);
    }
    // A near-size copy keeps source samples instead of redistributing them.
    RC nearTarget(0, 0, 3, 2);
    std::memset(transferred, 125, sizeof(transferred));
    if (!bitmap->FDraw8(indexedTransfer, nearTarget, transfer, nullptr, nullptr, true))
        return 1;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
        {
            const byte index = source[y * 4 + x];
            if (transferred[y * 15 + x] != (index ? (125 & (255 - index)) : 0))
                return 1;
        }
    // Reject malformed input before touching a shrinking destination too.
    std::memset(transferred, 125, sizeof(transferred));
    if (KauaiPixels::DrawRle(indexedTransfer, empty, reducedTarget, transfer, nullptr, nullptr, true))
        return 1;
    for (auto value : transferred)
        if (value != 125)
            return 1;
    ReleasePpo(&solid);
    ReleasePpo(&spriteClip);
    // Long transparent and opaque runs use zero-length continuation runs.
    // Exercise both kinds while stretching into an odd-pitch indexed surface.
    std::vector<byte> wideSource(1200, 17), wideDestination(1201 * 4, 77);
    for (int x = 0; x < 600; ++x)
        wideSource[x] = 0;
    wideSource[0] = 1;
    wideSource[599] = 2;
    RC wideBounds(0, 0, 600, 2), wideTarget(0, 0, 1200, 4);
    auto wide = MaskedBitmapMBMP::PmbmpNew(wideSource.data(), 600, 2, &wideBounds, 0, 0, 0);
    if (!wide)
        return 1;
    KauaiPixels::Surface8 indexed = {wideDestination.data(), 1200, 4, 1201};
    if (!wide->FDraw8(indexed, wideTarget))
        return 1;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 1201; ++x)
        {
            const byte expected = x == 1200 ? 77 : y >= 2 ? 17 : x < 2 ? 1 : x >= 1198 ? 2 : 77;
            if (wideDestination[y * 1201 + x] != expected)
                return 1;
        }
    ReleasePpo(&wide);
    ReleasePpo(&region);
    ReleasePpo(&bitmap);
    std::puts("Core bitmap pixels: reference point, transparency, region clipping and mask passed.");
    return assertion ? 1 : 0;
}
