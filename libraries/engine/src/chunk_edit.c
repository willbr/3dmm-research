/* Chunk editing policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_edit.h"
#include <string.h>
#define EXTRA ((ThreeMMU32)1)
#define LONER ((ThreeMMU32)2)
#define OK THREE_MM_CHUNK_OK
#define BAD THREE_MM_CHUNK_BAD_FILE
static int readable(const ThreeMMChunkEdit *edit)
{
    return edit && edit->count && edit->entry;
}
static int find(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *position)
{
    ThreeMMU32 low = 0, high = edit->count(edit->context), middle;
    ThreeMMChunkEntry entry;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        edit->entry(edit->context, middle, &entry);
        if (entry.type < type || (entry.type == type && entry.number < number))
            low = middle + 1;
        else if (entry.type > type || entry.number > number)
            high = middle;
        else
        {
            *position = middle;
            return 1;
        }
    }
    *position = low;
    return 0;
}
static void output(const ThreeMMChunkEdit *edit, const ThreeMMChunkLocation *location, int added)
{
    if (edit->output)
        edit->output(edit->context, location, added);
}
static void issue(const ThreeMMChunkEdit *edit, int kind)
{
    if (edit->issue)
        edit->issue(edit->context, kind);
}
int three_mm_chunk_unique(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 *position, ThreeMMU32 *number)
{
    ThreeMMU32 i, n = 0, count;
    ThreeMMChunkEntry entry;
    if (!readable(edit) || !position || !number)
        return BAD;
    if (find(edit, type, 0, &i))
    {
        count = edit->count(edit->context);
        for (++i, n = 1; i < count; ++i, ++n)
        {
            edit->entry(edit->context, i, &entry);
            if (entry.type != type || entry.number != n)
                break;
        }
    }
    *position = i;
    *number = n;
    return OK;
}
int three_mm_chunk_add(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 position,
                       ThreeMMU32 bytes, ThreeMMChunkLocation *added)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkLocation location;
    if (!readable(edit) || !edit->insert || !edit->store || !edit->erase || !edit->allocate ||
        position > edit->count(edit->context) || bytes > 0x7fffffffUL)
        return BAD;
    if (!edit->insert(edit->context, position))
    {
        output(edit, NULL, 1);
        return THREE_MM_CHUNK_MEMORY;
    }
    if (!edit->allocate(edit->context, bytes, &location))
    {
        edit->erase(edit->context, position);
        output(edit, NULL, 1);
        return THREE_MM_CHUNK_IO;
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
int three_mm_chunk_remove(const ThreeMMChunkEdit *edit, ThreeMMU32 position)
{
    ThreeMMChunkEntry entry;
    if (!readable(edit) || !edit->free_space || !edit->reset_runtime || !edit->erase ||
        position >= edit->count(edit->context))
        return BAD;
    edit->entry(edit->context, position, &entry);
    edit->free_space(edit->context, (entry.flags & EXTRA) != 0, entry.offset, entry.bytes);
    edit->reset_runtime(edit->context, entry.type, entry.number);
    edit->erase(edit->context, position);
    return OK;
}
int three_mm_chunk_put(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 bytes)
{
    ThreeMMU32 position;
    ThreeMMChunkEntry entry;
    ThreeMMChunkLocation location;
    int result;
    if (!readable(edit) || !edit->allocate || !edit->free_space || !edit->reset_runtime || !edit->store ||
        !edit->erase || !edit->insert || bytes > 0x7fffffffUL)
        return BAD;
    if (!find(edit, type, number, &position))
    {
        result = three_mm_chunk_add(edit, type, number, position, bytes, &location);
        if (result != OK)
            return result;
        if (edit->write && !edit->write(edit->context, &location, 1))
        {
            edit->entry(edit->context, position, &entry);
            entry.flags &= ~LONER;
            edit->store(edit->context, position, &entry, 0);
            three_mm_chunk_remove(edit, position);
            output(edit, NULL, 1);
            return THREE_MM_CHUNK_IO;
        }
        return OK;
    }
    /* Existing allocation failure must leave an existing output block alone. */
    if (!edit->allocate(edit->context, bytes, &location))
        return THREE_MM_CHUNK_IO;
    output(edit, &location, 0);
    if (edit->write && !edit->write(edit->context, &location, 0))
    {
        edit->free_space(edit->context, (location.flags & EXTRA) != 0, location.offset, location.bytes);
        output(edit, NULL, 0);
        return THREE_MM_CHUNK_IO;
    }
    edit->entry(edit->context, position, &entry);
    edit->free_space(edit->context, (entry.flags & EXTRA) != 0, entry.offset, entry.bytes);
    entry.offset = location.offset;
    entry.bytes = location.bytes;
    entry.flags = (entry.flags & ~EXTRA) | (location.flags & EXTRA);
    edit->store(edit->context, position, &entry, 0);
    edit->reset_runtime(edit->context, type, number);
    return OK;
}
int three_mm_chunk_decrement(const ThreeMMChunkEdit *edit, ThreeMMU32 position, int *should_delete)
{
    ThreeMMChunkEntry entry;
    if (!readable(edit) || !edit->store || !should_delete || position >= edit->count(edit->context))
        return BAD;
    edit->entry(edit->context, position, &entry);
    if (!entry.parent_count)
        return BAD;
    --entry.parent_count;
    edit->store(edit->context, position, &entry, 0);
    *should_delete = !entry.parent_count && !(entry.flags & LONER);
    return OK;
}
int three_mm_chunk_delete(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    ThreeMMChunkDeleteEvent event;
    ThreeMMU32 position, child_position, children;
    int skip = 0, remove;
    if (!readable(edit) || !edit->store || !edit->erase || !edit->free_space || !edit->reset_runtime || !edit->child ||
        !edit->begin_delete || !edit->next_delete)
        return BAD;
    if (!find(edit, type, number, &position))
        return THREE_MM_CHUNK_NOT_FOUND;
    edit->entry(edit->context, position, &entry);
    if (!(entry.flags & LONER) && entry.parent_count)
        return BAD;
    entry.flags &= ~LONER;
    edit->store(edit->context, position, &entry, 0);
    if (entry.parent_count)
        return OK;
    edit->begin_delete(edit->context, type, number);
    while (edit->next_delete(edit->context, skip, &event))
    {
        skip = 1;
        if (!find(edit, event.type, event.number, &position))
        {
            issue(edit, THREE_MM_EDIT_MISSING_NODE);
            continue;
        }
        if (event.flags & THREE_MM_DELETE_PRE)
        {
            if (!(event.flags & THREE_MM_DELETE_ROOT))
            {
                if (three_mm_chunk_decrement(edit, position, &remove) != OK)
                {
                    issue(edit, THREE_MM_EDIT_BAD_REFERENCE);
                    continue;
                }
                if (!remove)
                    continue;
            }
            skip = 0;
        }
        if (event.flags & THREE_MM_DELETE_POST)
        {
            if (event.flags & THREE_MM_DELETE_ERROR)
            {
                issue(edit, THREE_MM_EDIT_LOW_MEMORY);
                edit->entry(edit->context, position, &entry);
                for (children = entry.child_count; children;)
                {
                    edit->child(edit->context, position, --children, &child);
                    if (!find(edit, child.type, child.number, &child_position))
                    {
                        issue(edit, THREE_MM_EDIT_MISSING_CHILD);
                        continue;
                    }
                    if (three_mm_chunk_decrement(edit, child_position, &remove) != OK)
                        issue(edit, THREE_MM_EDIT_BAD_REFERENCE);
                }
            }
            three_mm_chunk_remove(edit, position);
        }
    }
    return OK;
}
static int find_child(const ThreeMMChunkEdit *edit, ThreeMMU32 parent, const ThreeMMChunkChild *key,
                      ThreeMMU32 *position)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    ThreeMMU32 low = 0, high, middle;
    edit->entry(edit->context, parent, &entry);
    high = entry.child_count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        edit->child(edit->context, parent, middle, &child);
        if (child.slot < key->slot ||
            (child.slot == key->slot &&
             (child.type < key->type || (child.type == key->type && child.number < key->number))))
            low = middle + 1;
        else if (child.slot == key->slot && child.type == key->type && child.number == key->number)
        {
            *position = middle;
            return 1;
        }
        else
            high = middle;
    }
    *position = low;
    return 0;
}
int three_mm_chunk_adopt_at(const ThreeMMChunkEdit *edit, ThreeMMU32 parent, ThreeMMU32 position,
                            const ThreeMMChunkChild *child, int clear_loner, ThreeMMU32 maximum)
{
    ThreeMMChunkEntry entry, target;
    ThreeMMU32 child_index;
    if (!readable(edit) || !edit->store || !edit->insert_child || !edit->erase_child || !edit->descendant || !child ||
        !maximum || maximum > 0x7fffffffUL || parent >= edit->count(edit->context))
        return BAD;
    if (!find(edit, child->type, child->number, &child_index))
        return THREE_MM_CHUNK_NOT_FOUND;
    edit->entry(edit->context, parent, &entry);
    if (position > entry.child_count)
        return BAD;
    if (edit->descendant(edit->context, child->type, child->number, entry.type, entry.number) != 0)
    {
        issue(edit, THREE_MM_EDIT_CYCLE_RISK);
        return BAD;
    }
    if (!edit->insert_child(edit->context, parent, position, child))
        return THREE_MM_CHUNK_MEMORY;
    edit->entry(edit->context, parent, &entry);
    if (entry.child_count >= maximum)
        goto overflow;
    ++entry.child_count;
    edit->store(edit->context, parent, &entry, 0);
    edit->entry(edit->context, child_index, &target);
    if (target.parent_count >= maximum)
    {
        edit->entry(edit->context, parent, &entry);
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
int three_mm_chunk_adopt(const ThreeMMChunkEdit *edit, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                         const ThreeMMChunkChild *child, int clear_loner, ThreeMMU32 maximum)
{
    ThreeMMU32 parent, position;
    if (!readable(edit) || !edit->child || !child)
        return BAD;
    if (!find(edit, parent_type, parent_number, &parent))
        return THREE_MM_CHUNK_NOT_FOUND;
    if (find_child(edit, parent, child, &position))
        return OK;
    return three_mm_chunk_adopt_at(edit, parent, position, child, clear_loner, maximum);
}
int three_mm_chunk_unlink(const ThreeMMChunkEdit *edit, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                          const ThreeMMChunkChild *child)
{
    ThreeMMChunkEntry entry;
    ThreeMMU32 parent, target, position;
    int remove;
    if (!readable(edit) || !edit->child || !edit->store || !edit->erase_child || !child || !edit->erase ||
        !edit->begin_delete || !edit->next_delete || !edit->free_space || !edit->reset_runtime)
        return BAD;
    if (!find(edit, parent_type, parent_number, &parent) || !find(edit, child->type, child->number, &target) ||
        !find_child(edit, parent, child, &position))
        return THREE_MM_CHUNK_NOT_FOUND;
    edit->entry(edit->context, parent, &entry);
    --entry.child_count;
    edit->store(edit->context, parent, &entry, 0);
    edit->erase_child(edit->context, parent, position);
    if (three_mm_chunk_decrement(edit, target, &remove) != OK)
    {
        issue(edit, THREE_MM_EDIT_BAD_REFERENCE);
        return BAD;
    }
    if (remove)
        return three_mm_chunk_delete(edit, child->type, child->number);
    return OK;
}
int three_mm_chunk_change_slot(const ThreeMMChunkEdit *edit, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                               const ThreeMMChunkChild *key, ThreeMMU32 new_slot)
{
    ThreeMMChunkChild changed, moving;
    ThreeMMU32 parent, source, destination, i;
    if (!readable(edit) || !edit->child || !edit->put_child || !key)
        return BAD;
    if (key->slot == new_slot)
        return OK;
    if (!find(edit, parent_type, parent_number, &parent))
        return THREE_MM_CHUNK_NOT_FOUND;
    changed = *key;
    changed.slot = new_slot;
    if (!find_child(edit, parent, key, &source) || find_child(edit, parent, &changed, &destination))
        return BAD;
    if (destination > source)
        --destination;
    if (source < destination)
        for (i = source; i < destination; ++i)
        {
            edit->child(edit->context, parent, i + 1, &moving);
            edit->put_child(edit->context, parent, i, &moving);
        }
    else
        for (i = source; i > destination; --i)
        {
            edit->child(edit->context, parent, i - 1, &moving);
            edit->put_child(edit->context, parent, i, &moving);
        }
    edit->put_child(edit->context, parent, destination, &changed);
    return OK;
}
