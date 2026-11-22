/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_file.h"
#include <stdlib.h>
#include <string.h>

#define CHUNK_GROUP_LIMIT 0x08000000UL

struct MMChunkIndex
{
    MMChunkHeader header;
    MMChunkPolicy policy;
    unsigned char *wire;
    MMU32 references, count, data_bytes, stride;
    int big;
};
static unsigned int word16(const unsigned char *bytes, int big)
{
    return big ? (unsigned int)bytes[0] * 256U + bytes[1] : (unsigned int)bytes[1] * 256U + bytes[0];
}
static MMU32 word32(const unsigned char *bytes, int big)
{
    MMU32 value = 0;
    int i;
    for (i = 0; i < 4; ++i)
        value = (value << 8) | bytes[big ? i : 3 - i];
    return value;
}
static MMI32 signed32(MMU32 value)
{
    return value <= 0x7fffffffUL ? (MMI32)value : -1 - (MMI32)((MMU32)0xffffffffUL - value);
}
static int signed16(unsigned int value)
{
    return value <= 32767U ? (int)value : -1 - (int)(65535U - value);
}
int mm_chunk_read_header(const MMReadStream *stream, MMChunkHeader *header)
{
    unsigned char bytes[MM_CHUNK_PREFIX_BYTES];
    MMChunkHeader result;
    if (!stream || !header || stream->length < sizeof(bytes))
        return MM_CHUNK_BAD_FILE;
    if (!mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return MM_CHUNK_IO;
    if (bytes[0] != 'C' || bytes[1] != 'H' || bytes[2] != 'N' || bytes[3] != '2' ||
        !((bytes[12] == 1 && bytes[13] == 0) || (bytes[12] == 0 && bytes[13] == 1)))
        return MM_CHUNK_BAD_FILE;
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
    if (result.version < 1 || result.backward_version > MM_CHUNK_CURRENT_VERSION ||
        result.logical_end < sizeof(bytes) || result.logical_end > stream->length ||
        result.logical_end > 0x7fffffffUL || result.index_offset < sizeof(bytes) ||
        result.index_offset > result.logical_end || !result.index_bytes ||
        result.index_bytes > result.logical_end - result.index_offset ||
        result.map_offset != result.index_offset + result.index_bytes ||
        result.map_bytes != result.logical_end - result.map_offset)
        return MM_CHUNK_BAD_FILE;
    *header = result;
    return MM_CHUNK_OK;
}
int mm_chunk_stored_extent(const MMChunkHeader *header, const MMChunkEntry *entry)
{
    MMU32 start = entry->offset, bytes = entry->bytes;
    MMU32 end = header->index_offset;
    return (start >= MM_CHUNK_PREFIX_BYTES || (!start && !bytes)) && start <= end && bytes <= end - start;
}
static void write16(unsigned char *bytes, unsigned int value, int big)
{
    bytes[big ? 1 : 0] = (unsigned char)value;
    bytes[big ? 0 : 1] = (unsigned char)(value >> 8);
}
static void write32(unsigned char *bytes, MMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
        bytes[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}
int mm_chunk_write_index(const MMChunkSave *save, const MMChunkWriter *writer,
                               MMChunkHeader *committed)
{
    unsigned char bytes[MM_CHUNK_PREFIX_BYTES];
    MMChunkHeader header;
    if (!save || !writer || !writer->index || !writer->prefix || (save->map_bytes && !writer->map) ||
        (save->big_endian != 0 && save->big_endian != 1) || save->system < -32768 || save->system > 32767 ||
        save->index_offset < sizeof(bytes) || save->index_offset > 0x7fffffffUL || !save->index_bytes ||
        save->index_bytes > 0x7fffffffUL - save->index_offset ||
        save->map_bytes > 0x7fffffffUL - save->index_offset - save->index_bytes)
        return MM_CHUNK_BAD_FILE;
    header.creator = save->creator;
    header.version = MM_CHUNK_CURRENT_VERSION;
    header.backward_version = MM_CHUNK_BACKWARD_VERSION;
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
        return MM_CHUNK_IO;
    if (committed)
        *committed = header;
    return MM_CHUNK_OK;
}
int mm_chunk_at(const MMChunkAccess *access, MMU32 position, MMChunkEntry *entry)
{
    MMChunkEntry result;
    if (!access || !entry || position >= access->count)
        return MM_CHUNK_NOT_FOUND;
    if (!access->entry || !access->entry(access->context, position, &result))
        return MM_CHUNK_IO;
    *entry = result;
    return MM_CHUNK_OK;
}
int mm_chunk_keys_ordered(const MMChunkAccess *access)
{
    MMChunkEntry entry, previous = {0};
    MMU32 i;
    for (i = 0; i < access->count; ++i)
    {
        if (mm_chunk_at(access, i, &entry) != MM_CHUNK_OK ||
            (i && (entry.type < previous.type ||
                  (entry.type == previous.type && entry.number <= previous.number))))
            return 0;
        previous = entry;
    }
    return 1;
}
int mm_chunk_find(const MMChunkAccess *access, MMU32 type, MMU32 number, MMU32 *position)
{
    MMU32 low = 0, high, middle;
    MMChunkEntry entry;
    int result;
    if (!access || !position)
        return MM_CHUNK_BAD_FILE;
    high = access->count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = mm_chunk_at(access, middle, &entry);
        if (result != MM_CHUNK_OK)
            return result;
        if (entry.type < type || (entry.type == type && entry.number < number))
            low = middle + 1;
        else if (entry.type > type || entry.number > number)
            high = middle;
        else
        {
            *position = middle;
            return MM_CHUNK_OK;
        }
    }
    *position = low;
    return MM_CHUNK_NOT_FOUND;
}
int mm_chunk_type_at(const MMChunkAccess *access, MMU32 type, MMU32 ordinal,
                           MMChunkEntry *entry, MMU32 *position)
{
    MMU32 first;
    MMChunkEntry value;
    int result;
    if (!access || !entry || !position)
        return MM_CHUNK_BAD_FILE;
    result = mm_chunk_find(access, type, 0, &first);
    if (result != MM_CHUNK_OK && result != MM_CHUNK_NOT_FOUND)
        return result;
    if (ordinal >= access->count - first)
        return MM_CHUNK_NOT_FOUND;
    first += ordinal;
    result = mm_chunk_at(access, first, &value);
    if (result != MM_CHUNK_OK)
        return result;
    if (value.type != type)
        return MM_CHUNK_NOT_FOUND;
    *entry = value;
    *position = first;
    return MM_CHUNK_OK;
}
int mm_chunk_child_at(const MMChunkAccess *access, MMU32 parent, MMU32 position,
                            MMChunkChild *child)
{
    MMChunkEntry entry;
    MMChunkChild result;
    int status = mm_chunk_at(access, parent, &entry);
    if (status != MM_CHUNK_OK)
        return status;
    if (!child || position >= entry.child_count)
        return MM_CHUNK_NOT_FOUND;
    if (!access->child || !access->child(access->context, parent, position, &result))
        return MM_CHUNK_IO;
    *child = result;
    return MM_CHUNK_OK;
}
int mm_chunk_find_child(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                              MMU32 number, MMU32 *position)
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 low = 0, high, middle;
    int result = mm_chunk_at(access, parent, &entry);
    if (!position)
        return MM_CHUNK_BAD_FILE;
    if (result != MM_CHUNK_OK)
        return result;
    high = entry.child_count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = mm_chunk_child_at(access, parent, middle, &child);
        if (result != MM_CHUNK_OK)
            return result;
        if (child.slot < slot || (child.slot == slot && child.type < type) ||
            (child.slot == slot && child.type == type && child.number < number))
            low = middle + 1;
        else if (child.slot != slot || child.type != type || child.number != number)
            high = middle;
        else
        {
            *position = middle;
            return MM_CHUNK_OK;
        }
    }
    *position = low;
    return MM_CHUNK_NOT_FOUND;
}
int mm_chunk_child_type(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                              MMChunkChild *child)
{
    MMChunkEntry entry;
    MMChunkChild value;
    MMU32 low = 0, high, middle;
    int result = mm_chunk_at(access, parent, &entry);
    if (result != MM_CHUNK_OK)
        return result;
    if (!child)
        return MM_CHUNK_BAD_FILE;
    high = entry.child_count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        result = mm_chunk_child_at(access, parent, middle, &value);
        if (result != MM_CHUNK_OK)
            return result;
        if (value.slot < slot || (value.slot == slot && value.type < type))
            low = middle + 1;
        else
            high = middle;
    }
    if (low == entry.child_count)
        return MM_CHUNK_NOT_FOUND;
    result = mm_chunk_child_at(access, parent, low, &value);
    if (result != MM_CHUNK_OK)
        return result;
    if (value.slot != slot || value.type < type)
        return MM_CHUNK_NOT_FOUND;
    *child = value;
    return MM_CHUNK_OK;
}

int mm_chunk_child_exact(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type, MMChunkChild *child)
{
    MMChunkChild value;
    int status;
    if (!child)
        return MM_CHUNK_BAD_FILE;
    status = mm_chunk_child_type(access, parent, slot, type, &value);
    if (status != MM_CHUNK_OK)
        return status;
    if (value.type != type)
        return MM_CHUNK_NOT_FOUND;
    *child = value;
    return MM_CHUNK_OK;
}

int mm_chunk_link_number(const MMChunkAccess *access, MMU32 parent_type, MMU32 parent_number, MMU32 slot,
                         MMU32 child_type, MMU32 *number)
{
    MMChunkChild child;
    MMU32 parent;
    int status;
    if (!number)
        return MM_CHUNK_BAD_FILE;
    status = mm_chunk_find(access, parent_type, parent_number, &parent);
    if (status == MM_CHUNK_OK)
        status = mm_chunk_child_exact(access, parent, slot, child_type, &child);
    if (status == MM_CHUNK_OK)
        *number = child.number;
    return status;
}

static int wire_row(const MMChunkIndex *index, MMU32 position, const unsigned char **row,
                    MMU32 *variable_bytes)
{
    const unsigned char *location;
    MMU32 offset, count;
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
static int wire_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    const MMChunkIndex *index = (const MMChunkIndex *)context;
    const unsigned char *row;
    MMU32 variable, flags, bytes, children, parents;
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
            flags = (row[29] ? MM_CHUNK_LONER : 0) | (row[30] ? MM_CHUNK_PACKED : 0);
        else
            flags = word32(row + 28, index->big) & ~(MMU32)1;
        if (!parents)
            flags |= MM_CHUNK_LONER;
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
        if (!parents && !(flags & MM_CHUNK_LONER))
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
static int wire_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    const MMChunkIndex *index = (const MMChunkIndex *)context;
    const unsigned char *row;
    MMU32 variable;
    MMChunkEntry entry;
    if (!wire_entry(context, parent, &entry) || position >= entry.child_count ||
        !wire_row(index, parent, &row, &variable))
        return 0;
    row += index->stride + position * 12;
    child->type = word32(row, index->big);
    child->number = word32(row + 4, index->big);
    child->slot = word32(row + 8, index->big);
    return 1;
}

typedef struct WireRange
{
    MMU32 offset, size;
} WireRange;

static int compare_wire_offsets(const void *a, const void *b)
{
    MMU32 left = ((const WireRange *)a)->offset, right = ((const WireRange *)b)->offset;
    return left < right ? -1 : left != right;
}

static int validate_unpadded_rows(const MMChunkIndex *index)
{
    WireRange *ranges;
    const unsigned char *location = index->wire + 20 + index->data_bytes;
    MMU32 i, limit, end;
    int result = MM_CHUNK_BAD_FILE;
    if (!index->count || (size_t)index->count > (size_t)-1 / sizeof(*ranges))
        return result;
    ranges = (WireRange *)malloc((size_t)index->count * sizeof(*ranges));
    if (!ranges)
        return MM_CHUNK_MEMORY;
    for (i = 0; i < index->count; ++i, location += 8)
    {
        ranges[i].offset = word32(location, index->big);
        ranges[i].size = word32(location + 4, index->big);
    }
    /* The lookup table is ordered by chunk key. Check occupied extents in
     * physical order so legacy rows can omit padding independently. */
    qsort(ranges, (size_t)index->count, sizeof(*ranges), compare_wire_offsets);
    if (ranges[0].offset == 0)
    {
        for (i = 0; i < index->count; ++i)
        {
            end = ranges[i].offset + ranges[i].size;
            limit = i + 1 == index->count ? index->data_bytes : ranges[i + 1].offset;
            if (limit < end || limit - end > ((4U - ranges[i].size) & 3U))
                break;
        }
        if (i == index->count)
            result = MM_CHUNK_OK;
    }
    free(ranges);
    return result;
}

int mm_chunk_index_open(const MMReadStream *stream, const MMChunkPolicy *policy,
                              MMChunkIndex **output)
{
    unsigned char group[20];
    MMChunkHeader header;
    MMChunkIndex *index;
    MMChunkEntry entry;
    MMU32 i, bytes, occupied = 0;
    int status, padded = 1;
    if (!policy || !output)
        return MM_CHUNK_BAD_FILE;
    status = mm_chunk_read_header(stream, &header);
    if (status != MM_CHUNK_OK)
        return status;
    if (header.index_bytes < 20)
        return MM_CHUNK_BAD_FILE;
    if (!mm_read_exact(stream, header.index_offset, group, sizeof(group)))
        return MM_CHUNK_IO;
    /* Validate the two legacy GG storage sizes before allocating the wire copy. */
    {
        int big = group[0] == 0 && group[1] == 1;
        MMU32 data = word32(group + 8, big), available = header.index_bytes - 20;
        if ((!big && !(group[0] == 1 && group[1] == 0)) || data > available || data >= CHUNK_GROUP_LIMIT ||
            available - data >= CHUNK_GROUP_LIMIT || (available - data) % 8 ||
            word32(group + 4, big) != (available - data) / 8 || word32(group + 12, big) != 0xffffffffUL)
            return MM_CHUNK_BAD_FILE;
    }
    if ((size_t)header.index_bytes != header.index_bytes)
        return MM_CHUNK_MEMORY;
    index = (MMChunkIndex *)malloc(sizeof(*index));
    if (!index)
        return MM_CHUNK_MEMORY;
    index->wire = (unsigned char *)malloc((size_t)header.index_bytes);
    if (!index->wire)
    {
        free(index);
        return MM_CHUNK_MEMORY;
    }
    index->references = 1;
    index->header = header;
    index->policy = *policy;
    memcpy(index->wire, group, sizeof(group));
    bytes = word32(group + 8, group[0] == 0);
    if (!mm_read_exact(stream, header.index_offset + 20, index->wire + 20, (size_t)bytes) ||
        !mm_read_exact(stream, header.index_offset + 20 + bytes, index->wire + 20 + bytes,
                             (size_t)(header.index_bytes - 20 - bytes)))
    {
        mm_chunk_index_close(index);
        return MM_CHUNK_IO;
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
        MMU32 variable, rounded, offset;
        if (!wire_entry(index, i, &entry) || !wire_row(index, i, &row, &variable))
            goto bad;
        offset = (MMU32)(row - index->wire - 20);
        rounded = (index->stride + variable + 3U) & ~(MMU32)3;
        if (rounded > index->data_bytes - offset || rounded > index->data_bytes - occupied)
            padded = 0;
        else
            occupied += rounded;
    }
    if (!padded || occupied != index->data_bytes)
    {
        status = validate_unpadded_rows(index);
        if (status != MM_CHUNK_OK)
        {
            mm_chunk_index_close(index);
            return status;
        }
    }
    *output = index;
    return MM_CHUNK_OK;
bad:
    mm_chunk_index_close(index);
    return MM_CHUNK_BAD_FILE;
}
MMChunkIndex *mm_chunk_index_retain(const MMChunkIndex *index)
{
    MMChunkIndex *owner = (MMChunkIndex *)index;
    if (!owner || owner->references == 0xffffffffUL)
        return NULL;
    ++owner->references;
    return owner;
}
void mm_chunk_index_close(MMChunkIndex *index)
{
    if (index && !--index->references)
    {
        free(index->wire);
        free(index);
    }
}
MMChunkAccess mm_chunk_index_access(const MMChunkIndex *index)
{
    MMChunkAccess access;
    access.context = (void *)index;
    access.count = index->count;
    access.entry = wire_entry;
    access.child = wire_child;
    return access;
}
const MMChunkHeader *mm_chunk_index_header(const MMChunkIndex *index)
{
    return &index->header;
}
int mm_chunk_index_name(const MMChunkIndex *index, MMU32 position, const unsigned char **bytes,
                              MMU32 *count)
{
    const unsigned char *row;
    MMU32 variable;
    MMChunkEntry entry;
    if (!index || !bytes || !count || !wire_entry((void *)index, position, &entry) ||
        !wire_row(index, position, &row, &variable))
        return MM_CHUNK_BAD_FILE;
    *bytes = row + index->stride + entry.child_count * 12;
    *count = variable - entry.child_count * 12;
    return MM_CHUNK_OK;
}

typedef struct RekeyRow
{
    MMU32 type, number, old_position;
} RekeyRow;

static int compare_rekey_rows(const void *first, const void *second)
{
    const RekeyRow *a = (const RekeyRow *)first, *b = (const RekeyRow *)second;
    if (a->type != b->type)
        return a->type < b->type ? -1 : 1;
    return a->number == b->number ? 0 : a->number < b->number ? -1 : 1;
}

int mm_chunk_child_compare(const MMChunkChild *a, const MMChunkChild *b)
{
    if (a->slot != b->slot)
        return a->slot < b->slot ? -1 : 1;
    if (a->type != b->type)
        return a->type < b->type ? -1 : 1;
    return a->number == b->number ? 0 : a->number < b->number ? -1 : 1;
}

static int compare_rekey_children(const void *first, const void *second)
{
    return mm_chunk_child_compare(first, second);
}

int mm_chunk_index_rekey(const MMChunkIndex *index, const MMU32 *numbers, MMChunkIndexWrite write, void *context)
{
    MMChunkAccess access;
    MMChunkEntry entry;
    MMChunkChild *children = NULL;
    RekeyRow *rows = NULL;
    const unsigned char *original;
    unsigned char *wire = NULL, *row, *location;
    MMU32 i, j, variable, fixed_and_variable, rounded, offset = 0, target, largest_child_count = 0;
    int status = MM_CHUNK_BAD_FILE;
    if (!index || !write || (!numbers && index->count))
        return status;
    access = mm_chunk_index_access(index);
    if (!mm_chunk_keys_ordered(&access))
        return status;
    if (!access.count)
        return write(context, index->header.index_offset, index->wire, index->header.index_bytes)
                   ? MM_CHUNK_OK : MM_CHUNK_IO;
    status = MM_CHUNK_MEMORY;
    if ((size_t)access.count > (size_t)-1 / sizeof(*rows))
        return status;
    rows = (RekeyRow *)malloc((size_t)access.count * sizeof(*rows));
    if (!rows)
        return status;
    for (i = 0; i < access.count; ++i)
    {
        wire_entry(access.context, i, &entry);
        rows[i].type = entry.type;
        rows[i].number = numbers[i];
        rows[i].old_position = i;
        if (entry.child_count > largest_child_count)
            largest_child_count = entry.child_count;
    }
    qsort(rows, access.count, sizeof(*rows), compare_rekey_rows);
    if (largest_child_count)
    {
        if ((size_t)largest_child_count > (size_t)-1 / sizeof(*children))
            goto done;
        children = (MMChunkChild *)malloc((size_t)largest_child_count * sizeof(*children));
        if (!children)
            goto done;
    }
    wire = (unsigned char *)malloc((size_t)index->header.index_bytes);
    if (!wire)
        goto done;
    memcpy(wire, index->wire, 20);
    location = wire + 20 + index->data_bytes;
    status = MM_CHUNK_BAD_FILE;
    for (i = 0; i < access.count; ++i)
    {
        MMU32 old = rows[i].old_position;
        if ((i && !compare_rekey_rows(rows + i - 1, rows + i)) ||
            !wire_row(index, old, &original, &variable) || !wire_entry(access.context, old, &entry))
            goto done;
        fixed_and_variable = index->stride + variable;
        rounded = (fixed_and_variable + 3U) & ~(MMU32)3;
        row = wire + 20 + offset;
        memcpy(row, original, rounded);
        write32(row + 4, rows[i].number, index->big);
        for (j = 0; j < entry.child_count; ++j)
        {
            if (!wire_child(access.context, old, j, &children[j]) ||
                mm_chunk_find(&access, children[j].type, children[j].number, &target) != MM_CHUNK_OK)
                goto done;
            children[j].number = numbers[target];
        }
        if (entry.child_count > 1)
            qsort(children, entry.child_count, sizeof(*children), compare_rekey_children);
        for (j = 0; j < entry.child_count; ++j)
        {
            unsigned char *child = row + index->stride + j * 12;
            if (j && !mm_chunk_child_compare(children + j - 1, children + j))
                goto done;
            write32(child, children[j].type, index->big);
            write32(child + 4, children[j].number, index->big);
            write32(child + 8, children[j].slot, index->big);
        }
        write32(location + i * 8, offset, index->big);
        write32(location + i * 8 + 4, fixed_and_variable, index->big);
        offset += rounded;
    }
    status = write(context, index->header.index_offset, wire, index->header.index_bytes) ? MM_CHUNK_OK : MM_CHUNK_IO;
done:
    free(wire);
    free(children);
    free(rows);
    return status;
}

typedef struct GraphFrame
{
    MMU32 node, next, children;
} GraphFrame;
int mm_chunk_validate_graph(const MMChunkAccess *access, MMU32 max_references)
{
    unsigned char *marks;
    GraphFrame *stack = NULL, *grown;
    MMU32 capacity = 0, depth, i, target, refs = 0, children = 0;
    MMChunkEntry entry;
    MMChunkChild child;
    int result = MM_CHUNK_BAD_FILE;
    if (!access)
        return result;
    for (i = 0; i < access->count; ++i)
    {
        if (mm_chunk_at(access, i, &entry) != MM_CHUNK_OK ||
            entry.child_count > entry.variable_bytes / 12 || entry.parent_count > max_references)
            return result;
        refs += entry.parent_count;
        children += entry.child_count;
    }
    if (refs != children)
        return result;
    if (!access->count)
        return MM_CHUNK_OK;
    if ((size_t)access->count != access->count)
        return MM_CHUNK_MEMORY;
    marks = (unsigned char *)calloc((size_t)access->count, 1);
    if (!marks)
        return MM_CHUNK_MEMORY;
    for (i = access->count; i-- > 0;)
    {
        if (mm_chunk_at(access, i, &entry) != MM_CHUNK_OK)
            goto done;
        if (entry.parent_count || !entry.child_count || (marks[i] & 2))
            continue;
        depth = 0;
        target = i;
        for (;;)
        {
            if (depth == capacity)
            {
                MMU32 next = capacity ? capacity * 2 : 32;
                if (next < capacity || next > access->count)
                    next = access->count;
                if ((size_t)next != next || (size_t)next > (size_t)-1 / sizeof(*stack))
                {
                    result = MM_CHUNK_MEMORY;
                    goto done;
                }
                grown = (GraphFrame *)realloc(stack, (size_t)next * sizeof(*stack));
                if (!grown)
                {
                    result = MM_CHUNK_MEMORY;
                    goto done;
                }
                stack = grown;
                capacity = next;
            }
            if (mm_chunk_at(access, target, &entry) != MM_CHUNK_OK)
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
                if (mm_chunk_child_at(access, frame->node, frame->next++, &child) != MM_CHUNK_OK ||
                    mm_chunk_find(access, child.type, child.number, &target) != MM_CHUNK_OK)
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
        if (mm_chunk_at(access, i, &entry) != MM_CHUNK_OK ||
            ((marks[i] & 4) != 0) != (entry.parent_count != 0))
            goto done;
    }
    result = MM_CHUNK_OK;
done:
    free(stack);
    free(marks);
    return result;
}
