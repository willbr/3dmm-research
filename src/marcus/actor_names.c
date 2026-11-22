#include "actor_names.h"
#include "mm/collections.h"
#include <stdio.h>
#include <string.h>

int marcus_actor_names_init(MarcusActorNames *names, const MMStorageAllocator *allocator)
{
    names->count = names->bytes = 0;
    return mm_storage_init(&names->storage, allocator) == MM_STORAGE_OK;
}

void marcus_actor_names_clear(MarcusActorNames *names)
{
    names->count = names->bytes = 0;
}

void marcus_actor_names_dispose(MarcusActorNames *names)
{
    mm_storage_dispose(&names->storage);
    marcus_actor_names_clear(names);
}

void marcus_actor_names_swap(MarcusActorNames *first, MarcusActorNames *second)
{
    MarcusActorNames saved = *first;
    *first = *second;
    *second = saved;
}

int marcus_actor_names_copy(MarcusActorNames *destination, const MarcusActorNames *source)
{
    MarcusActorNames copy;
    int ok;
    if (destination == source)
        return 1;
    if (!marcus_actor_names_init(&copy, &destination->storage.allocator))
        return 0;
    ok = mm_storage_copy(&copy.storage, &source->storage, source->bytes, source->count * 8) == MM_STORAGE_OK;
    if (ok)
    {
        copy.count = source->count;
        copy.bytes = source->bytes;
        marcus_actor_names_swap(destination, &copy);
    }
    marcus_actor_names_dispose(&copy);
    return ok;
}

static MMGroup group_view(const MarcusActorNames *names)
{
    MMGroup group = {0};
    group.storage = (MMCollectionStorage *)&names->storage;
    group.fixed_bytes = sizeof(MMI32);
    group.count = names->count;
    group.bytes = names->bytes;
    group.free_count = -1;
    return group;
}

static int find_actor(const MMGroup *group, MMI32 actor, MMU32 *position, MMGroupLocation *location)
{
    MMU32 first = 0, last = group->count;
    while (first < last)
    {
        const MMU32 middle = first + (last - first) / 2;
        MMI32 id;
        if (mm_group_location(group, middle, location) != MM_STORAGE_OK)
            return -1;
        memcpy(&id, group->storage->buffers[0].data + location->offset, sizeof(id));
        if (id == actor)
        {
            *position = middle;
            return 1;
        }
        if (id < actor)
            first = middle + 1;
        else
            last = middle;
    }
    *position = first;
    return 0;
}

int marcus_actor_names_set(MarcusActorNames *names, MMI32 actor, const char *name, size_t bytes)
{
    MMGroup group = group_view(names);
    MMGroupLocation location;
    MMU32 position;
    int found, status;
    if (bytes >= MM_STORAGE_LIMIT - 1)
        return 0;
    found = find_actor(&group, actor, &position, &location);
    if (found < 0)
        return 0;
    names->storage.min_grow[0] = names->storage.buffers[0].capacity / 2;
    names->storage.min_grow[1] = names->storage.buffers[1].capacity / 2;
    if (found)
        status = mm_group_put(&group, position, (MMU32)bytes + 1, name);
    else
        status = mm_group_insert(&group, position, (MMU32)bytes + 1, name, &actor);
    if (status != MM_STORAGE_OK)
        return 0;
    names->count = group.count;
    names->bytes = group.bytes;
    return 1;
}

const char *marcus_actor_names_get(const MarcusActorNames *names, MMI32 actor, size_t *bytes)
{
    const MMGroup group = group_view(names);
    MMGroupLocation location;
    MMU32 position;
    if (find_actor(&group, actor, &position, &location) != 1)
        return NULL;
    if (bytes)
        *bytes = (size_t)location.bytes - group.fixed_bytes - 1;
    return (const char *)names->storage.buffers[0].data + location.offset + group.fixed_bytes;
}

void marcus_actor_display_name(const MarcusActorNames *names, MMI32 actor, int text, int prop,
                              char *output, size_t capacity)
{
    char fallback[32];
    size_t length, count;
    const char *name = marcus_actor_names_get(names, actor, &length);
    if (!capacity)
        return;
    if (!name)
    {
        length = (size_t)sprintf(fallback, "%s %ld", text ? "3D text" : prop ? "Prop" : "Actor", (long)actor);
        name = fallback;
    }
    count = length < capacity ? length : capacity - 1;
    while (count && count < length && ((unsigned char)name[count] & 0xc0) == 0x80)
        --count;
    memcpy(output, name, count);
    output[count] = '\0';
}
