#ifndef ENGINE_PART_INDICES_H
#define ENGINE_PART_INDICES_H

#include "kauai_core.h"
#include "mm/part_indices.h"

namespace EnginePartIndices
{
inline MMPartIndexView Borrow(PDynamicArray list)
{
    ASSERTNAME
    AssertPo(list, 0);
    Assert(list->CbEntry() == MM_PART_INDEX_BYTES, "bad body part index width");
    const auto count = list->IvMac();
    return {count ? static_cast<const unsigned char *>(list->QvGet(0)) : nullptr, count};
}
}

#endif
