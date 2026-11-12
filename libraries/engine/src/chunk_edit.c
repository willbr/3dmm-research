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
