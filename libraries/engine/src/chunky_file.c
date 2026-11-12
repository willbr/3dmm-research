/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunky_file.h"
#include <stdlib.h>
#include <string.h>

#define CHUNK_GROUP_LIMIT 0x08000000UL

struct ThreeMMChunkIndex
{
    ThreeMMChunkHeader header;
    ThreeMMChunkPolicy policy;
    unsigned char *wire;
    ThreeMMU32 count, data_bytes, stride;
    int big;
};
static unsigned int word16(const unsigned char *bytes, int big)
{
    return big ? (unsigned int)bytes[0] * 256U + bytes[1] : (unsigned int)bytes[1] * 256U + bytes[0];
}
static ThreeMMU32 word32(const unsigned char *bytes, int big)
{
    ThreeMMU32 value = 0;
    int i;
    for (i = 0; i < 4; ++i)
        value = (value << 8) | bytes[big ? i : 3 - i];
    return value;
}
static ThreeMMI32 signed32(ThreeMMU32 value)
{
    return value <= 0x7fffffffUL ? (ThreeMMI32)value : -1 - (ThreeMMI32)((ThreeMMU32)0xffffffffUL - value);
}
static int signed16(unsigned int value)
{
    return value <= 32767U ? (int)value : -1 - (int)(65535U - value);
}
int three_mm_chunk_read_header(const ThreeMMReadStream *stream, ThreeMMChunkHeader *header)
{
    unsigned char bytes[THREE_MM_CHUNK_PREFIX_BYTES];
    ThreeMMChunkHeader result;
    if (!stream || !header || stream->length < sizeof(bytes))
        return THREE_MM_CHUNK_BAD_FILE;
    if (!three_mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return THREE_MM_CHUNK_IO;
    if (bytes[0] != 'C' || bytes[1] != 'H' || bytes[2] != 'N' || bytes[3] != '2' ||
        !((bytes[12] == 1 && bytes[13] == 0) || (bytes[12] == 0 && bytes[13] == 1)))
        return THREE_MM_CHUNK_BAD_FILE;
    result.big_endian = bytes[12] == 0;
    result.creator = word32(bytes + 4, result.big_endian);
    result.version = signed16(word16(bytes + 8, result.big_endian));
    result.backward_version = signed16(word16(bytes + 10, result.big_endian));
    result.system = signed16(word16(bytes + 14, result.big_endian));
    result.logical_end = word32(bytes + 16, result.big_endian);
    result.index_offset = word32(bytes + 20, result.big_endian);
    result.index_bytes = word32(bytes + 24, result.big_endian);
    result.map_offset = word32(bytes + 28, result.big_endian);
    result.map_bytes = word32(bytes + 32, result.big_endian);
    if (result.version < 1 || result.backward_version > THREE_MM_CHUNK_CURRENT_VERSION ||
        result.logical_end < sizeof(bytes) || result.logical_end > stream->length ||
        result.logical_end > 0x7fffffffUL || result.index_offset < sizeof(bytes) ||
        result.index_offset > result.logical_end || !result.index_bytes ||
        result.index_bytes > result.logical_end - result.index_offset ||
        result.map_offset != result.index_offset + result.index_bytes ||
        result.map_bytes != result.logical_end - result.map_offset)
        return THREE_MM_CHUNK_BAD_FILE;
    *header = result;
    return THREE_MM_CHUNK_OK;
}
static void write16(unsigned char *bytes, unsigned int value, int big)
{
    bytes[big ? 1 : 0] = (unsigned char)value;
    bytes[big ? 0 : 1] = (unsigned char)(value >> 8);
}
static void write32(unsigned char *bytes, ThreeMMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
        bytes[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}
int three_mm_chunk_write_index(const ThreeMMChunkSave *save, const ThreeMMChunkWriter *writer,
                               ThreeMMChunkHeader *committed)
{
    unsigned char bytes[THREE_MM_CHUNK_PREFIX_BYTES];
    ThreeMMChunkHeader header;
    if (!save || !writer || !writer->index || !writer->prefix || (save->map_bytes && !writer->map) ||
        (save->big_endian != 0 && save->big_endian != 1) || save->system < -32768 || save->system > 32767 ||
        save->index_offset < sizeof(bytes) || save->index_offset > 0x7fffffffUL || !save->index_bytes ||
        save->index_bytes > 0x7fffffffUL - save->index_offset ||
        save->map_bytes > 0x7fffffffUL - save->index_offset - save->index_bytes)
        return THREE_MM_CHUNK_BAD_FILE;
    header.creator = save->creator;
    header.version = THREE_MM_CHUNK_CURRENT_VERSION;
    header.backward_version = THREE_MM_CHUNK_BACKWARD_VERSION;
    header.big_endian = save->big_endian;
    header.system = save->system;
    header.index_offset = save->index_offset;
    header.index_bytes = save->index_bytes;
    header.map_offset = header.index_offset + header.index_bytes;
    header.map_bytes = save->map_bytes;
    header.logical_end = header.map_offset + header.map_bytes;
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "CHN2", 4);
    write32(bytes + 4, header.creator, header.big_endian);
    write16(bytes + 8, (unsigned)header.version, header.big_endian);
    write16(bytes + 10, (unsigned)header.backward_version, header.big_endian);
    write16(bytes + 12, 1, header.big_endian);
    write16(bytes + 14, (unsigned)header.system, header.big_endian);
    write32(bytes + 16, header.logical_end, header.big_endian);
    write32(bytes + 20, header.index_offset, header.big_endian);
    write32(bytes + 24, header.index_bytes, header.big_endian);
    write32(bytes + 28, header.map_offset, header.big_endian);
    write32(bytes + 32, header.map_bytes, header.big_endian);
    if (!writer->index(writer->context, header.index_offset, header.index_bytes) ||
        (header.map_bytes && !writer->map(writer->context, header.map_offset, header.map_bytes)) ||
        !writer->prefix(writer->context, bytes, sizeof(bytes)))
        return THREE_MM_CHUNK_IO;
    if (committed)
        *committed = header;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_at(const ThreeMMChunkAccess *access, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    ThreeMMChunkEntry result;
    if (!access || !entry || position >= access->count)
        return THREE_MM_CHUNK_NOT_FOUND;
    if (!access->entry || !access->entry(access->context, position, &result))
        return THREE_MM_CHUNK_IO;
    *entry = result;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_find(const ThreeMMChunkAccess *access, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *position)
{
    ThreeMMU32 low = 0, high, middle;
    ThreeMMChunkEntry entry;
    int result;
    if (!access || !position)
        return THREE_MM_CHUNK_BAD_FILE;
    high = access->count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = three_mm_chunk_at(access, middle, &entry);
        if (result != THREE_MM_CHUNK_OK)
            return result;
        if (entry.type < type || (entry.type == type && entry.number < number))
            low = middle + 1;
        else if (entry.type > type || entry.number > number)
            high = middle;
        else
        {
            *position = middle;
            return THREE_MM_CHUNK_OK;
        }
    }
    *position = low;
    return THREE_MM_CHUNK_NOT_FOUND;
}
int three_mm_chunk_type_at(const ThreeMMChunkAccess *access, ThreeMMU32 type, ThreeMMU32 ordinal,
                           ThreeMMChunkEntry *entry, ThreeMMU32 *position)
{
    ThreeMMU32 first;
    ThreeMMChunkEntry value;
    int result;
    if (!access || !entry || !position)
        return THREE_MM_CHUNK_BAD_FILE;
    result = three_mm_chunk_find(access, type, 0, &first);
    if (result != THREE_MM_CHUNK_OK && result != THREE_MM_CHUNK_NOT_FOUND)
        return result;
    if (ordinal >= access->count - first)
        return THREE_MM_CHUNK_NOT_FOUND;
    first += ordinal;
    result = three_mm_chunk_at(access, first, &value);
    if (result != THREE_MM_CHUNK_OK)
        return result;
    if (value.type != type)
        return THREE_MM_CHUNK_NOT_FOUND;
    *entry = value;
    *position = first;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_child_at(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 position,
                            ThreeMMChunkChild *child)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild result;
    int status = three_mm_chunk_at(access, parent, &entry);
    if (status != THREE_MM_CHUNK_OK)
        return status;
    if (!child || position >= entry.child_count)
        return THREE_MM_CHUNK_NOT_FOUND;
    if (!access->child || !access->child(access->context, parent, position, &result))
        return THREE_MM_CHUNK_IO;
    *child = result;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_find_child(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 slot, ThreeMMU32 type,
                              ThreeMMU32 number, ThreeMMU32 *position)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    ThreeMMU32 low = 0, high, middle;
    int result = three_mm_chunk_at(access, parent, &entry);
    if (!position)
        return THREE_MM_CHUNK_BAD_FILE;
    if (result != THREE_MM_CHUNK_OK)
        return result;
    high = entry.child_count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = three_mm_chunk_child_at(access, parent, middle, &child);
        if (result != THREE_MM_CHUNK_OK)
            return result;
        if (child.slot < slot || (child.slot == slot && child.type < type) ||
            (child.slot == slot && child.type == type && child.number < number))
            low = middle + 1;
        else if (child.slot != slot || child.type != type || child.number != number)
            high = middle;
        else
        {
            *position = middle;
            return THREE_MM_CHUNK_OK;
        }
    }
    *position = low;
    return THREE_MM_CHUNK_NOT_FOUND;
}
int three_mm_chunk_child_type(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 slot, ThreeMMU32 type,
                              ThreeMMChunkChild *child)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild value;
    ThreeMMU32 low = 0, high, middle;
    int result = three_mm_chunk_at(access, parent, &entry);
    if (result != THREE_MM_CHUNK_OK)
        return result;
    if (!child)
        return THREE_MM_CHUNK_BAD_FILE;
    high = entry.child_count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = three_mm_chunk_child_at(access, parent, middle, &value);
        if (result != THREE_MM_CHUNK_OK)
            return result;
        if (value.slot < slot || (value.slot == slot && value.type < type))
            low = middle + 1;
        else
            high = middle;
    }
    if (low == entry.child_count)
        return THREE_MM_CHUNK_NOT_FOUND;
    result = three_mm_chunk_child_at(access, parent, low, &value);
    if (result != THREE_MM_CHUNK_OK)
        return result;
    if (value.slot != slot || value.type < type)
        return THREE_MM_CHUNK_NOT_FOUND;
    *child = value;
    return THREE_MM_CHUNK_OK;
}

static int wire_row(const ThreeMMChunkIndex *index, ThreeMMU32 position, const unsigned char **row,
                    ThreeMMU32 *variable_bytes)
{
    const unsigned char *location;
    ThreeMMU32 offset, count;
    if (position >= index->count)
        return 0;
    location = index->wire + 20 + index->data_bytes + position * 8;
    offset = word32(location, index->big);
    count = word32(location + 4, index->big);
    if (offset > index->data_bytes || count < index->stride || count > index->data_bytes - offset)
        return 0;
    *row = index->wire + 20 + offset;
    *variable_bytes = count - index->stride;
    return 1;
}
static int wire_entry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    const ThreeMMChunkIndex *index = (const ThreeMMChunkIndex *)context;
    const unsigned char *row;
    ThreeMMU32 variable, flags, bytes, children, parents;
    if (!wire_row(index, position, &row, &variable) || variable >= CHUNK_GROUP_LIMIT)
        return 0;
    if (index->stride == 32)
    {
        /* Preserve the expanded-to-compact reader's pre-swap size checks. */
        if (!index->policy.expanded_runtime &&
            (signed32(word32(row + 12, index->policy.native_big_endian)) > 0x00ffffffL ||
             signed32(word32(row + 16, index->policy.native_big_endian)) > 65535L ||
             signed32(word32(row + 20, index->policy.native_big_endian)) > 65535L))
            return 0;
        bytes = word32(row + 12, index->big);
        children = word32(row + 16, index->big);
        parents = word32(row + 20, index->big);
        if (index->header.version < 4)
            flags = (row[29] ? THREE_MM_CHUNK_LONER : 0) | (row[30] ? THREE_MM_CHUNK_PACKED : 0);
        else
            flags = word32(row + 28, index->big) & ~(ThreeMMU32)1;
        if (!parents)
            flags |= THREE_MM_CHUNK_LONER;
        if (!index->policy.expanded_runtime)
        {
            bytes &= 0x00ffffffUL;
            parents &= 65535U;
            flags &= 255U;
        }
    }
    else
    {
        bytes = word32(row + 12, index->big);
        flags = (bytes & 255U) & ~1U;
        bytes >>= 8;
        children = word16(row + 16, index->big);
        parents = word16(row + 18, index->big);
        if (!parents && !(flags & THREE_MM_CHUNK_LONER))
            return 0;
    }
    if (children > variable / 12 || variable - children * 12 > index->policy.max_name_bytes)
        return 0;
    entry->type = word32(row, index->big);
    entry->number = word32(row + 4, index->big);
    entry->offset = word32(row + 8, index->big);
    entry->bytes = bytes;
    entry->flags = flags;
    entry->child_count = children;
    entry->parent_count = parents;
    entry->variable_bytes = variable;
    return 1;
}
static int wire_child(void *context, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *child)
{
    const ThreeMMChunkIndex *index = (const ThreeMMChunkIndex *)context;
    const unsigned char *row;
    ThreeMMU32 variable;
    ThreeMMChunkEntry entry;
    if (!wire_entry(context, parent, &entry) || position >= entry.child_count ||
        !wire_row(index, parent, &row, &variable))
        return 0;
    row += index->stride + position * 12;
    child->type = word32(row, index->big);
    child->number = word32(row + 4, index->big);
    child->slot = word32(row + 8, index->big);
    return 1;
}
int three_mm_chunk_index_open(const ThreeMMReadStream *stream, const ThreeMMChunkPolicy *policy,
                              ThreeMMChunkIndex **output)
{
    unsigned char group[20];
    ThreeMMChunkHeader header;
    ThreeMMChunkIndex *index;
    ThreeMMChunkEntry entry;
    ThreeMMU32 i, bytes, occupied = 0;
    int status;
    if (!policy || !output)
        return THREE_MM_CHUNK_BAD_FILE;
    status = three_mm_chunk_read_header(stream, &header);
    if (status != THREE_MM_CHUNK_OK)
        return status;
    if (header.index_bytes < 20)
        return THREE_MM_CHUNK_BAD_FILE;
    if (!three_mm_read_exact(stream, header.index_offset, group, sizeof(group)))
        return THREE_MM_CHUNK_IO;
    /* Validate the two legacy GG storage sizes before allocating the wire copy. */
    {
        int big = group[0] == 0 && group[1] == 1;
        ThreeMMU32 data = word32(group + 8, big), available = header.index_bytes - 20;
        if ((!big && !(group[0] == 1 && group[1] == 0)) || data > available || data >= CHUNK_GROUP_LIMIT ||
            available - data >= CHUNK_GROUP_LIMIT || (available - data) % 8 ||
            word32(group + 4, big) != (available - data) / 8 || word32(group + 12, big) != 0xffffffffUL)
            return THREE_MM_CHUNK_BAD_FILE;
    }
    if ((size_t)header.index_bytes != header.index_bytes)
        return THREE_MM_CHUNK_MEMORY;
    index = (ThreeMMChunkIndex *)malloc(sizeof(*index));
    if (!index)
        return THREE_MM_CHUNK_MEMORY;
    index->wire = (unsigned char *)malloc((size_t)header.index_bytes);
    if (!index->wire)
    {
        free(index);
        return THREE_MM_CHUNK_MEMORY;
    }
    index->header = header;
    index->policy = *policy;
    memcpy(index->wire, group, sizeof(group));
    bytes = word32(group + 8, group[0] == 0);
    if (!three_mm_read_exact(stream, header.index_offset + 20, index->wire + 20, (size_t)bytes) ||
        !three_mm_read_exact(stream, header.index_offset + 20 + bytes, index->wire + 20 + bytes,
                             (size_t)(header.index_bytes - 20 - bytes)))
    {
        three_mm_chunk_index_close(index);
        return THREE_MM_CHUNK_IO;
    }
    index->big = index->wire[0] == 0 && index->wire[1] == 1;
    if (!index->big && !(index->wire[0] == 1 && index->wire[1] == 0))
        goto bad;
    index->count = word32(index->wire + 4, index->big);
    index->data_bytes = word32(index->wire + 8, index->big);
    index->stride = word32(index->wire + 16, index->big);
    bytes = header.index_bytes - 20;
    if (word32(index->wire + 12, index->big) != 0xffffffffUL || index->data_bytes > bytes ||
        index->count != (bytes - index->data_bytes) / 8 || (bytes - index->data_bytes) % 8 ||
        (index->stride != 32 && (header.version < 4 || index->stride != 20)))
        goto bad;
    for (i = 0; i < index->count; ++i)
    {
        const unsigned char *row;
        ThreeMMU32 variable, rounded, offset;
        if (!wire_entry(index, i, &entry) || !wire_row(index, i, &row, &variable))
            goto bad;
        offset = (ThreeMMU32)(row - index->wire - 20);
        rounded = (index->stride + variable + 3U) & ~(ThreeMMU32)3;
        if (rounded > index->data_bytes - offset || rounded > index->data_bytes - occupied)
            goto bad;
        occupied += rounded;
    }
    if (occupied != index->data_bytes)
        goto bad;
    *output = index;
    return THREE_MM_CHUNK_OK;
bad:
    three_mm_chunk_index_close(index);
    return THREE_MM_CHUNK_BAD_FILE;
}
void three_mm_chunk_index_close(ThreeMMChunkIndex *index)
{
    if (index)
    {
        free(index->wire);
        free(index);
    }
}
ThreeMMChunkAccess three_mm_chunk_index_access(ThreeMMChunkIndex *index)
{
    ThreeMMChunkAccess access;
    access.context = index;
    access.count = index->count;
    access.entry = wire_entry;
    access.child = wire_child;
    return access;
}
const ThreeMMChunkHeader *three_mm_chunk_index_header(const ThreeMMChunkIndex *index)
{
    return &index->header;
}
int three_mm_chunk_index_name(const ThreeMMChunkIndex *index, ThreeMMU32 position, const unsigned char **bytes,
                              ThreeMMU32 *count)
{
    const unsigned char *row;
    ThreeMMU32 variable;
    ThreeMMChunkEntry entry;
    if (!index || !bytes || !count || !wire_entry((void *)index, position, &entry) ||
        !wire_row(index, position, &row, &variable))
        return THREE_MM_CHUNK_BAD_FILE;
    *bytes = row + index->stride + entry.child_count * 12;
    *count = variable - entry.child_count * 12;
    return THREE_MM_CHUNK_OK;
}

typedef struct GraphFrame
{
    ThreeMMU32 node, next, children;
} GraphFrame;
int three_mm_chunk_validate_graph(const ThreeMMChunkAccess *access, ThreeMMU32 max_references)
{
    unsigned char *marks;
    GraphFrame *stack = NULL, *grown;
    ThreeMMU32 capacity = 0, depth, i, target, refs = 0, children = 0;
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    int result = THREE_MM_CHUNK_BAD_FILE;
    if (!access)
        return result;
    for (i = 0; i < access->count; ++i)
    {
        if (three_mm_chunk_at(access, i, &entry) != THREE_MM_CHUNK_OK ||
            entry.child_count > entry.variable_bytes / 12 || entry.parent_count > max_references)
            return result;
        refs += entry.parent_count;
        children += entry.child_count;
    }
    if (refs != children)
        return result;
    if (!access->count)
        return THREE_MM_CHUNK_OK;
    if ((size_t)access->count != access->count)
        return THREE_MM_CHUNK_MEMORY;
    marks = (unsigned char *)calloc((size_t)access->count, 1);
    if (!marks)
        return THREE_MM_CHUNK_MEMORY;
    for (i = access->count; i-- > 0;)
    {
        if (three_mm_chunk_at(access, i, &entry) != THREE_MM_CHUNK_OK)
            goto done;
        if (entry.parent_count || !entry.child_count || (marks[i] & 2))
            continue;
        depth = 0;
        target = i;
        for (;;)
        {
            if (depth == capacity)
            {
                ThreeMMU32 next = capacity ? capacity * 2 : 32;
                if (next < capacity || next > access->count)
                    next = access->count;
                if ((size_t)next != next || (size_t)next > (size_t)-1 / sizeof(*stack))
                {
                    result = THREE_MM_CHUNK_MEMORY;
                    goto done;
                }
                grown = (GraphFrame *)realloc(stack, (size_t)next * sizeof(*stack));
                if (!grown)
                {
                    result = THREE_MM_CHUNK_MEMORY;
                    goto done;
                }
                stack = grown;
                capacity = next;
            }
            if (three_mm_chunk_at(access, target, &entry) != THREE_MM_CHUNK_OK)
                goto done;
            stack[depth].node = target;
            stack[depth].next = 0;
            stack[depth].children = entry.child_count;
            marks[target] |= 1;
            ++depth;
            while (depth)
            {
                GraphFrame *frame = &stack[depth - 1];
                if (frame->next == frame->children)
                {
                    marks[frame->node] = (unsigned char)((marks[frame->node] & ~1U) | 2);
                    --depth;
                    continue;
                }
                if (three_mm_chunk_child_at(access, frame->node, frame->next++, &child) != THREE_MM_CHUNK_OK ||
                    three_mm_chunk_find(access, child.type, child.number, &target) != THREE_MM_CHUNK_OK)
                    goto done;
                marks[target] |= 4;
                if (marks[target] & 1)
                    goto done;
                if (!(marks[target] & 2))
                    break;
            }
            if (!depth)
                break;
        }
    }
    for (i = 0; i < access->count; ++i)
    {
        if (three_mm_chunk_at(access, i, &entry) != THREE_MM_CHUNK_OK ||
            ((marks[i] & 4) != 0) != (entry.parent_count != 0))
            goto done;
    }
    result = THREE_MM_CHUNK_OK;
done:
    free(stack);
    free(marks);
    return result;
}
