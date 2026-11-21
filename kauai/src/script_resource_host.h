#ifndef KAUAI_SCRIPT_RESOURCE_HOST_H
#define KAUAI_SCRIPT_RESOURCE_HOST_H
#include "mm/script_resource.h"
#include "handle_services.h"

static MMScriptResourceServices ScriptCollectionReaders()
{
    MMScriptResourceServices s = {};
    s.decode = KauaiDecodeBlockHandle;
    s.read_rows = [](void *, const MMReadStream *stream, MMArray *rows, int *other) -> void * {
        short order;
        auto array = Group::DynamicArray::PglReadStream(*stream, &order, nullptr);
        if (array)
        {
            *rows = array->CoreRows();
            *other = order == kboOther;
        }
        return array;
    };
    s.read_strings = [](void *, const MMReadStream *stream, MMStringTable *rows, int *other) -> void * {
        short order;
        auto table = Group::StringTable_GST::PgstReadStream(*stream, &order, nullptr);
        if (table)
        {
            *rows = table->CoreStrings();
            *other = order == kboOther;
        }
        return table;
    };
    s.wrap_collection = [](void *, void *owned) -> void * {
        return NewObj Chunky::GenericCacheableObject(static_cast<BASE *>(owned));
    };
    s.release = [](void *, void *owned) { static_cast<BASE *>(owned)->Release(); };
    s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    return s;
}
#endif
