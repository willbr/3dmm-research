/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_services.h"
#include "font_names.h"
#include "pixel_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kauai_core.h"
#include "region.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *) {}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong) { std::abort(); }
class TestFace : public KauaiFonts::Face
{
  public:
    bool invalid = false;
    bool Measure(KauaiFonts::Text, KauaiFonts::Metrics &metrics) override
    {
        metrics = {2, 1, 1, 0};
        return true;
    }
    bool Rasterize(KauaiFonts::Text, KauaiFonts::Mask &mask, bool) override
    {
        mask = {};
        mask.width = mask.pitch = invalid ? 10 : 2;
        mask.height = 1;
        mask.coverage = {128, 255};
        return true;
    }
};
int main()
{
    using namespace KauaiFonts;
    for (const std::string name : {"Arial", "Caf\xC3\xA9", "\xE6\xBC\xA2", "\xF0\x9F\x8E\xAC"})
    {
        std::vector<std::uint8_t> encoded;
        std::string restored;
        if (!NameFromUtf8(name, Encoding::utf16, encoded) ||
            !NameToUtf8({encoded.data(), encoded.size() / 2, Encoding::utf16}, restored) || restored != name)
            return 1;
        if (NameFromUtf8(name, Encoding::legacyBytes, encoded) &&
            (!NameToUtf8({encoded.data(), encoded.size()}, restored) || restored != name))
            return 1;
    }
    for (const std::string invalid : {"", "\x80", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF0\x80\x80\xAF",
                                      "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xE2\x82", "\xE2 A"})
    {
        std::vector<std::uint8_t> encoded = {1, 2};
        if (NameFromUtf8(invalid, Encoding::utf16, encoded) || !encoded.empty())
            return 1;
    }
    std::vector<std::uint8_t> encoded;
    std::string restored;
    const char16_t invalidUnits[] = {0xD800, 'A'};
    const char embedded[] = {'A', 0, 'B'};
    if (!TextToUtf8({embedded, 3}, restored) || restored != std::string(embedded, 3) ||
        !TextToUtf8({nullptr, 0}, restored) || !restored.empty())
        return 1;
    if (NameToUtf8({invalidUnits, 2, Encoding::utf16}, restored) || !restored.empty() ||
        NameToUtf8({embedded, 3}, restored) || NameToUtf8({nullptr, 1}, restored) ||
        NameFromUtf8(std::string(embedded, 3), Encoding::utf16, encoded))
        return 1;
    const Metrics metrics = {21, 12, 9, 3};
    Placement placement;
    if (!Place(metrics, 30, 40, Horizontal::center, Vertical::baseline, placement) || placement.logical.left != 20 ||
        placement.logical.right != 41 || placement.logical.top != 31 || placement.logical.bottom != 46 ||
        placement.cell.bottom != 43 || placement.cell.left != 19)
        return 1;
    if (!Place(metrics, 30, 40, Horizontal::right, Vertical::bottom, placement) || placement.logical.left != 9 ||
        placement.logical.top != 25 || placement.logical.bottom != 40 || placement.cell.top != 28 ||
        placement.cell.bottom != 40)
        return 1;
    if (Place(metrics, INT32_MAX, 0, Horizontal::left, Vertical::top, placement) ||
        Place({21, 12, 13, 0}, 0, 0, Horizontal::left, Vertical::top, placement))
        return 1;
    TestFace face;
    std::uint32_t pixels[12];
    for (auto &pixel : pixels)
        pixel = 0x112233;
    KauaiPixels::Surface32 surface = {pixels, 4, 3, 16};
    Placement glyph;
    if (!Place({2, 1, 1, 0}, 1, 1, Horizontal::left, Vertical::top, glyph))
        return 1;
    Image32 image;
    if (!face.Paint32({"A", 1}, surface, glyph, 0xFF0000, Background::transparent, 0, image) || image.left != 1 ||
        image.top != 1 || image.width != 2 || image.height != 1 || image.pixels[0] != 0x881119 ||
        image.pixels[1] != 0xFF0000)
        return 1;
    if (!face.Paint32({"A", 1}, surface, glyph, 0xFF0000, Background::opaque, 0xFF, image) ||
        image.pixels[0] != 0x80007F)
        return 1;
    if (!face.Paint32({"A", 1}, surface, glyph, 0xFF0000, Background::invert, 0, image) || image.pixels[0] != 0xF76E66)
        return 1;
    // Frozen half-coverage red over white: preserve untouched packed samples,
    // clear RGB555's unused bit for ink and retain row padding/clip holes.
    for (int depth : {16, 24, 32})
    {
        const int pitch = depth == 16 ? 12 : depth == 24 ? 16 : 20;
        std::vector<std::uint8_t> packed(pitch * 3, 0xFF);
        auto before = packed;
        KauaiPixels::PackedSurface target{packed.data(), 4, 3, pitch, depth};
        RC clip(0, 0, 4, 3), hole(2, 1, 3, 2);
        auto region = Region::PregnNew(&clip);
        if (!region || !region->FDiffRc(&hole))
            return 1;
        if (!face.DrawPacked({"A", 1}, target, glyph, 0xFF0000, Background::transparent, 0, &clip, region))
            return 1;
        const auto offset = pitch + depth / 8;
        if (depth == 16)
        {
            before[offset] = 0xEF;
            before[offset + 1] = 0x7D;
        }
        else
        {
            before[offset] = 0x7F;
            before[offset + 1] = 0x7F;
            before[offset + 2] = 0xFF;
            if (depth == 32)
                before[offset + 3] = 0;
        }
        if (packed != before)
            return 1;
        face.invalid = true;
        if (face.DrawPacked({"A", 1}, target, glyph, 0, Background::opaque, 0, &clip, region) || packed != before)
            return 1;
        face.invalid = false;
        target.depth = 4;
        if (face.DrawPacked({"A", 1}, target, glyph, 0, Background::transparent, 0) || packed != before)
            return 1;
        ReleasePpo(&region);
    }
    face.invalid = true;
    if (face.Paint32({"A", 1}, surface, glyph, 0, Background::transparent, 0, image))
        return 1;
    for (auto pixel : pixels)
        if (pixel != 0x112233)
            return 1;
    std::puts("Font layout: alignment, baseline, external leading and overflow passed.");
    return 0;
}
