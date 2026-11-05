/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_surface.h"
#include <SDL3/SDL.h>
#include <memory>
#include <limits>
namespace KauaiSDL
{
bool BlitPixels(const KauaiPixels::PackedSurface &destination, const KauaiPixels::PackedSurface &source,
                const KauaiPixels::DamageRect &sourceRect, const KauaiPixels::DamageRect &destinationRect,
                const KauaiPixels::DamageRect *clips, std::size_t clipCount)
{
    const auto valid = [](const KauaiPixels::PackedSurface &surface) {
        return surface.pixels && surface.width > 0 && surface.height > 0 &&
               (surface.depth == 8 || surface.depth == 32) &&
               std::int64_t(surface.pitch) >= std::int64_t(surface.width) * (surface.depth / 8) &&
               std::uint64_t(surface.pitch) * surface.height <= std::numeric_limits<std::size_t>::max() &&
               (surface.depth != 8 || (surface.palette && surface.colors == 256));
    };
    const auto width = std::int64_t(sourceRect.right) - sourceRect.left;
    const auto height = std::int64_t(sourceRect.bottom) - sourceRect.top;
    if (!valid(source) || !valid(destination) || width <= 0 || height <= 0 || sourceRect.left < 0 ||
        sourceRect.top < 0 || sourceRect.right > source.width || sourceRect.bottom > source.height ||
        width != std::int64_t(destinationRect.right) - destinationRect.left ||
        height != std::int64_t(destinationRect.bottom) - destinationRect.top ||
        (source.depth == 32 && destination.depth != 32))
        return false;
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto destinationStart = reinterpret_cast<std::uintptr_t>(destination.pixels);
    const auto sourceBytes = std::size_t(source.pitch) * source.height;
    const auto destinationBytes = std::size_t(destination.pitch) * destination.height;
    if ((sourceStart <= destinationStart && destinationStart - sourceStart < sourceBytes) ||
        (destinationStart < sourceStart && sourceStart - destinationStart < destinationBytes))
        return false;
    if (destination.depth == 8)
        for (int i = 0; i < 256; ++i)
            if (source.palette[i].bRed != destination.palette[i].bRed ||
                source.palette[i].bGreen != destination.palette[i].bGreen ||
                source.palette[i].bBlue != destination.palette[i].bBlue)
                return false;
    const auto format = [](int depth) { return depth == 8 ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_ARGB8888; };
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface input(SDL_CreateSurfaceFrom(source.width, source.height, format(source.depth), source.pixels, source.pitch),
                  SDL_DestroySurface);
    Surface output(SDL_CreateSurfaceFrom(destination.width, destination.height, format(destination.depth),
                                         destination.pixels, destination.pitch),
                   SDL_DestroySurface);
    if (!input || !output || !SDL_SetSurfaceBlendMode(input.get(), SDL_BLENDMODE_NONE))
        return false;
    if (source.depth == 8)
    {
        std::unique_ptr<SDL_Palette, decltype(&SDL_DestroyPalette)> palette(SDL_CreatePalette(256), SDL_DestroyPalette);
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i)
            colors[i] = {source.palette[i].bRed, source.palette[i].bGreen, source.palette[i].bBlue, 0};
        if (!palette || !SDL_SetPaletteColors(palette.get(), colors, 0, 256) ||
            !SDL_SetSurfacePalette(input.get(), palette.get()) ||
            (destination.depth == 8 && !SDL_SetSurfacePalette(output.get(), palette.get())))
            return false;
    }
    const SDL_Rect src{sourceRect.left, sourceRect.top, int(width), int(height)};
    const SDL_Rect dst{destinationRect.left, destinationRect.top, int(width), int(height)};
    if (!clips)
        return SDL_BlitSurface(input.get(), &src, output.get(), &dst);
    for (std::size_t i = 0; i < clipCount; ++i)
    {
        const auto &r = clips[i];
        if (r.left >= r.right || r.top >= r.bottom)
            continue;
        const SDL_Rect clip{r.left, r.top, r.right - r.left, r.bottom - r.top};
        if (SDL_SetSurfaceClipRect(output.get(), &clip) && !SDL_BlitSurface(input.get(), &src, output.get(), &dst))
            return false;
    }
    return true;
}
} // namespace KauaiSDL
