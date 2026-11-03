/*
 * SPDX-License-Identifier: Apache-2.0
 * HFD fixed-point curve subdivision adapted from emf-converter's gdi-raster.ts.
 * Copyright 2025-present pptx-viewer contributors.
 * Licensed under the Apache License, Version 2.0. See
 * docs/licenses/ellipse-Apache-2.0.txt and ellipse-NOTICE.txt.
 *
 * Modified for 3DMMForever: C++ implementation, separate large-bound path,
 * ellipse construction, bounded row generation and integer scan conversion.
 */
#include "ellipse_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
namespace KauaiPixels
{
namespace
{
struct Vertex
{
    std::int64_t x, y; // Sixteenths of a pixel, local to the oval's rectangle.
};
// Explicit floor division avoids implementation-defined signed right shifts.
std::int64_t Shift(std::int64_t value, unsigned bits)
{
    const auto scale = std::int64_t(1) << bits;
    return value / scale - (value % scale < 0);
}
struct FixedBasis
{
    std::int64_t position, advance, endError, startError;
    FixedBasis(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
        : position(a * 1024), advance((d - a) * 1024), endError((b - 2 * c + d) * 6144),
          startError((a - 2 * b + c) * 6144)
    {
    }
    std::int64_t Error() const
    {
        return std::max(std::abs(endError), std::abs(startError));
    }
    void InitialHalve(unsigned shift)
    {
        endError = Shift(endError + startError, 1);
        advance = Shift(advance - Shift(endError, shift), 1);
    }
    void Normalize(unsigned shift)
    {
        position *= 8;
        advance *= 8;
        if (shift >= 3)
        {
            endError = Shift(endError, shift - 3);
            startError = Shift(startError, shift - 3);
        }
        else
        {
            endError *= std::int64_t(1) << (3 - shift);
            startError *= std::int64_t(1) << (3 - shift);
        }
    }
    void Halve()
    {
        endError = Shift(endError + startError, 3);
        advance = Shift(advance - endError, 1);
        startError = Shift(startError, 2);
    }
    bool CanDouble() const
    {
        return std::max(std::abs(startError), std::abs(2 * endError - startError)) <= 130944;
    }
    void Double()
    {
        advance = 2 * advance + endError;
        startError *= 4;
        endError = 8 * endError - startError;
    }
    std::int64_t Step()
    {
        position += advance;
        advance += endError;
        const auto previous = endError;
        endError = 2 * endError - startError;
        startError = previous;
        return Shift(position + 4096, 13);
    }
};
void FlattenFixed(const Vertex *p, std::vector<Vertex> &vertices)
{
    FixedBasis x(p[0].x, p[1].x, p[2].x, p[3].x), y(p[0].y, p[1].y, p[2].y, p[3].y);
    unsigned shift = 0, steps = 1;
    while (std::max(x.Error(), y.Error()) > (std::int64_t(65472) << shift))
    {
        shift += 2;
        x.InitialHalve(shift);
        y.InitialHalve(shift);
        steps *= 2;
    }
    x.Normalize(shift);
    y.Normalize(shift);
    while (steps)
    {
        if (std::max(x.Error(), y.Error()) > 523776)
        {
            x.Halve();
            y.Halve();
            steps *= 2;
        }
        while (!(steps & 1) && x.CanDouble() && y.CanDouble())
        {
            x.Double();
            y.Double();
            steps /= 2;
        }
        --steps;
        vertices.push_back({x.Step(), y.Step()});
    }
}
struct LargeBasis
{
    double position, advance, endError, startError;
    LargeBasis(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
        : position(static_cast<double>(a)), advance(static_cast<double>(d - a)),
          endError(static_cast<double>(6 * (b - 2 * c + d))), startError(static_cast<double>(6 * (a - 2 * b + c)))
    {
    }
    double Error() const
    {
        return std::max(std::abs(endError), std::abs(startError));
    }
    void Halve()
    {
        endError = (endError + startError) / 8;
        advance = (advance - endError) / 2;
        startError /= 4;
    }
    bool CanDouble() const
    {
        return std::max(std::abs(startError), std::abs(2 * endError - startError)) <= 16;
    }
    void Double()
    {
        advance = 2 * advance + endError;
        startError *= 4;
        endError = 8 * endError - startError;
    }
    std::int64_t Step()
    {
        position += advance;
        advance += endError;
        const auto previous = endError;
        endError = 2 * endError - startError;
        startError = previous;
        return static_cast<std::int64_t>(std::floor(position + 0.5));
    }
};
void FlattenLarge(const Vertex *p, std::vector<Vertex> &vertices)
{
    LargeBasis x(p[0].x, p[1].x, p[2].x, p[3].x), y(p[0].y, p[1].y, p[2].y, p[3].y);
    unsigned steps = 1;
    while (steps)
    {
        while (std::max(x.Error(), y.Error()) > 64)
        {
            x.Halve();
            y.Halve();
            steps *= 2;
        }
        while (!(steps & 1) && x.CanDouble() && y.CanDouble())
        {
            x.Double();
            y.Double();
            steps /= 2;
        }
        --steps;
        vertices.push_back({x.Step(), y.Step()});
    }
}
std::vector<Vertex> Outline(std::int32_t width, std::int32_t height)
{
    const std::int64_t cx = 8LL * (width - 1), cy = 8LL * (height - 1);
    const std::int64_t rx = 8LL * width - 4, ry = 8LL * height - 4;
    constexpr double kappa = 0.5522847498307936;
    const auto kx = static_cast<std::int64_t>(rx * kappa) + 1, ky = static_cast<std::int64_t>(ry * kappa);
    const Vertex p[] = {{cx + rx, cy},      {cx + rx, cy - ky}, {cx + kx, cy - ry}, {cx, cy - ry},
                        {cx - kx, cy - ry}, {cx - rx, cy - ky}, {cx - rx, cy},      {cx - rx, cy + ky},
                        {cx - kx, cy + ry}, {cx, cy + ry},      {cx + kx, cy + ry}, {cx + rx, cy + ky},
                        {cx + rx, cy}};
    std::vector<Vertex> vertices{p[0]};
    for (unsigned i = 0; i < 12; i += 3)
        if (rx < 16384 && ry < 16384)
            FlattenFixed(p + i, vertices);
        else
            FlattenLarge(p + i, vertices);
    return vertices;
}
} // namespace
bool EllipseRows(std::int32_t width, std::int32_t height, std::int32_t firstRow, std::int32_t rows,
                 std::vector<EllipseSpan> &spans)
{
    // Bounds keep the exact edge-intersection products within signed 64 bits.
    if (width < 0 || height < 0 || width > (1 << 26) || height > (1 << 26) || rows < 0 || rows > (1 << 26))
        return false;
    try
    {
        std::vector<EllipseSpan> result(static_cast<std::size_t>(rows));
        if (width && height && rows && firstRow < height && std::int64_t(firstRow) + rows > 0)
        {
            const auto vertices = Outline(width, height);
            for (std::int32_t row = 0; row < rows; ++row)
            {
                const auto y = std::int64_t(firstRow) + row;
                if (y < 0 || y >= height)
                    continue;
                std::int64_t left = width, right = 0;
                for (std::size_t i = 1; i < vertices.size(); ++i)
                {
                    auto a = vertices[i - 1], b = vertices[i];
                    if (a.y > b.y)
                        std::swap(a, b);
                    if (a.y <= y * 16 && y * 16 < b.y)
                    {
                        const auto dy = b.y - a.y;
                        const auto numerator = a.x * dy + (y * 16 - a.y) * (b.x - a.x);
                        const auto denominator = dy * 16;
                        const auto x = numerator / denominator + (numerator % denominator > 0);
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
                }
                if (left < right)
                    result[row] = {static_cast<std::int32_t>(std::max<std::int64_t>(0, left)),
                                   static_cast<std::int32_t>(std::min<std::int64_t>(width, right))};
            }
        }
        spans.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiPixels
