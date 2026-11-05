/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Adapter for ImGui 1.92's incremental font atlas. Uses the same glyph and
// encoding service as Studio, including WinFNT bitmap faces and DPI scaling.
#ifndef KAUAI_SDL_DIALOG_FONT_H
#define KAUAI_SDL_DIALOG_FONT_H
#include "font_host.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstring>
namespace KauaiSDL
{
namespace DialogFont
{
// 8 points at 96 DPI is an 11-pixel character height (excluding internal
// leading), carried by a 13-pixel MS Sans Serif bitmap cell.
constexpr int Pixels = 11;
constexpr int CellPixels = 13;
struct Source
{
    std::unique_ptr<KauaiFonts::Face> probe;
};
struct Baked
{
    std::unique_ptr<KauaiFonts::Face> face;
    KauaiFonts::Metrics metrics;
};
inline std::unique_ptr<KauaiFonts::Face> Open(ImFontConfig *source)
{
    return KauaiFonts::CreateStudioFace(
        {source->FontData, std::strlen(static_cast<const char *>(source->FontData))},
        ImMax(1, static_cast<int>(std::round(source->SizePixels * Pixels / CellPixels))), source->FontLoaderFlags != 0,
        false, false);
}
inline KauaiFonts::Text Character(ImWchar codepoint, std::uint16_t (&units)[2])
{
    units[0] = static_cast<std::uint16_t>(codepoint);
    if (codepoint <= 0xffff)
        return {units, 1, KauaiFonts::Encoding::utf16};
    units[0] = static_cast<std::uint16_t>(0xd800 + ((codepoint - 0x10000) >> 10));
    units[1] = static_cast<std::uint16_t>(0xdc00 + ((codepoint - 0x10000) & 0x3ff));
    return {units, 2, KauaiFonts::Encoding::utf16};
}
inline const ImFontLoader *Loader()
{
    static const ImFontLoader loader = [] {
        ImFontLoader result;
        result.Name = "Kauai monochrome/WinFNT";
        result.FontSrcInit = [](ImFontAtlas *, ImFontConfig *source) {
            auto data = IM_NEW(Source)();
            data->probe = Open(source);
            source->FontLoaderData = data;
            return data->probe != nullptr;
        };
        result.FontSrcDestroy = [](ImFontAtlas *, ImFontConfig *source) {
            IM_DELETE(static_cast<Source *>(source->FontLoaderData));
            source->FontLoaderData = nullptr;
        };
        result.FontSrcContainsGlyph = [](ImFontAtlas *, ImFontConfig *source, ImWchar codepoint) {
            std::uint16_t units[2];
            KauaiFonts::Metrics metrics;
            return static_cast<Source *>(source->FontLoaderData)->probe->Measure(Character(codepoint, units), metrics);
        };
        result.FontBakedSrcLoaderDataSize = sizeof(Baked);
        result.FontBakedInit = [](ImFontAtlas *, ImFontConfig *source, ImFontBaked *font, void *memory) {
            auto data = IM_PLACEMENT_NEW(memory) Baked();
            data->face = Open(source);
            if (!data->face || !data->face->Measure({"Ag", 2}, data->metrics) || data->metrics.cellHeight <= 0)
                return false;
            const float scale = font->Size / data->metrics.cellHeight;
            font->Ascent = std::round(data->metrics.ascent * scale);
            font->Descent = -std::round((data->metrics.cellHeight - data->metrics.ascent) * scale);
            return true;
        };
        result.FontBakedDestroy = [](ImFontAtlas *, ImFontConfig *, ImFontBaked *, void *memory) {
            static_cast<Baked *>(memory)->~Baked();
        };
        result.FontBakedLoadGlyph = [](ImFontAtlas *atlas, ImFontConfig *source, ImFontBaked *font, void *memory,
                                       ImWchar codepoint, ImFontGlyph *glyph, float *advance) {
            auto &data = *static_cast<Baked *>(memory);
            std::uint16_t units[2];
            const auto text = Character(codepoint, units);
            KauaiFonts::Metrics metrics;
            if (!data.face->Measure(text, metrics))
                return false;
            const float scale = font->Size / data.metrics.cellHeight;
            const float width = std::round(metrics.advance * scale);
            if (advance)
            {
                *advance = width;
                return true;
            }
            KauaiFonts::Mask mask;
            if (!data.face->Rasterize(text, mask, false))
                return false;
            glyph->Codepoint = codepoint;
            glyph->AdvanceX = width;
            if (!mask.width || !mask.height)
                return true;
            const float density = source->RasterizerDensity * font->RasterizerDensity;
            const int w = ImMax(1, static_cast<int>(std::round(mask.width * scale * density)));
            const int h = ImMax(1, static_cast<int>(std::round(mask.height * scale * density)));
            const auto packed = ImFontAtlasPackAddRect(atlas, w, h);
            if (packed == ImFontAtlasRectId_Invalid)
                return false;
            // Nearest-neighbor enlargement preserves the original binary ink.
            auto &pixels = atlas->Builder->TempBuffer;
            pixels.resize(w * h * 4);
            auto rgba = reinterpret_cast<ImU32 *>(pixels.Data);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    rgba[y * w + x] =
                        IM_COL32(255, 255, 255, mask.coverage[(y * mask.height / h) * mask.pitch + x * mask.width / w]);
            glyph->X0 = std::round(mask.left * scale);
            glyph->Y0 = std::round(mask.top * scale);
            glyph->X1 = glyph->X0 + w / density;
            glyph->Y1 = glyph->Y0 + h / density;
            glyph->Visible = true;
            glyph->Colored = false;
            glyph->PackId = packed;
            ImFontAtlasBakedSetFontGlyphBitmap(atlas, font, source, glyph, ImFontAtlasPackGetRect(atlas, packed),
                                               pixels.Data, ImTextureFormat_RGBA32, w * 4);
            return true;
        };
        return result;
    }();
    return &loader;
}
inline ImFont *Add(const char *family, bool bold = false, float pixels = CellPixels)
{
    ImFontConfig config;
    config.FontData = const_cast<char *>(family);
    config.FontDataSize = static_cast<int>(std::strlen(family) + 1);
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = pixels;
    config.FontLoader = Loader();
    config.FontLoaderFlags = bold ? 1 : 0;
    config.PixelSnapH = config.PixelSnapV = true;
    return ImGui::GetIO().Fonts->AddFont(&config);
}
} // namespace DialogFont
} // namespace KauaiSDL
#endif
