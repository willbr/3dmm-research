/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "damage_map.h"
#include <new>

namespace KauaiPixels
{
namespace
{
MMPixelRect RasterRect(const DamageRect &r)
{
    return {r.left, r.top, r.right, r.bottom};
}
int Append(void *context, const MMPixelRect *r)
{
    auto &out = *static_cast<std::vector<DamageRect> *>(context);
    if (out.size() == out.max_size())
        return 0;
    try
    {
        out.push_back({r->left, r->top, r->right, r->bottom});
        return 1;
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }
}
} // namespace
DamageMap::~DamageMap()
{
    mm_damage_reset(&map);
}
DamageMap::DamageMap(const DamageMap &other)
{
    if (!mm_damage_clone(&map, &other.map))
        throw std::bad_alloc();
}
DamageMap &DamageMap::operator=(const DamageMap &other)
{
    if (!mm_damage_clone(&map, &other.map))
        throw std::bad_alloc();
    return *this;
}
DamageMap::DamageMap(DamageMap &&other) noexcept : map(other.map)
{
    other.map = {};
}
DamageMap &DamageMap::operator=(DamageMap &&other) noexcept
{
    if (this != &other)
    {
        mm_damage_reset(&map);
        map = other.map;
        other.map = {};
    }
    return *this;
}
bool DamageMap::Init(std::int32_t width, std::int32_t height)
{
    return mm_damage_init(&map, width, height) != 0;
}
void DamageMap::Apply(const DamageRect *rectangle, bool invalidate)
{
    const auto r = rectangle ? RasterRect(*rectangle) : MMPixelRect{};
    mm_damage_set(&map, rectangle ? &r : nullptr, invalidate);
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
    MMPixelRect bounds;
    const auto marked = mm_damage_bounds(&map, &bounds);
    rectangle = {bounds.left, bounds.top, bounds.right, bounds.bottom};
    return marked != 0;
}
bool DamageMap::Snapshot(std::vector<DamageRect> &rectangles) const
{
    std::vector<DamageRect> result;
    if (!mm_damage_visit(&map, Append, &result))
        return false;
    rectangles.swap(result);
    return true;
}
bool DamageMap::Take(std::vector<DamageRect> &rectangles)
{
    std::vector<DamageRect> result;
    if (!mm_damage_take(&map, Append, &result))
        return false;
    rectangles.swap(result);
    return true;
}
bool DamageMap::Scroll(const DamageRect &rectangle, std::int32_t dx, std::int32_t dy)
{
    const auto r = RasterRect(rectangle);
    return mm_damage_scroll(&map, &r, dx, dy) != 0;
}
} // namespace KauaiPixels
