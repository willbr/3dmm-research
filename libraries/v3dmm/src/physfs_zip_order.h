/*
 * Adapted from PhysicsFS 1.1.0's __PHYSFS_sort (physfs.c). This altered
 * version sorts VXP member records so duplicate ZIP lookups match the
 * PhysicsFS 1.0.60 DLL shipped with v3dmm 0.14.
 * https://icculus.org/physfs/downloads/physfs-1.1.0.tar.gz
 *
 * Copyright (c) 2003 Ryan C. Gordon and others.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ryan C. Gordon <icculus@clutteredmind.org>
 */
#ifndef THREE_MM_PHYSFS_ZIP_ORDER_H
#define THREE_MM_PHYSFS_ZIP_ORDER_H
#include "three_mm/v3dmm.h"
#include <cstring>
#include <utility>

namespace ThreeMM::V3dmm::Detail
{
inline void SortZipMembers(std::vector<Member> &entries, std::size_t lo, std::size_t hi)
{
    auto compare = [&](std::size_t a, std::size_t b) {
        return std::strcmp(entries[a].name.c_str(), entries[b].name.c_str());
    };
    if (hi - lo <= 4)
    {
        bool sorted;
        do
        {
            sorted = true;
            for (auto i = lo; i < hi; ++i)
                if (compare(i, i + 1) > 0)
                {
                    std::swap(entries[i], entries[i + 1]);
                    sorted = false;
                }
        } while (!sorted);
        return;
    }
    auto i = (hi + lo) / 2;
    if (compare(lo, i) > 0)
        std::swap(entries[lo], entries[i]);
    if (compare(lo, hi) > 0)
        std::swap(entries[lo], entries[hi]);
    if (compare(i, hi) > 0)
        std::swap(entries[i], entries[hi]);
    auto j = hi - 1;
    std::swap(entries[i], entries[j]);
    const auto pivot = j;
    i = lo;
    for (;;)
    {
        while (compare(++i, pivot) < 0)
        {
        }
        while (compare(--j, pivot) > 0)
        {
        }
        if (j < i)
            break;
        std::swap(entries[i], entries[j]);
    }
    std::swap(entries[i], entries[hi - 1]);
    SortZipMembers(entries, lo, j);
    SortZipMembers(entries, i + 1, hi);
}

inline std::vector<Member> SelectZipMembers(const std::vector<Member> &entries)
{
    if (entries.empty())
        return {};
    auto sorted = entries;
    SortZipMembers(sorted, 0, sorted.size() - 1);
    std::vector<bool> selected(entries.size(), false);
    for (std::size_t first = 0; first < sorted.size();)
    {
        auto lo = 0, hi = int(sorted.size()) - 1;
        // Exact case-sensitive lookup, with the original lower midpoint.
        while (lo <= hi)
        {
            const auto middle = lo + (hi - lo) / 2;
            const auto order = std::strcmp(sorted[first].name.c_str(), sorted[middle].name.c_str());
            if (order == 0)
            {
                selected[sorted[middle].index] = true;
                break;
            }
            if (order < 0)
                hi = middle - 1;
            else
                lo = middle + 1;
        }
        const auto name = sorted[first].name;
        while (first < sorted.size() && sorted[first].name == name)
            ++first;
    }
    std::vector<Member> result;
    for (const auto &entry : entries)
        if (selected[entry.index])
            result.push_back(entry);
    return result;
}
} // namespace ThreeMM::V3dmm::Detail
#endif
