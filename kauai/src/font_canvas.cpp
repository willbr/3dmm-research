/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_services.h"
#include "pixel_canvas.h"
#include "pixel_buffer.h"
#include "kauai_core.h"
#undef size
ASSERTNAME
namespace KauaiFonts
{
bool Face::DrawPacked(Text text, const KauaiPixels::PackedSurface &destination, const Placement &placement,
                      std::uint32_t foreground, Background background, std::uint32_t backgroundColor, const RC *clip,
                      Region *region)
{
    if (destination.depth != 16 && destination.depth != 24 && destination.depth != 32)
        return false;
    KauaiPixels::Surface32 surface = {destination.pixels, destination.width, destination.height, destination.pitch};
    // Preserve color-aware font smoothing by painting against a lossless
    // RGB snapshot. Native-free packing is applied only after painting
    // succeeds, so a failed provider leaves the destination untouched.
    KauaiPixels::PixelBuffer snapshot;
    KauaiPixels::PackedSurface packed = {destination.pixels, destination.width, destination.height, destination.pitch,
                                         destination.depth};
    if (destination.depth != 32)
    {
        if (!snapshot.Init(destination.width, destination.height, 32))
            return false;
        KauaiPixels::PackedSurface expanded = {snapshot.Pixels(), snapshot.Width(), snapshot.Height(), snapshot.Pitch(),
                                               32};
        RC bounds(0, 0, destination.width, destination.height);
        if (!KauaiPixels::CopyPacked(expanded, packed, bounds, bounds))
            return false;
        // FreeType's color renderer preserves the high byte on untouched
        // pixels and clears it when writing coverage. Carry RGB555's
        // unused bit there so untouched and inverted cells retain it.
        if (destination.depth == 16)
            for (int y = 0; y < snapshot.Height(); ++y)
                for (int x = 0; x < snapshot.Width(); ++x)
                {
                    const auto row = static_cast<const std::uint8_t *>(destination.pixels) + y * destination.pitch;
                    auto pixels = static_cast<std::uint32_t *>(snapshot.Pixels());
                    pixels[y * snapshot.Width() + x] |= std::uint32_t(row[x * 2 + 1] & 0x80) << 24;
                }
        surface = {snapshot.Pixels(), snapshot.Width(), snapshot.Height(), snapshot.Pitch()};
    }
    Image32 image;
    if (!Paint32(text, surface, placement, foreground, background, backgroundColor, image))
        return false;
    if (image.width < 0 || image.height < 0 || image.width > INT32_MAX / 4 ||
        std::uint64_t(image.width) * image.height > image.pixels.size() ||
        std::int64_t(image.left) + image.width > INT32_MAX || std::int64_t(image.top) + image.height > INT32_MAX)
        return false;
    if (image.width && image.height)
    {
        KauaiPixels::Surface32 source = {image.pixels.data(), image.width, image.height, image.width * 4};
        RC sourceBounds(0, 0, image.width, image.height),
            target(image.left, image.top, image.left + image.width, image.top + image.height);
        if (destination.depth != 32)
        {
            KauaiPixels::PackedSurface input = {image.pixels.data(), image.width, image.height, image.width * 4, 32};
            KauaiPixels::PixelBuffer words;
            if (destination.depth == 16)
            {
                if (!words.Init(image.width, image.height, 16))
                    return false;
                for (int y = 0; y < image.height; ++y)
                    for (int x = 0; x < image.width; ++x)
                    {
                        const auto rgb = image.pixels[y * image.width + x];
                        const auto raw =
                            ((rgb >> 9) & 0x7C00) | ((rgb >> 6) & 0x3E0) | ((rgb >> 3) & 0x1F) | ((rgb >> 16) & 0x8000);
                        auto pixel = static_cast<std::uint8_t *>(words.Pixels()) + y * words.Pitch() + x * 2;
                        pixel[0] = static_cast<std::uint8_t>(raw);
                        pixel[1] = static_cast<std::uint8_t>(raw >> 8);
                    }
                input = {words.Pixels(), image.width, image.height, words.Pitch(), 16};
            }
            if (!KauaiPixels::CopyPacked(packed, input, sourceBounds, target, clip, region))
                return false;
        }
        else if (!KauaiPixels::Copy(surface, source, sourceBounds, target, clip, region))
            return false;
    }
    return true;
}
} // namespace KauaiFonts
