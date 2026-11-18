#ifndef ENGINE_TEMPLATE_COSTUMES_H
#define ENGINE_TEMPLATE_COSTUMES_H

#include "kauai_core.h"
#include "mm/template_create.h"

namespace EngineTemplateCostumes
{
// The BASE allocation retains the native allocation and memory diagnostics.
// The C table manages the two collection buffers and their contents.
class Allocation : public BASE
{
  public:
    MMTemplateCostumeTable table;

    Allocation()
    {
        const auto allocator = CollectionAllocator();
        mm_template_costumes_init(&table, &allocator, this,
                                  [](void *owner) { static_cast<Allocation *>(owner)->Release(); });
    }

#ifdef DEBUG
    void MarkMem() override
    {
        BASE::MarkMem();
        MarkPv(table.storage.buffers[0].data);
        MarkPv(table.storage.buffers[1].data);
    }
#endif
};

inline MMTemplateCostumeTable *Create()
{
    ASSERTNAME
    auto allocation = NewObj Allocation;
    return allocation ? &allocation->table : nullptr;
}
}

#endif
