/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PIXEL_CANVAS_H
#define KAUAI_PIXEL_CANVAS_H
#include <cstdint>
#include <cstddef>
#include "color.h"
class RC;
class Region;

namespace KauaiPixels
{
struct DibFrame;
struct Surface32;
// Legacy bottom-up true-color DIB stretch policy. 24-bit input uses edge
// sampling measured from the bottom; 32-bit input uses pixel-center sampling.
// Input is the decoded top-down BGRX32 frame. No native objects are required.
bool DrawDibFrame(const Surface32 &destination, const DibFrame &frame, const RC &rectangle, const RC *clip = nullptr,
                  Region *region = nullptr);
// Borrowed top-down XRGB8888 storage. Pitch includes any row padding.
struct Surface32
{
    void *pixels;
    std::int32_t width, height, pitch;
};
struct Surface8
{
    void *pixels;
    std::int32_t width, height, pitch;
};
// Top-down DIB-compatible packing: MSB-first 1/4-bit indices, 8-bit indices,
// little-endian RGB555, BGR24 or BGRX32. Palette reserved bytes are ignored.
struct PackedSurface
{
    void *pixels;
    std::int32_t width, height, pitch, depth;
    const Color *palette = nullptr;
    std::int32_t colors = 0;
};
struct PackedCopyOptions
{
    // Legacy direct sampling when both dimension differences are at most one.
    bool nearSizeCopy = true;
};
// COLORONCOLOR sampling, clipping, format conversion and overlap-safe self-copy.
// Near-size true-color-to-8-bit copies return false without writing: the
// legacy host's approximate palette policy still requires a native adapter.
// Same-format true-color copies retain unused bits. Indexed formats use their
// color tables; an omitted monochrome table defaults to black and white.
bool CopyPacked(const PackedSurface &destination, const PackedSurface &source, const RC &sourceRect,
                const RC &destinationRect, const RC *clip = nullptr, Region *region = nullptr,
                const PackedCopyOptions &options = PackedCopyOptions{});
enum class InkMode
{
    color,
    clear,
    invert
};
struct Ink
{
    std::uint32_t rgb = 0;
    InkMode mode = InkMode::color;
};
struct Brush
{
    Ink foreground, background;
    // Most-significant bit is the leftmost pixel. Pattern origin is (0,0)
    // in the surface, independent of shape origin and clipping.
    std::uint8_t pattern[8] = {255, 255, 255, 255, 255, 255, 255, 255};
};
struct Ink8
{
    // An indexed raster operation is (old & andMask) ^ xorMask. This also
    // preserves legacy palette-dependent Boolean brush operations.
    std::uint8_t andMask = 0, xorMask = 0;
    static Ink8 Color(std::uint8_t index)
    {
        return {0, index};
    }
    static Ink8 Clear()
    {
        return {255, 0};
    }
    static Ink8 Invert()
    {
        return {255, 255};
    }
};
struct Brush8
{
    Ink8 foreground, background;
    std::uint8_t pattern[8] = {255, 255, 255, 255, 255, 255, 255, 255};
};
// Resolve Kauai's legacy fill/Boolean brush semantics against a destination
// palette. Highlight rectangles use a raw destination inversion instead.
bool ResolveBrush8(const Brush &brush, const Color *palette, std::int32_t colors, Brush8 &indexed,
                   bool highlight = false);
void FillRect(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1);
void FillRect(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1);
// Legacy filled oval geometry differs from the XOR of two elliptic regions
// used for a frame. Invalid/oversized geometry fails before any pixels change.
bool FillOval(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1);
bool FillOval(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1);
struct Point
{
    std::int32_t x, y;
};
struct Coverage8
{
    const std::uint8_t *pixels;
    std::int32_t width, height, pitch;
};
void DrawCoverage(const Surface32 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rgb, const RC *clip = nullptr, Region *region = nullptr);
// Indexed glyphs use monochrome coverage; antialiased masks are thresholded.
void DrawCoverage(const Surface8 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint8_t index, const RC *clip = nullptr, Region *region = nullptr);
// Binary coverage into any packed format. Samples >=128 replace the stored
// pixel with rawPixel; no palette conversion or alpha blending is performed.
// Padding and neighboring packed pixels are preserved. No palette is needed.
bool DrawCoverage(const PackedSurface &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rawPixel, const RC *clip = nullptr, Region *region = nullptr);
// Closed polygon with nonzero winding. Horizontal edges and the last scanline
// are excluded, matching Kauai's filled polygon/outlined line representation.
bool FillPolygon(const Surface32 &surface, const Point *points, std::size_t count, const Brush &brush,
                 const RC *clip = nullptr, Region *region = nullptr);
bool FillPolygon(const Surface8 &surface, const Point *points, std::size_t count, const Brush8 &brush,
                 const RC *clip = nullptr, Region *region = nullptr);
// State needed for the original solid monochrome color conversion. Reference
// tokens identify the origin of a legacy color; only equality is significant.
// The rasterizer never interprets them as platform flags or native objects.
struct MonochromeBrushState
{
    std::uint32_t backgroundRgb = 0xFFFFFF;
    std::uint8_t backgroundReference = 0, foregroundReference = 0, alternateReference = 0;
    // Hosts disable this when distinct source colors resolve to equal RGB.
    bool collapseEqualColors = true;
};
// Resolve a monochrome drawing color using explicit background/reference
// state. An optional palette contains exactly two entries; null is black/white.
std::uint8_t ResolveMonochromeColor(std::uint32_t rgb, std::uint8_t reference,
                                    const MonochromeBrushState &state, const Color *palette = nullptr);
// Monochrome solid, 4-bit indexed, RGB555 and BGR24 shape drawing.
// packed row padding and legacy RGB555 unused-bit handling. Unsupported formats fail
// before writing; 8/32-bit callers use the specialized surface overloads.
// Monochrome accepts solid, constant-pattern and equal-source-color brushes;
// mixed two-color pattern realization fails transactionally for host fallback.
bool FillRect(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1,
              const MonochromeBrushState &monochrome = MonochromeBrushState{});
bool FillOval(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip = nullptr,
              Region *region = nullptr, std::int32_t frameX = -1, std::int32_t frameY = -1,
              const MonochromeBrushState &monochrome = MonochromeBrushState{});
bool FillPolygon(const PackedSurface &surface, const Point *points, std::size_t count, const Brush &brush,
                 const RC *clip = nullptr, Region *region = nullptr,
                 const MonochromeBrushState &monochrome = MonochromeBrushState{});
// Highlight inversion also supports monochrome; it toggles every selected
// stored bit, including RGB555's unused bit. No palette is required.
// Brush inversion instead XORs the packed representation of white.
bool InvertRect(const PackedSurface &surface, const RC &rectangle, const RC *clip = nullptr, Region *region = nullptr);
// Nearest-neighbor stretch, destination clipping and overlap-safe self-copy.
// Source samples outside its surface are left untouched in the destination.
bool Copy(const Surface32 &destination, const Surface32 &source, const RC &sourceRect, const RC &destinationRect,
          const RC *clip = nullptr, Region *region = nullptr);
// Move existing pixels inside a scroll rectangle. Exposed cells and padding
// are left untouched. Both source and destination must be within the effective
// clipping rectangle/region, matching legacy ScrollDC. Clipping does not change
// the sampling origin. Empty rectangles/large displacements are successful
// no-ops; inverted rectangles fail without writing.
bool Scroll(const Surface32 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip = nullptr,
            Region *region = nullptr);
bool Scroll(const Surface8 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip = nullptr,
            Region *region = nullptr);
struct PackedScrollOptions
{
    // Original leftward monochrome self-blits process clip bands in move
    // order, copying full bytes before masked edges. Edges may read the moved
    // body rather than old pixels. Other formats use snapshot semantics.
    bool legacyMonochromeEdges = false;
};
// Raw samples in all six packed formats; no palette is needed. Includes
// RGB555's unused bit and preserves unused bits in partial final bytes.
bool Scroll(const PackedSurface &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy,
            const RC *clip = nullptr, Region *region = nullptr,
            const PackedScrollOptions &options = PackedScrollOptions{});
struct Indexed8
{
    const void *pixels;
    std::int32_t width, height, pitch;
    const Color *palette;
    std::int32_t colors;
};
// Source and destination storage must not overlap. Palette entries use the
// legacy BGR color layout; the reserved byte never becomes an alpha channel.
bool CopyIndexed(const Surface32 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const RC *clip = nullptr, Region *region = nullptr);
// Indexed-to-indexed copies translate palettes, preserving raw indices when
// the full palettes match (including duplicate colors). Supports overlapping
// storage and GDI COLORONCOLOR's near-size copy policy. Out-of-bounds samples
// leave destination pixels unchanged. Palette storage must not overlap pixels.
bool CopyIndexed(const Surface8 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const Color *palette, std::int32_t colors, const RC *clip = nullptr, Region *region = nullptr);
// Native-endian row lengths followed by Kauai skip/run indexed pixel data.
// A solid bitmap omits the literal pixels and uses fillIndex for every run.
struct RleBitmap8
{
    const std::uint8_t *data;
    std::size_t bytes;
    const std::int16_t *rowBytes;
    std::int32_t width, height;
    bool solid;
    std::uint8_t fillIndex;
};
// Native mask-OR/color-AND transfer into monochrome, 4-bit indexed, RGB555 or BGR24 storage, including
// indexed BLACKONWHITE shrink reduction before color translation.
bool DrawRle(const PackedSurface &destination, const RleBitmap8 &source, const RC &destinationRect,
             const Color *palette, std::int32_t colors, const RC *clip = nullptr,
             Region *region = nullptr, bool andShrink = false);
struct MaskedTransfer8
{
    std::uint8_t colorIndices[256] = {};
    std::uint8_t maskWhite = 255, maskBlack = 0, backgroundIndex = 255;
};
// Reproduces the legacy mask-OR/color-AND transfer through two palettes.
// Unlike direct indexed drawing, a private palette can make transparent cells
// affect the destination. Unspecified source palette entries are black.
bool ResolveMaskedTransfer8(const Color *source, std::int32_t sourceColors, const Color *destination,
                            std::int32_t destinationColors, MaskedTransfer8 &transfer);
// Validates the entire encoded image before drawing. Borrowed source data,
// row lengths and palette must not overlap destination storage. Transparent
// cells leave destination pixels unchanged in the direct variants, including on
// non-integral stretches. A resolved transfer instead applies both Boolean
// passes to every pixel. Transfer tables must not overlap destination storage.
// Resolved transfers retain GDI's near-size copy behavior. andShrink selects
// its BLACKONWHITE reduction of raw source indices/masks before translation;
// otherwise it selects COLORONCOLOR sampling. No native objects are needed.
bool DrawRle(const Surface32 &destination, const RleBitmap8 &source, const RC &destinationRect, const Color *palette,
             std::int32_t colors, const RC *clip = nullptr, Region *region = nullptr);
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect, const RC *clip = nullptr,
             Region *region = nullptr);
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect,
             const MaskedTransfer8 &transfer, const RC *clip = nullptr, Region *region = nullptr,
             bool andShrink = false);
} // namespace KauaiPixels
#endif
