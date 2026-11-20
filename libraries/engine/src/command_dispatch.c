/* Command queue and filter policy converted from Kauai cmd_core.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/command_dispatch.h"
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

int mm_commands_route(MMCommandDispatch *dispatch, void *context, const MMCommandRoute *route)
{
    MMCommandFilter filter;
    void *target;
    int status;
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
                status = route->send(context, filter.handler);
        }
        if (!status)
        {
            target = route->target(context);
            if (target)
                status = route->send(context, target);
        }
        if (status >= 0)
        {
            status = route->finish(context);
            if (status >= 0)
                status = 1;
        }
    }
    dispatch->dispatching = 0;
    return status;
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
        *flags = 0;
        status = query->enable(context, filter.handler, flags);
        if (status)
            return status;
    }
    target = query->target(context);
    *flags = 0;
    return target ? query->enable(context, target, flags) : 0;
}
