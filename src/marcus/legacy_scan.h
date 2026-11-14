#ifndef MARCUS_LEGACY_SCAN_H
#define MARCUS_LEGACY_SCAN_H

#include "zb_capture.h"
#include <algorithm>
#include <cstdint>

// Integer setup matching BRender's awtmi.h and ti8_piz.asm. Pixels, texture
// lookup and depth tests remain on the GPU; this only prepares edge/gradient
// constants, including the two scan converters' different half-X rounding.
struct MarcusLegacyScan
{
    std::int32_t y[4] = {}; // top, middle, bottom, flags (negative = floating path)
    std::int32_t edges[4] = {}; // long X/gradient, middle X/top gradient
    std::int32_t bottom[4] = {}; // bottom gradient
    std::uint32_t parameter[4][4] = {}; // Z,I,U,V: current, dX, dY(no carry)
};
static_assert(sizeof(MarcusLegacyScan) == 112, "HLSL scan setup layout");
inline MarcusLegacyScan MarcusPrepareScan(const BrRasterTriangle &triangle)
{
    MarcusLegacyScan result;
    const bool textured = triangle.texture.pixels != nullptr;
    if (triangle.raster_mode == 3 || (!triangle.raster_mode && (triangle.flags & 0x20)))
    {
        result.y[3] = -1;
        return result;
    }
    struct Vertex { std::int32_t x, y; std::uint32_t parameter[4]; } vertices[3];
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto &source = triangle.vertices[i];
        const auto &fixed = triangle.fixed_vertices[i];
        vertices[i] = {triangle.raster_mode ? fixed.x : std::int32_t(source.x * 65536),
                       triangle.raster_mode ? fixed.y : std::int32_t(source.y * 65536),
                       {triangle.raster_mode ? std::uint32_t(fixed.z) : std::uint32_t(double(source.z) * 65536),
                        triangle.raster_mode ? std::uint32_t(fixed.intensity) : std::uint32_t(std::int32_t(source.intensity * 65536)),
                        triangle.raster_mode ? std::uint32_t(fixed.u) : std::uint32_t(std::int32_t(source.u * 65536)),
                        triangle.raster_mode ? std::uint32_t(fixed.v) : std::uint32_t(std::int32_t(source.v * 65536))}};
    }
    // Match the native tie ordering before truncating subpixel coordinates.
    auto a = &vertices[0], b = &vertices[1], c = &vertices[2];
    if (textured)
    {
        if (a->y > b->y)
        {
            if (b->y > c->y) std::swap(a, c);
            else if (a->y > c->y) { std::swap(a, b); std::swap(b, c); }
            else std::swap(a, b);
        }
        else if (b->y > c->y)
        {
            if (a->y > c->y) { std::swap(b, c); std::swap(a, b); }
            else std::swap(b, c);
        }
    }
    else if (a->y > b->y)
    {
        if (c->y <= b->y) std::swap(a, c);
        else if (a->y <= c->y) std::swap(a, b);
        else { std::swap(a, b); std::swap(b, c); }
    }
    else if (b->y > c->y)
    {
        if (a->y <= c->y) std::swap(b, c);
        else { std::swap(b, c); std::swap(a, b); }
    }
    const auto x0 = a->x >> 16, x1 = b->x >> 16, x2 = c->x >> 16;
    const auto y0 = a->y >> 16, y1 = b->y >> 16, y2 = c->y >> 16;
    const auto longX = x2 - x0, longY = y2 - y0, topX = x1 - x0, topY = y1 - y0;
    const auto divisor = topX * longY - longX * topY;
    if (!longY || !divisor) return result;
    const auto gradient = [](std::int32_t dx, std::int32_t dy) {
        return dy ? std::int32_t(std::uint32_t(dx) * (65536u / std::uint32_t(dy))) : 0;
    };
    const auto longGradient = gradient(longX, longY);
    const auto topGradient = gradient(topX, topY), bottomGradient = gradient(x2 - x1, y2 - y1);
    const bool longLeft = textured ? ((topY && topGradient >= longGradient) ||
                                      (y2 != y1 && bottomGradient < longGradient)) : divisor > 0;
    result.y[0] = y0; result.y[1] = y1; result.y[2] = y2; result.y[3] = 1 | (longLeft ? 2 : 0);
    result.edges[0] = x0; result.edges[1] = longGradient;
    result.edges[2] = x1; result.edges[3] = topGradient;
    result.bottom[0] = bottomGradient;
    const auto divide = [=](std::int64_t numerator) {
        if (textured)
        {
            const auto value = numerator / divisor;
            return value < INT32_MIN || value > INT32_MAX ? 0u : std::uint32_t(value);
        }
        const auto magnitude = numerator < 0 ? std::uint64_t(-numerator) : std::uint64_t(numerator);
        const auto denominator = divisor < 0 ? 0u - std::uint32_t(divisor) : std::uint32_t(divisor);
        if ((magnitude >> 32) >= denominator) return std::uint32_t(magnitude);
        const auto value = std::uint32_t(magnitude / denominator);
        return (numerator < 0) != (divisor < 0) ? 0u - value : value;
    };
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto ab = std::int32_t(b->parameter[i] - a->parameter[i]);
        const auto ac = std::int32_t(c->parameter[i] - a->parameter[i]);
        const auto dx = divide(std::int64_t(ab) * longY - std::int64_t(ac) * topY);
        const auto dy = divide(std::int64_t(ac) * topX - std::int64_t(ab) * longX);
        const auto half = textured ? std::uint32_t(std::int32_t(dx) / 2) :
            std::uint32_t(std::int32_t(dx) >> 1) + (dx & 1);
        result.parameter[i][0] = a->parameter[i] + half;
        result.parameter[i][1] = dx;
        result.parameter[i][2] = std::uint32_t(longGradient >> 16) * dx + dy;
    }
    return result;
}
#endif
