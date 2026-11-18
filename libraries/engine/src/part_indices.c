#include "mm/part_indices.h"
#include "mm/collections.h"
#include <string.h>

#if SHRT_MAX != 32767 || USHRT_MAX != 65535U
#error Part indices require sixteen-bit shorts
#endif

void mm_part_indices_init(MMPartIndexList *list, const MMStorageAllocator *allocator, void *owner,
                          void (*release_owner)(void *))
{
    memset(list, 0, sizeof(*list));
    mm_storage_init(&list->storage, allocator);
    list->storage.min_grow[0] = 128;
    list->storage.min_grow[1] = 16;
    list->owner = owner;
    list->release_owner = release_owner;
}

void mm_part_indices_dispose(MMPartIndexList *list)
{
    if (list)
    {
        mm_storage_dispose(&list->storage);
        if (list->release_owner)
            list->release_owner(list->owner);
    }
}

static void publish(MMPartIndexList *list, MMI32 count)
{
    list->view.bytes = list->storage.buffers[0].data;
    list->view.count = count;
}

int mm_part_indices_read(MMPartIndexList *list, const MMReadStream *stream)
{
    const unsigned short one = 1;
    MMCollectionHeader header;
    unsigned char *word, first;
    MMU32 i;
    int status = mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, &header);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    if (header.width != MM_PART_INDEX_BYTES)
        return MM_MOVIE_BAD_FILE;
    status = mm_storage_read(&list->storage, stream, header.header_bytes, header.first_bytes, 0);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_MEMORY;
    if (header.big_endian != (*(const unsigned char *)&one == 0))
        for (i = 0; i < header.count; ++i)
        {
            word = list->storage.buffers[0].data + i * MM_PART_INDEX_BYTES;
            first = word[0];
            word[0] = word[1];
            word[1] = first;
        }
    publish(list, (MMI32)header.count);
    return MM_MOVIE_OK;
}

static int reserve(MMPartIndexList *list, MMI32 count)
{
    return count >= 0 && (MMU32)count < MM_STORAGE_LIMIT / MM_PART_INDEX_BYTES &&
           mm_storage_ensure(&list->storage, (MMU32)count * MM_PART_INDEX_BYTES, 0, 0) == MM_STORAGE_OK;
}

int mm_part_indices_copy(MMPartIndexList *list, const MMPartIndexView *source)
{
    if (!reserve(list, source->count))
        return 0;
    if (source->count)
        memcpy(list->storage.buffers[0].data, source->bytes, (size_t)source->count * MM_PART_INDEX_BYTES);
    publish(list, source->count);
    return 1;
}

int mm_part_indices_fill(MMPartIndexList *list, MMI32 count, short index)
{
    MMI32 i;
    if (!reserve(list, count))
        return 0;
    for (i = 0; i < count; ++i)
        memcpy(list->storage.buffers[0].data + i * MM_PART_INDEX_BYTES, &index, MM_PART_INDEX_BYTES);
    publish(list, count);
    return 1;
}

int mm_part_indices_append(MMPartIndexList *list, short index)
{
    MMI32 count = list->view.count;
    if (!reserve(list, count + 1))
        return 0;
    memcpy(list->storage.buffers[0].data + count * MM_PART_INDEX_BYTES, &index, MM_PART_INDEX_BYTES);
    publish(list, count + 1);
    return 1;
}

MMI32 mm_part_index_get(const MMPartIndexView *view, MMI32 index)
{
    short value;
    if (index < 0 || index >= view->count)
        return -1;
    memcpy(&value, view->bytes + index * MM_PART_INDEX_BYTES, MM_PART_INDEX_BYTES);
    return value;
}

MMI32 mm_part_indices_wire_size(const MMPartIndexList *list)
{
    return MM_PART_INDEX_HEADER_BYTES + list->view.count * MM_PART_INDEX_BYTES;
}
