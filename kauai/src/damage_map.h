/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_DAMAGE_MAP_H
#define KAUAI_DAMAGE_MAP_H
#include <cstddef>
#include <cstdint>
#include <vector>
namespace KauaiPixels
{
struct DamageRect
{
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};
// Exact logical-pixel damage, independent of native window/update regions.
// One bit per pixel; snapshots merge identical adjacent scanlines into bands.
class DamageMap
{
    std::int32_t width = 0, height = 0;
    std::size_t stride = 0;
    std::vector<std::uint64_t> bits;
    mutable bool boundsValid = false;
    mutable DamageRect cached;
    bool Clip(DamageRect &rectangle) const;
    void Apply(const DamageRect *rectangle, bool invalidate);

  public:
    // Transactional reset to an empty map. Invalid sizes leave state intact.
    bool Init(std::int32_t width, std::int32_t height);
    void Invalidate(const DamageRect *rectangle = nullptr);
    void Validate(const DamageRect *rectangle = nullptr);
    bool Bounds(DamageRect &rectangle) const;
    // Allocation failure leaves both map and output unchanged.
    bool Snapshot(std::vector<DamageRect> &rectangles) const;
    bool Take(std::vector<DamageRect> &rectangles);
    // Move existing damage within rectangle, clipping at its edges. Exposed
    // cells are marked separately by the caller's repaint policy.
    bool Scroll(const DamageRect &rectangle, std::int32_t dx, std::int32_t dy);
};
} // namespace KauaiPixels
#endif
