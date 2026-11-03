/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_ELLIPSE_GEOMETRY_H
#define KAUAI_ELLIPSE_GEOMETRY_H
#include <cstdint>
#include <vector>
namespace KauaiPixels
{
struct EllipseSpan
{
    std::int32_t left = 0, right = 0;
};
// Coverage of the legacy filled oval in local coordinates. Only the requested
// rows are produced; rows outside the shape are empty. Zero extents are empty,
// negative/oversized extents fail transactionally. No native graphics objects.
bool EllipseRows(std::int32_t width, std::int32_t height, std::int32_t firstRow, std::int32_t rows,
                 std::vector<EllipseSpan> &spans);
} // namespace KauaiPixels
#endif
