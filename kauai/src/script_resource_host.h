#ifndef KAUAI_SCRIPT_RESOURCE_HOST_H
#define KAUAI_SCRIPT_RESOURCE_HOST_H
#include "mm/script_resource.h"
#include "handle_services.h"

static inline MMScriptResourceServices ScriptCollectionReaders()
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
    s.release = [](void *, void *owned) { static_cast<BASE *>(owned)->Release(); };
    return s;
}

static inline const MMScriptCollectionHost &ScriptCollectionHost()
{
    static const MMScriptCollectionHost host = {
        nullptr, KauaiDecodeBlockHandle, KauaiResourceObjectAllocator(), KauaiCopyAllocator(), koskCur, sizeof(achar),
        [](void *, const void *input, size_t bytes, int encoding, void *out, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(out), capacity);
        },
        [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); },
#ifdef DEBUG
        [](void *owner, const MMCollectionStorage *storage, const MMResourceObject *binding) {
            MarkPv(owner);
            MarkPv(storage->buffers[0].data);
            MarkPv(storage->buffers[1].data);
            if (binding->cache && !binding->attached)
                mm_resource_cache_mark(binding->cache);
        }
#else
        nullptr
#endif
    };
    return host;
}
#endif
