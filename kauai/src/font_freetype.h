/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_FREETYPE_H
#define KAUAI_FONT_FREETYPE_H
#include "font_services.h"
#include <memory>
#include <string>
namespace KauaiFonts
{
struct FreeTypeOptions
{
    std::int32_t pixels = 16, faceIndex = 0;
    bool bold = false, italic = false, underline = false;
    bool monochrome = true;
    // A host can preserve legacy cell geometry while changing rasterizers.
    // The advance is always computed from the loaded font's glyph metrics.
    bool useCellMetrics = false;
    Metrics cellMetrics;
    // Derive legacy Windows cell geometry from the font's OS/2 or WinFNT
    // tables. No host font or device context is needed.
    bool legacyCellMetrics = false;
    std::int32_t bitmapScale = 1; // Integer enlargement for legacy bitmap fonts.
};
struct FontInfo
{
    std::string family; // UTF-8 legacy family name (OpenType name ID 1).
    std::int32_t faceIndex = 0, bitmapPixels = 0;
    bool bold = false, italic = false, fixedPitch = false;
};
// Enumerates all faces in an outline collection or bitmap font resource.
bool InspectFreeTypeFont(const std::vector<std::uint8_t> &bytes, std::vector<FontInfo> &faces);
// Owns its font bytes and FreeType state. No SDL, GDI or native font handles.
std::unique_ptr<Face> CreateFreeTypeFace(std::vector<std::uint8_t> bytes, const FreeTypeOptions &options);
} // namespace KauaiFonts
#endif
