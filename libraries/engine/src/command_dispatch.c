/* Command queue and filter policy converted from Kauai cmd_core.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/command_dispatch.h"
#include "mm/wide.h"
#include <string.h>

typedef struct MMCommandFilter
{
    void *handler;
    MMI32 level;
    MMU32 mask;
} MMCommandFilter;

int mm_commands_init(MMCommandDispatch *dispatch, const MMStorageAllocator *allocator, int trash)
{
    memset(dispatch, 0, sizeof(*dispatch));
    if (mm_storage_init(&dispatch->queue_storage, allocator) != MM_STORAGE_OK ||
        mm_storage_init(&dispatch->filter_storage, allocator) != MM_STORAGE_OK)
        return 0;
    dispatch->queue_storage.min_grow[0] = dispatch->filter_storage.min_grow[0] = 128;
    dispatch->queue_storage.min_grow[1] = dispatch->filter_storage.min_grow[1] = 16;
    dispatch->queue.storage = &dispatch->queue_storage;
    dispatch->queue.width = sizeof(MMQueuedCommand);
    dispatch->queue.free_count = -1;
    dispatch->queue.trash_unused = trash;
    dispatch->filters.storage = &dispatch->filter_storage;
    dispatch->filters.width = sizeof(MMCommandFilter);
    dispatch->filters.free_count = -1;
    dispatch->filters.trash_unused = trash;
    return 1;
}

int mm_commands_reserve(MMCommandDispatch *dispatch, MMU32 commands, MMU32 filters)
{
    return mm_array_reserve(&dispatch->queue, commands, 0) == MM_STORAGE_OK &&
           mm_array_reserve(&dispatch->filters, filters, 0) == MM_STORAGE_OK;
}

static void entry_at(const MMArray *array, MMU32 index, void *entry)
{
    memcpy(entry, array->storage->buffers[0].data + index * array->width, array->width);
}

int mm_commands_insert(MMCommandDispatch *dispatch, const MMQueuedCommand *command, int at_head)
{
    MMU32 index = at_head ? dispatch->queue.count : 0;
    return mm_array_insert(&dispatch->queue, index, command, 1) == MM_STORAGE_OK;
}

int mm_commands_take(MMCommandDispatch *dispatch, MMQueuedCommand *command)
{
    return mm_array_pop(&dispatch->queue, command) == MM_STORAGE_OK;
}

int mm_commands_at(const MMCommandDispatch *dispatch, MMU32 index, MMQueuedCommand *command)
{
    if (index >= dispatch->queue.count)
        return 0;
    entry_at(&dispatch->queue, index, command);
    return 1;
}

int mm_commands_take_key(MMCommandDispatch *dispatch, MMI32 key_id, MMQueuedCommand *command)
{
    MMQueuedCommand next;
    if (!dispatch->queue.count)
        return 0;
    entry_at(&dispatch->queue, dispatch->queue.count - 1, &next);
    if (next.id != key_id)
        return -1;
    mm_commands_take(dispatch, command);
    return 1;
}

int mm_commands_contains(const MMCommandDispatch *dispatch, MMI32 id)
{
    MMQueuedCommand command;
    MMU32 i = dispatch->queue.count;
    while (i)
    {
        entry_at(&dispatch->queue, --i, &command);
        if (command.id == id)
            return 1;
    }
    return 0;
}

static int purge_commands(MMCommandDispatch *dispatch, MMI32 id, void *target, void *context,
                          MMCommandRelease release)
{
    MMQueuedCommand command;
    MMU32 i = dispatch->queue.count;
    int status;
    while (i)
    {
        if (!mm_commands_at(dispatch, --i, &command))
            continue;
        if (target ? command.target == target : command.id == id)
        {
            mm_array_delete(&dispatch->queue, i, 1);
            if (command.group)
            {
                status = release(context, command.group);
                if (status < 0)
                    return status;
            }
        }
    }
    return 0;
}

int mm_commands_flush(MMCommandDispatch *dispatch, MMI32 id, void *context, MMCommandRelease release)
{
    return purge_commands(dispatch, id, 0, context, release);
}

static void remove_filter(MMCommandDispatch *dispatch, MMU32 index)
{
    mm_array_delete(&dispatch->filters, index, 1);
    if (index < dispatch->next_filter)
        --dispatch->next_filter;
}

int mm_commands_bury(MMCommandDispatch *dispatch, void *target, void *context, MMCommandRelease release)
{
    MMCommandFilter filter;
    MMU32 i = dispatch->filters.count;
    if (!target)
        return 0;
    while (i)
    {
        entry_at(&dispatch->filters, --i, &filter);
        if (filter.handler == target)
            remove_filter(dispatch, i);
    }
    return purge_commands(dispatch, 0, target, context, release);
}

int mm_commands_dispose(MMCommandDispatch *dispatch, void *context, MMCommandRelease release)
{
    MMQueuedCommand command;
    int status;
    while (mm_commands_take(dispatch, &command))
    {
        if (command.group)
        {
            status = release(context, command.group);
            if (status < 0)
                return status;
        }
    }
    mm_storage_dispose(&dispatch->queue_storage);
    mm_storage_dispose(&dispatch->filter_storage);
    dispatch->filters.count = dispatch->next_filter = 0;
    return 0;
}

MMU32 mm_command_target_mask(const void *handler, const void *target)
{
    if (!target)
        return MM_COMMAND_UNTARGETED;
    return handler == target ? MM_COMMAND_SELF : MM_COMMAND_OTHER;
}

int mm_command_id_allocate(MMU32 *last, MMU32 count, void *context, MMCommandIdInUse in_use, MMI32 *first)
{
    const MMU32 high = 0x80000000UL;
    MMU32 remaining = count;
    int occupied;
    if (!last || !first || !count || count >= high)
        return 0;
    *last |= high;
    while (remaining)
    {
        ++*last;
        if (!(*last & high))
        {
            *last = high;
            remaining = count;
            continue;
        }
        occupied = in_use ? in_use(context, mm_signed_word(*last)) : 0;
        if (occupied < 0)
            return occupied;
        if (occupied)
            remaining = count;
        else
            --remaining;
    }
    *first = mm_signed_word(*last - (count - 1));
    return 1;
}

const void *mm_command_map_find(const MMCommandMap *map, MMI32 id, MMU32 mask,
                                int (*has_default)(const void *))
{
    const unsigned char *entry, *fallback = NULL;
    MMCommandBinding binding;
    for (; map; map = map->base)
    {
        entry = (const unsigned char *)map->entries;
        for (;; entry += map->stride)
        {
            memcpy(&binding, entry, sizeof(binding));
            if (!binding.id)
                break;
            if (binding.id == id && (binding.mask & mask))
                return entry;
        }
        if (!fallback && (binding.mask & mask) && has_default(entry))
            fallback = entry;
    }
    return fallback;
}

int mm_command_binding_enable(const MMCommandBinding *binding, int has_command, int has_enabler)
{
    if (!has_command)
        return MM_COMMAND_ENABLE_NONE;
    if (has_enabler)
        return MM_COMMAND_ENABLE_CALLBACK;
    return binding->id ? MM_COMMAND_ENABLE_EXPLICIT : MM_COMMAND_ENABLE_NONE;
}

static MMU32 first_at_level(const MMCommandDispatch *dispatch, MMI32 level)
{
    MMCommandFilter filter;
    MMU32 low = 0, high = dispatch->filters.count, middle;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        entry_at(&dispatch->filters, middle, &filter);
        if (filter.level < level)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

int mm_commands_add_filter(MMCommandDispatch *dispatch, void *handler, MMI32 level, MMU32 mask)
{
    MMCommandFilter filter = {0};
    MMU32 index = first_at_level(dispatch, level);
    if (!(mask & MM_COMMAND_ALL))
        return 0;
    filter.handler = handler;
    filter.level = level;
    filter.mask = mask;
    if (mm_array_insert(&dispatch->filters, index, &filter, 1) != MM_STORAGE_OK)
        return 0;
    if (index <= dispatch->next_filter)
        ++dispatch->next_filter;
    return 1;
}

void mm_commands_remove_filter(MMCommandDispatch *dispatch, void *handler, MMI32 level)
{
    MMCommandFilter filter;
    MMU32 index = first_at_level(dispatch, level);
    for (; index < dispatch->filters.count; ++index)
    {
        entry_at(&dispatch->filters, index, &filter);
        if (filter.level != level)
            break;
        if (filter.handler == handler)
        {
            remove_filter(dispatch, index);
            break;
        }
    }
}

int mm_command_modal_allows(void *modal, void *target, MMCommandParent parent)
{
    if (!modal || !target)
        return 1;
    for (; target; target = parent(target))
        if (target == modal)
            return 1;
    return 0;
}

static int send_to(const MMCommandRoute *route, void *context, void *handler)
{
    int allowed = route->allowed ? route->allowed(context, handler) : 1;
    return allowed > 0 ? route->send(context, handler) : allowed;
}

int mm_commands_route(MMCommandDispatch *dispatch, void *context, const MMCommandRoute *route)
{
    MMCommandFilter filter;
    void *target;
    int status, discarded;
    if (dispatch->dispatching)
        return 0;
    dispatch->dispatching = 1;
    status = route->prepare(context);
    if (status > 0)
    {
        status = 0;
        dispatch->next_filter = 0;
        while (!status && dispatch->next_filter < dispatch->filters.count)
        {
            entry_at(&dispatch->filters, dispatch->next_filter++, &filter);
            if (filter.mask & mm_command_target_mask(filter.handler, route->target(context)))
                status = send_to(route, context, filter.handler);
        }
        if (!status)
        {
            target = route->target(context);
            if (target)
                status = send_to(route, context, target);
        }
        if (status >= 0)
        {
            status = route->finish(context);
            if (status >= 0)
                status = 1;
        }
    }
    if (status && route->discard_group)
    {
        discarded = route->discard_group(context);
        if (status > 0 && discarded < 0)
            status = discarded;
    }
    dispatch->dispatching = 0;
    return status;
}

static int enable_at(const MMCommandEnable *query, void *context, void *handler, MMU32 *flags)
{
    int allowed = query->allowed ? query->allowed(context, handler) : 1;
    *flags = 0;
    return allowed > 0 ? query->enable(context, handler, flags) : allowed;
}

int mm_commands_enable(const MMCommandDispatch *dispatch, void *context, const MMCommandEnable *query,
                       MMU32 *flags)
{
    MMCommandFilter filter;
    MMU32 index, count = dispatch->filters.count;
    void *target;
    int status;
    for (index = 0; index < count && index < dispatch->filters.count; ++index)
    {
        entry_at(&dispatch->filters, index, &filter);
        status = enable_at(query, context, filter.handler, flags);
        if (status)
            return status;
    }
    target = query->target(context);
    *flags = 0;
    return target ? enable_at(query, context, target, flags) : 0;
}
