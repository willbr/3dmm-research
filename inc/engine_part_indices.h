#ifndef ENGINE_PART_INDICES_H
#define ENGINE_PART_INDICES_H

#include "kauai_core.h"
#include "mm/part_indices.h"

namespace EnginePartIndices
{
// Keep the BASE allocation and its diagnostics at the native boundary.
class Allocation : public BASE
{
  public:
    MMPartIndexList list;

    Allocation()
    {
        const auto allocator = CollectionAllocator();
        mm_part_indices_init(&list, &allocator, this,
                             [](void *owner) { static_cast<Allocation *>(owner)->Release(); });
    }

#ifdef DEBUG
    void MarkMem() override
    {
        BASE::MarkMem();
        MarkPv(list.storage.buffers[0].data);
    }
#endif
};

inline MMPartIndexList *Create()
{
    ASSERTNAME
    auto allocation = NewObj Allocation;
    return allocation ? &allocation->list : nullptr;
}

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
