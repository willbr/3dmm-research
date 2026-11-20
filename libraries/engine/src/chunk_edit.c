/* Chunk editing policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_edit.h"
#include <string.h>
#define EXTRA ((MMU32)1)
#define LONER ((MMU32)2)
#define OK MM_CHUNK_OK
#define BAD MM_CHUNK_BAD_FILE
static int readable(const MMChunkEdit *edit)
{
    return edit && edit->count && edit->entry;
}
MMChunkAccess mm_chunk_edit_access(const MMChunkEdit *edit)
{
    MMChunkAccess access = {0};
    if (readable(edit))
    {
        access.context = edit->context;
        access.count = edit->count(edit->context);
        access.entry = edit->entry;
        access.child = edit->child;
    }
    return access;
}
static int find(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 *position)
{
    const MMChunkAccess access = mm_chunk_edit_access(edit);
    return mm_chunk_find(&access, type, number, position);
}
static MMU32 index_children(void *p, MMU32 type, MMU32 number)
{
    const MMChunkEdit *edit = p;
    MMChunkEntry entry;
    MMU32 row;
    if (find(edit, type, number, &row) != OK || !edit->entry(edit->context, row, &entry))
        return 0;
    return entry.child_count;
}
static int index_child(void *p, MMU32 type, MMU32 number, MMU32 position, MMChunkChild *child)
{
    const MMChunkEdit *edit = p;
    const MMChunkAccess access = mm_chunk_edit_access(edit);
    MMU32 row;
    return mm_chunk_find(&access, type, number, &row) == OK &&
           mm_chunk_child_at(&access, row, position, child) == OK;
}
MMChunkGraph mm_chunk_edit_graph(const MMChunkEdit *edit)
{
    MMChunkGraph graph = {NULL, index_children, index_child};
    graph.context = (void *)edit;
    return graph;
}
static void output(const MMChunkEdit *edit, const MMChunkLocation *location, int added)
{
    if (edit->output)
        edit->output(edit->payload_context, location, added);
}
int mm_chunk_set_flag(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 flag,
                            int enabled)
{
    MMChunkEntry entry;
    MMU32 position, value = enabled ? flag : 0;
    int result;
    if (!readable(edit) || !edit->store ||
        (flag != MM_CHUNK_LONER && flag != MM_CHUNK_PACKED && flag != MM_CHUNK_FOREST) ||
        (flag != MM_CHUNK_LONER && !edit->reset_runtime))
        return BAD;
    result = find(edit, type, number, &position);
    if (result != OK)
        return result;
    if (!edit->entry(edit->context, position, &entry))
        return MM_CHUNK_IO;
    if (flag == MM_CHUNK_LONER)
    {
        if (!enabled)
            return entry.flags & LONER ? mm_chunk_delete(edit, type, number) : OK;
    }
    else if ((entry.flags & flag) == value)
        return OK;
    entry.flags = (entry.flags & ~flag) | value;
    edit->store(edit->context, position, &entry, 0);
    if (flag != MM_CHUNK_LONER)
        edit->reset_runtime(edit->context, type, number);
    return OK;
}
static void issue(const MMChunkEdit *edit, int kind)
{
    if (edit->issue)
        edit->issue(edit->context, kind);
}
int mm_chunk_unique(const MMChunkEdit *edit, MMU32 type, MMU32 *position, MMU32 *number)
{
    MMU32 i, n = 0, count;
    MMChunkEntry entry;
    int result;
    if (!readable(edit) || !position || !number)
        return BAD;
    result = find(edit, type, 0, &i);
    if (result == OK)
    {
        count = edit->count(edit->context);
        for (++i, n = 1; i < count; ++i, ++n)
        {
            if (!edit->entry(edit->context, i, &entry))
                return MM_CHUNK_IO;
            if (entry.type != type || entry.number != n)
                break;
        }
    }
    else if (result != MM_CHUNK_NOT_FOUND)
        return result;
    *position = i;
    *number = n;
    return OK;
}
int mm_chunk_add(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 position,
                       MMU32 bytes, MMChunkLocation *added)
{
    MMChunkEntry entry;
    MMChunkLocation location;
    if (!readable(edit) || !edit->insert || !edit->store || !edit->erase || !edit->allocate ||
        position > edit->count(edit->context) || bytes > 0x7fffffffUL)
        return BAD;
    if (!edit->insert(edit->context, position))
    {
        output(edit, NULL, 1);
        return MM_CHUNK_MEMORY;
    }
    if (!edit->allocate(edit->context, bytes, &location))
    {
        edit->erase(edit->context, position);
        output(edit, NULL, 1);
        return MM_CHUNK_IO;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = type;
    entry.number = number;
    entry.offset = location.offset;
    entry.bytes = location.bytes;
    entry.flags = (location.flags & EXTRA) | LONER;
    edit->store(edit->context, position, &entry, 1);
    output(edit, &location, 1);
    if (added)
        *added = location;
    return OK;
}
int mm_chunk_remove(const MMChunkEdit *edit, MMU32 position)
{
    MMChunkEntry entry;
    if (!readable(edit) || !edit->free_space || !edit->reset_runtime || !edit->erase ||
        position >= edit->count(edit->context))
        return BAD;
    if (!edit->entry(edit->context, position, &entry))
        return MM_CHUNK_IO;
    edit->free_space(edit->context, (entry.flags & EXTRA) != 0, entry.offset, entry.bytes);
    edit->reset_runtime(edit->context, entry.type, entry.number);
    edit->erase(edit->context, position);
    return OK;
}
int mm_chunk_put(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 bytes)
{
    MMU32 position;
    MMChunkEntry entry;
    MMChunkLocation location;
    int result;
    if (!readable(edit) || !edit->allocate || !edit->free_space || !edit->reset_runtime || !edit->store ||
        !edit->erase || !edit->insert || bytes > 0x7fffffffUL)
        return BAD;
    result = find(edit, type, number, &position);
    if (result == MM_CHUNK_NOT_FOUND)
    {
        result = mm_chunk_add(edit, type, number, position, bytes, &location);
        if (result != OK)
            return result;
        if (edit->write && !edit->write(edit->payload_context, &location, 1))
        {
            if (edit->entry(edit->context, position, &entry))
            {
                entry.flags &= ~LONER;
                edit->store(edit->context, position, &entry, 0);
                mm_chunk_remove(edit, position);
            }
            output(edit, NULL, 1);
            return MM_CHUNK_IO;
        }
        return OK;
    }
    if (result != OK)
        return result;
    if (!edit->entry(edit->context, position, &entry))
        return MM_CHUNK_IO;
    /* Existing allocation failure must leave an existing output block alone. */
    if (!edit->allocate(edit->context, bytes, &location))
        return MM_CHUNK_IO;
    output(edit, &location, 0);
    if (edit->write && !edit->write(edit->payload_context, &location, 0))
    {
        edit->free_space(edit->context, (location.flags & EXTRA) != 0, location.offset, location.bytes);
        output(edit, NULL, 0);
        return MM_CHUNK_IO;
    }
    edit->free_space(edit->context, (entry.flags & EXTRA) != 0, entry.offset, entry.bytes);
    entry.offset = location.offset;
    entry.bytes = location.bytes;
    entry.flags = (entry.flags & ~EXTRA) | (location.flags & EXTRA);
    edit->store(edit->context, position, &entry, 0);
    edit->reset_runtime(edit->context, type, number);
    return OK;
}
int mm_chunk_decrement(const MMChunkEdit *edit, MMU32 position, int *should_delete)
{
    MMChunkEntry entry;
    if (!readable(edit) || !edit->store || !should_delete || position >= edit->count(edit->context))
        return BAD;
    if (!edit->entry(edit->context, position, &entry))
        return MM_CHUNK_IO;
    if (!entry.parent_count)
        return BAD;
    --entry.parent_count;
    edit->store(edit->context, position, &entry, 0);
    *should_delete = !entry.parent_count && !(entry.flags & LONER);
    return OK;
}
int mm_chunk_delete(const MMChunkEdit *edit, MMU32 type, MMU32 number)
{
    const MMChunkGraph graph = mm_chunk_edit_graph(edit);
    MMChunkWalk walk;
    MMChunkEntry entry;
    MMChunkChild child, node;
    MMU32 position, child_position, children, flags;
    int skip = 0, remove, result;
    if (!readable(edit) || !edit->store || !edit->erase || !edit->free_space || !edit->reset_runtime || !edit->child)
        return BAD;
    result = find(edit, type, number, &position);
    if (result != OK)
        return result;
    if (!edit->entry(edit->context, position, &entry))
        return MM_CHUNK_IO;
    if (!(entry.flags & LONER) && entry.parent_count)
        return BAD;
    if (!entry.parent_count && mm_chunk_walk_init(&walk, edit->walk_allocator) != MM_CHUNK_OK)
        return BAD;
    entry.flags &= ~LONER;
    edit->store(edit->context, position, &entry, 0);
    if (entry.parent_count)
        return OK;
    mm_chunk_walk_reset(&walk, &graph, type, number);
    while (mm_chunk_walk_next(&walk, skip, &node, NULL, &flags))
    {
        skip = 1;
        if (find(edit, node.type, node.number, &position) != OK)
        {
            issue(edit, MM_EDIT_MISSING_NODE);
            continue;
        }
        if (flags & MM_WALK_PRE)
        {
            if (!(flags & MM_WALK_ROOT))
            {
                if (mm_chunk_decrement(edit, position, &remove) != OK)
                {
                    issue(edit, MM_EDIT_BAD_REFERENCE);
                    continue;
                }
                if (!remove)
                    continue;
            }
            skip = 0;
        }
        if (flags & MM_WALK_POST)
        {
            if (flags & MM_WALK_ERROR)
            {
                issue(edit, MM_EDIT_LOW_MEMORY);
                if (!edit->entry(edit->context, position, &entry))
                {
                    issue(edit, MM_EDIT_MISSING_NODE);
                    continue;
                }
                for (children = entry.child_count; children;)
                {
                    if (!edit->child(edit->context, position, --children, &child) ||
                        find(edit, child.type, child.number, &child_position) != OK)
                    {
                        issue(edit, MM_EDIT_MISSING_CHILD);
                        continue;
                    }
                    if (mm_chunk_decrement(edit, child_position, &remove) != OK)
                        issue(edit, MM_EDIT_BAD_REFERENCE);
                }
            }
            mm_chunk_remove(edit, position);
        }
    }
    mm_chunk_walk_dispose(&walk);
    return OK;
}
static int find_child(const MMChunkEdit *edit, MMU32 parent, const MMChunkChild *key,
                      MMU32 *position)
{
    const MMChunkAccess access = mm_chunk_edit_access(edit);
    return mm_chunk_find_child(&access, parent, key->slot, key->type, key->number, position);
}
static int adopt_at(const MMChunkEdit *edit, MMU32 parent, MMU32 position,
                    const MMChunkChild *child, int clear_loner)
{
    const MMChunkGraph graph = mm_chunk_edit_graph(edit);
    MMChunkEntry entry, target;
    MMU32 child_index, maximum = edit->max_references;
    int result;
    if (!edit->store || !edit->insert_child || !edit->erase_child || !maximum || maximum > 0x7fffffffUL)
        return BAD;
    result = find(edit, child->type, child->number, &child_index);
    if (result != OK)
        return result;
    if (!edit->entry(edit->context, parent, &entry) || !edit->entry(edit->context, child_index, &target))
        return MM_CHUNK_IO;
    if (position > entry.child_count)
        return BAD;
    if (mm_chunk_reaches(&graph, edit->walk_allocator, child->type, child->number,
                         entry.type, entry.number, entry.parent_count) != 0)
    {
        issue(edit, MM_EDIT_CYCLE_RISK);
        return BAD;
    }
    if (!edit->insert_child(edit->context, parent, position, child))
        return MM_CHUNK_MEMORY;
    if (!edit->entry(edit->context, parent, &entry))
    {
        edit->erase_child(edit->context, parent, position);
        return MM_CHUNK_IO;
    }
    if (entry.child_count >= maximum)
        goto overflow;
    ++entry.child_count;
    edit->store(edit->context, parent, &entry, 0);
    if (target.parent_count >= maximum)
    {
        --entry.child_count;
        edit->store(edit->context, parent, &entry, 0);
        goto overflow;
    }
    ++target.parent_count;
    if (clear_loner)
        target.flags &= ~LONER;
    edit->store(edit->context, child_index, &target, 0);
    return OK;
overflow:
    edit->erase_child(edit->context, parent, position);
    return BAD;
}
int mm_chunk_adopt(const MMChunkEdit *edit, MMU32 parent_type, MMU32 parent_number,
                         const MMChunkChild *child, int clear_loner)
{
    MMU32 parent, position;
    int result;
    if (!readable(edit) || !edit->child || !child)
        return BAD;
    result = find(edit, parent_type, parent_number, &parent);
    if (result != OK)
        return result;
    result = find_child(edit, parent, child, &position);
    return result == MM_CHUNK_NOT_FOUND ? adopt_at(edit, parent, position, child, clear_loner) : result;
}
int mm_chunk_unlink(const MMChunkEdit *edit, MMU32 parent_type, MMU32 parent_number,
                          const MMChunkChild *child)
{
    MMChunkEntry entry;
    MMU32 parent, target, position;
    int remove, result;
    if (!readable(edit) || !edit->child || !edit->store || !edit->erase_child || !child || !edit->erase ||
        !edit->free_space || !edit->reset_runtime)
        return BAD;
    result = find(edit, parent_type, parent_number, &parent);
    if (result == OK)
        result = find(edit, child->type, child->number, &target);
    if (result == OK)
        result = find_child(edit, parent, child, &position);
    if (result != OK)
        return result;
    if (!edit->entry(edit->context, parent, &entry))
        return MM_CHUNK_IO;
    --entry.child_count;
    edit->store(edit->context, parent, &entry, 0);
    edit->erase_child(edit->context, parent, position);
    if (mm_chunk_decrement(edit, target, &remove) != OK)
    {
        issue(edit, MM_EDIT_BAD_REFERENCE);
        return BAD;
    }
    if (remove)
        return mm_chunk_delete(edit, child->type, child->number);
    return OK;
}
int mm_chunk_change_slot(const MMChunkEdit *edit, MMU32 parent_type, MMU32 parent_number,
                               const MMChunkChild *key, MMU32 new_slot)
{
    MMChunkChild changed;
    MMU32 parent, source;
    int result;
    if (!readable(edit) || !edit->child || !edit->put_child || !key)
        return BAD;
    if (key->slot == new_slot)
        return OK;
    result = find(edit, parent_type, parent_number, &parent);
    if (result != OK)
        return result;
    changed = *key;
    changed.slot = new_slot;
    result = find_child(edit, parent, key, &source);
    if (result != OK)
        return result == MM_CHUNK_NOT_FOUND ? BAD : result;
    return mm_chunk_retarget(edit, parent, source, &changed);
}
int mm_chunk_retarget(const MMChunkEdit *edit, MMU32 parent, MMU32 source,
                            const MMChunkChild *changed)
{
    MMChunkEntry entry;
    MMChunkChild moving;
    MMU32 destination, i;
    int result;
    if (!readable(edit) || !edit->child || !edit->put_child || !changed || parent >= edit->count(edit->context))
        return BAD;
    if (!edit->entry(edit->context, parent, &entry))
        return MM_CHUNK_IO;
    if (source >= entry.child_count)
        return BAD;
    result = find_child(edit, parent, changed, &destination);
    if (result != MM_CHUNK_NOT_FOUND)
        return result == OK ? BAD : result;
    if (destination > source)
        --destination;
    if (source < destination)
        for (i = source; i < destination; ++i)
        {
            if (!edit->child(edit->context, parent, i + 1, &moving))
                return MM_CHUNK_IO;
            edit->put_child(edit->context, parent, i, &moving);
        }
    else
        for (i = source; i > destination; --i)
        {
            if (!edit->child(edit->context, parent, i - 1, &moving))
                return MM_CHUNK_IO;
            edit->put_child(edit->context, parent, i, &moving);
        }
    edit->put_child(edit->context, parent, destination, changed);
    return OK;
}
