/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "damage_map.h"
#include <algorithm>
#include <cstring>
#include <new>
namespace KauaiPixels
{
bool DamageMap::Init(std::int32_t w, std::int32_t h)
{
    if (w < 1 || h < 1)
        return false;
    const auto row = (std::uint64_t(w) + 63) / 64;
    const auto count = row * h;
    if (count > INT32_MAX / sizeof(std::uint64_t) || count > bits.max_size())
        return false;
    try
    {
        std::vector<std::uint64_t> allocated(static_cast<std::size_t>(count));
        bits.swap(allocated);
        width = w;
        height = h;
        stride = static_cast<std::size_t>(row);
        cached = {};
        boundsValid = true;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool DamageMap::Clip(DamageRect &r) const
{
    r.left = std::max(0, r.left);
    r.top = std::max(0, r.top);
    r.right = std::min(width, r.right);
    r.bottom = std::min(height, r.bottom);
    return r.left < r.right && r.top < r.bottom;
}
void DamageMap::Apply(const DamageRect *rectangle, bool invalidate)
{
    DamageRect r = rectangle ? *rectangle : DamageRect{0, 0, width, height};
    if (!Clip(r))
        return;
    const auto first = r.left / 64, last = (r.right - 1) / 64;
    for (auto y = r.top; y < r.bottom; ++y)
        for (auto x = first; x <= last; ++x)
        {
            auto mask = ~std::uint64_t(0);
            if (x == first)
                mask &= ~std::uint64_t(0) << (r.left & 63);
            if (x == last)
                mask &= ~std::uint64_t(0) >> (63 - ((r.right - 1) & 63));
            auto &word = bits[std::size_t(y) * stride + x];
            if (invalidate)
                word |= mask;
            else
                word &= ~mask;
        }
    boundsValid = false;
}
void DamageMap::Invalidate(const DamageRect *rectangle)
{
    Apply(rectangle, true);
}
void DamageMap::Validate(const DamageRect *rectangle)
{
    Apply(rectangle, false);
}
bool DamageMap::Bounds(DamageRect &rectangle) const
{
    if (!boundsValid)
    {
        cached = {width, height, 0, 0};
        for (std::int32_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < stride; ++x)
            {
                auto word = bits[std::size_t(y) * stride + x];
                if (!word)
                    continue;
                unsigned first = 0, last = 63;
                while (!(word & (std::uint64_t(1) << first)))
                    ++first;
                while (!(word & (std::uint64_t(1) << last)))
                    --last;
                cached.left = std::min(cached.left, static_cast<std::int32_t>(x * 64 + first));
                cached.right = std::max(cached.right, static_cast<std::int32_t>(x * 64 + last + 1));
                cached.top = std::min(cached.top, y);
                cached.bottom = y + 1;
            }
        if (cached.right <= cached.left)
            cached = {};
        boundsValid = true;
    }
    rectangle = cached;
    return cached.left < cached.right && cached.top < cached.bottom;
}
bool DamageMap::Snapshot(std::vector<DamageRect> &rectangles) const
{
    try
    {
        std::vector<DamageRect> result;
        std::size_t rowStart = 0;
        for (std::int32_t y = 0; y < height; ++y)
        {
            const auto row = bits.data() + std::size_t(y) * stride;
            if (y && std::memcmp(row, row - stride, stride * sizeof(std::uint64_t)) == 0)
            {
                for (auto i = rowStart; i < result.size(); ++i)
                    ++result[i].bottom;
                continue;
            }
            rowStart = result.size();
            for (std::int32_t x = 0; x < width;)
            {
                const auto on = [&](std::int32_t p) { return row[p / 64] & (std::uint64_t(1) << (p & 63)); };
                if (!on(x))
                {
                    ++x;
                    continue;
                }
                const auto left = x++;
                while (x < width && on(x))
                    ++x;
                result.push_back({left, y, x, y + 1});
            }
        }
        rectangles.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool DamageMap::Take(std::vector<DamageRect> &rectangles)
{
    if (!Snapshot(rectangles))
        return false;
    Validate();
    return true;
}
bool DamageMap::Scroll(const DamageRect &rectangle, std::int32_t dx, std::int32_t dy)
{
    DamageRect area = rectangle, pending;
    if (!Clip(area) || (!dx && !dy) || !Bounds(pending))
        return true;
    if (pending.right <= area.left || pending.left >= area.right || pending.bottom <= area.top ||
        pending.top >= area.bottom)
        return true;
    std::vector<DamageRect> snapshot;
    if (!Snapshot(snapshot))
        return false;
    Validate(&area);
    for (auto part : snapshot)
    {
        const auto left = std::max(part.left, area.left), right = std::min(part.right, area.right);
        const auto top = std::max(part.top, area.top), bottom = std::min(part.bottom, area.bottom);
        if (left >= right || top >= bottom)
            continue;
        const auto x0 = std::max<std::int64_t>(area.left, std::int64_t(left) + dx);
        const auto y0 = std::max<std::int64_t>(area.top, std::int64_t(top) + dy);
        const auto x1 = std::min<std::int64_t>(area.right, std::int64_t(right) + dx);
        const auto y1 = std::min<std::int64_t>(area.bottom, std::int64_t(bottom) + dy);
        if (x0 < x1 && y0 < y1)
        {
            DamageRect moved{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                             static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1)};
            Invalidate(&moved);
        }
    }
    return true;
}
} // namespace KauaiPixels
