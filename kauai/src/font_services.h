/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_SERVICES_H
#define KAUAI_FONT_SERVICES_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
class RC;
class Region;
namespace KauaiPixels
{
struct Surface32;
struct PackedSurface;
}
namespace KauaiFonts
{
struct FamilyInfo
{
    std::string name; // UTF-8 legacy family identifier.
    bool fixedPitch = false;
};
enum class Encoding
{
    legacyBytes,
    utf16
};
struct Text
{
    const void *data;
    std::size_t units;
    Encoding encoding = Encoding::legacyBytes;
};
// Decode counted host text to UTF-8, preserving empty strings and embedded
// NULs. Malformed UTF-16 fails; this does not rewrite stored movie strings.
bool TextToUtf8(Text text, std::string &utf8);
struct Metrics
{
    std::int32_t advance = 0, cellHeight = 0, ascent = 0, externalLeading = 0;
};
struct Mask
{
    // Coverage is top-down, one byte per pixel. left/top are relative to the
    // run's left-aligned cell origin, including any glyph overhang.
    std::vector<std::uint8_t> coverage;
    std::vector<std::uint8_t> background; // Optional opaque cell coverage, same bounds/pitch.
    std::int32_t width = 0, height = 0, pitch = 0, left = 0, top = 0;
};
enum class Horizontal
{
    left,
    center,
    right
};
enum class Vertical
{
    top,
    center,
    baseline,
    bottom
};
struct Bounds
{
    std::int32_t left, top, right, bottom;
};
struct Placement
{
    Bounds logical; // Includes external leading, for legacy layout and boxes.
    Bounds cell;    // GDI-compatible opaque background and glyph cell origin.
};
enum class Background
{
    transparent,
    opaque,
    invert
};
struct Image32
{
    std::vector<std::uint32_t> pixels;
    std::int32_t left = 0, top = 0, width = 0, height = 0;
};
class Face
{
  public:
    virtual ~Face() = default;
    virtual bool Measure(Text text, Metrics &metrics) = 0;
    virtual bool Rasterize(Text text, Mask &mask, bool opaqueBackground = false) = 0;
    // Optional color rendering preserves backend smoothing, including LCD
    // blending against existing pixels. Returned image bounds are absolute;
    // the borrowed destination is read-only and remains unchanged on failure.
    // Output image storage must be separate from the borrowed destination.
    virtual bool Paint32(Text, const KauaiPixels::Surface32 &, const Placement &, std::uint32_t, Background,
                         std::uint32_t, Image32 &);
    // Paint and clip into RGB555/BGR24/BGRX32 storage. Failure leaves the
    // destination unchanged. RGB555 requires a Paint32 provider that preserves
    // the high byte for untouched/inverted pixels and clears it for ink writes
    // (the default implementation does). Native GDI RGB555 smoothing is separate.
    bool DrawPacked(Text, const KauaiPixels::PackedSurface &, const Placement &, std::uint32_t, Background,
                    std::uint32_t, const RC *clip = nullptr, Region *region = nullptr);
};
bool Place(const Metrics &metrics, std::int32_t x, std::int32_t y, Horizontal horizontal, Vertical vertical,
           Placement &placement);
} // namespace KauaiFonts
#endif
