/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "cursor_data.h"
#include <utility>
#include "kauai_core.h"
#include "groups.h"
#undef size
ASSERTNAME
namespace KauaiCursors
{
bool Read(DataBlock &block, int preferredWidth, int preferredHeight, Monochrome &image)
{
    if (preferredWidth < 1 || preferredHeight < 1)
        return false;
    short order;
    auto group = GeneralGroup::PggRead(&block, &order);
    if (!group)
        return false;
    int best = -1;
    std::int64_t distance = INT64_MAX;
    CursorOnFile chosen = {};
    if (group->CbFixed() == sizeof(CursorOnFile))
        for (LegacyLong i = 0; i < group->IvMac(); ++i)
        {
            CursorOnFile candidate;
            group->GetFixed(i, &candidate);
            if (order == kboOther)
                SwapBytesBom(&candidate, kbomCurf);
            if (candidate.curt != curtMonochrome || candidate.dxp != candidate.dyp ||
                (candidate.dxp != 16 && candidate.dxp != 32) || candidate.xp >= candidate.dxp ||
                candidate.yp >= candidate.dyp || candidate.dxp > preferredWidth || candidate.dyp > preferredHeight ||
                group->Cb(i) != candidate.dxp * candidate.dyp / 4)
                continue;
            const auto delta = std::int64_t(preferredWidth) - candidate.dxp + preferredHeight - candidate.dyp;
            if (delta < distance)
            {
                distance = delta;
                best = i;
                chosen = candidate;
            }
        }
    bool result = false;
    if (best >= 0)
        try
        {
            Monochrome loaded;
            loaded.width = chosen.dxp;
            loaded.height = chosen.dyp;
            loaded.pitch = chosen.dxp / 8;
            loaded.hotX = chosen.xp;
            loaded.hotY = chosen.yp;
            const auto bytes = loaded.pitch * loaded.height;
            const auto planes = static_cast<const std::uint8_t *>(group->QvGet(best));
            loaded.andBits.assign(planes, planes + bytes);
            loaded.xorBits.assign(planes + bytes, planes + 2 * bytes);
            image = std::move(loaded);
            result = true;
        }
        catch (const std::bad_alloc &)
        {
        }
    ReleasePpo(&group);
    return result;
}
} // namespace KauaiCursors
