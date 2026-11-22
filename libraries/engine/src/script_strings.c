/* Runtime registry converted from Kauai screxe.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_strings.h"
#include "mm/string_table.h"
#include "mm/wide.h"
#include <string.h>

enum { REGISTRY_STRIDE = 8 };

static void bind(MMStringTable *view, const MMCollectionStorage *storage)
{
    view->strings = storage->buffers[0].data;
    view->entries = storage->buffers[1].data;
    view->string_capacity = storage->buffers[0].capacity;
    view->entry_capacity = storage->buffers[1].capacity;
}

static int reserve(void *context, MMStringTable *view, MMU32 bytes, MMU32 entries)
{
    MMScriptStrings *registry = context;
    int result = mm_storage_ensure(&registry->storage, bytes, entries, 0);
    bind(view, &registry->storage);
    return result == MM_STORAGE_OK;
}

static MMStringTable read_view(const MMScriptStrings *registry)
{
    MMStringTable view;
    memset(&view, 0, sizeof(view));
    bind(&view, &registry->storage);
    view.count = registry->count;
    view.bytes = registry->bytes;
    view.unit_bytes = registry->unit_bytes;
    view.trash_unused = registry->trash_unused;
    view.stride = REGISTRY_STRIDE;
    view.free_count = -1;
    return view;
}

static int find(const MMStringTable *view, MMI32 id, MMU32 *index)
{
    MMU32 first = 0, last = view->count, mid;
    MMI32 other;
    while (first < last)
    {
        mid = first + (last - first) / 2;
        memcpy(&other, view->entries + mid * REGISTRY_STRIDE + 4, sizeof(other));
        if (other == id)
        {
            *index = mid;
            return 1;
        }
        if (other < id)
            first = mid + 1;
        else
            last = mid;
    }
    *index = first;
    return 0;
}

int mm_script_strings_init(MMScriptStrings *registry, unsigned unit_bytes, int trash_unused,
                           const MMStorageAllocator *allocator)
{
    if (!registry || (unit_bytes != 1 && unit_bytes != 2))
        return MM_STORAGE_ARGUMENT;
    memset(registry, 0, sizeof(*registry));
    if (mm_storage_init(&registry->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    registry->unit_bytes = unit_bytes;
    registry->trash_unused = trash_unused;
    registry->storage.min_grow[0] = 120;
    registry->storage.min_grow[1] = 12 * REGISTRY_STRIDE;
    return MM_STORAGE_OK;
}

void mm_script_strings_dispose(MMScriptStrings *registry)
{
    if (mm_storage_dispose(&registry->storage) == MM_STORAGE_OK)
        registry->bytes = registry->count = registry->last_id = 0;
}

int mm_script_strings_get(const MMScriptStrings *registry, MMI32 id, const void **text, MMU32 *count)
{
    MMStringTable view = read_view(registry);
    MMU32 index;
    *text = NULL;
    *count = 0;
    return find(&view, id, &index) && mm_strings_get(&view, index, text, count);
}

static int put(MMScriptStrings *registry, MMStringTable *view, MMU32 index, int replace,
               MMI32 id, const void *text, MMU32 count)
{
    int ok;
    view->reserve = reserve;
    view->context = registry;
    if (replace)
        ok = mm_strings_put(view, index, text, count);
    else
        ok = mm_strings_insert(view, index, text, count, &id);
    registry->bytes = view->bytes;
    registry->count = view->count;
    return ok;
}

int mm_script_strings_put(MMScriptStrings *registry, MMI32 id, const void *text, MMU32 count)
{
    MMStringTable view = read_view(registry);
    MMU32 index;
    int replace = find(&view, id, &index);
    return put(registry, &view, index, replace, id, text, count);
}

int mm_script_strings_add(MMScriptStrings *registry, MMI32 *id, const void *text, MMU32 count)
{
    MMStringTable view = read_view(registry);
    MMU32 index;
    do
    {
        registry->last_id = (registry->last_id + 1U) | 0x80000000UL;
        *id = mm_signed_word(registry->last_id);
    } while (find(&view, *id, &index));
    return put(registry, &view, index, 0, *id, text, count);
}

static int delete_at(MMScriptStrings *registry, MMStringTable *view, MMU32 index)
{
    int ok = mm_strings_delete(view, index);
    registry->bytes = view->bytes;
    registry->count = view->count;
    return ok;
}

void mm_script_strings_delete(MMScriptStrings *registry, MMI32 id)
{
    MMStringTable view = read_view(registry);
    MMU32 index;
    if (find(&view, id, &index))
        delete_at(registry, &view, index);
}

int mm_script_strings_move(MMScriptStrings *registry, MMI32 source, MMI32 destination)
{
    MMStringTable view = read_view(registry);
    MMU32 index;
    if (!find(&view, destination, &index))
        return 0;
    if (source == destination)
        return 1;

    /* Retail's distinct-ID path looks up destination for both rows. It deletes
     * that entry and assigns its ID to the next row if one exists. */
    if (!delete_at(registry, &view, index))
        return 0;
    if (index < view.count)
        memcpy(view.entries + index * REGISTRY_STRIDE + 4, &destination, sizeof(destination));
    return 1;
}
