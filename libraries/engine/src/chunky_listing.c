#include "mm/chunky_listing.h"
#include "mm/collections.h"
#include "mm/string_value.h"
#include <stdio.h>
#include <string.h>

enum { row_bytes = 8, row_text_limit = 5 * row_bytes + 7 + row_bytes + 2 };

static size_t format_row(unsigned char *text, const unsigned char *bytes, size_t count)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t i;
    unsigned char value;

    memset(text, ' ', 5 * row_bytes);
    for (i = 0; i < count; ++i)
    {
        value = bytes[i];
        text[5 * i] = '0';
        text[5 * i + 1] = 'x';
        text[5 * i + 2] = digits[value >> 4];
        text[5 * i + 3] = digits[value & 15];
    }
    text += 5 * row_bytes;
    memcpy(text, "   // '", 7);
    text += 7;
    for (i = 0; i < count; ++i)
    {
        value = bytes[i];
        text[i] = value < 0x20 || value == 0x7F ? '?' : value;
    }
    memcpy(text + count, "' ", 2);
    return row_text_limit - row_bytes + count;
}

static void emit_line(const unsigned char *line, size_t length, MMChunkyWriteLine write_line, void *context)
{
    write_line(context, line, length > MM_CHUNKY_LISTING_LINE_LIMIT ? MM_CHUNKY_LISTING_LINE_LIMIT : length, 1);
}

int mm_chunky_list_bytes(const MMReadStream *stream, size_t indent, MMChunkyWriteLine write_line, void *context)
{
    unsigned char line[MM_CHUNKY_LISTING_LINE_LIMIT + row_text_limit];
    unsigned char buffer[4096];
    MMU32 offset, remaining;
    size_t count, row, row_count, length;

    if (!stream || !write_line || indent > MM_CHUNKY_LISTING_LINE_LIMIT)
        return 0;
    memset(line, '\t', indent);
    memcpy(line + indent, "BYTE", 4);
    emit_line(line, indent + 4, write_line, context);
    for (offset = 0; offset < stream->length; offset += (MMU32)count)
    {
        remaining = stream->length - offset;
        count = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        if (!mm_read_exact(stream, offset, buffer, count))
            return 0;
        for (row = 0; row < count; row += row_count)
        {
            row_count = count - row < row_bytes ? count - row : row_bytes;
            length = format_row(line + indent, buffer + row, row_count);
            emit_line(line, indent + length, write_line, context);
        }
    }
    return 1;
}

void mm_chunky_list_pack(MMU32 format, MMChunkyWriteLine write_line, void *context)
{
    char text[sizeof("PACKFMT (0xFFFFFFFF) PACK")];

    if (!format)
    {
        write_line(context, "PACK", 4, 1);
        return;
    }
    sprintf(text, "PACKFMT (0x%lX) PACK", (unsigned long)format);
    write_line(context, text, strlen(text), 1);
}

MMChunkyListKind mm_chunky_list_kind(MMU32 tag)
{
    switch (tag >> 8)
    {
    case 0x475354UL: return MM_CHUNKY_LIST_GST;
    case 0x415354UL: return MM_CHUNKY_LIST_AST;
    }
    switch (tag >> 16)
    {
    case 0x474cUL: return MM_CHUNKY_LIST_GL;
    case 0x414cUL: return MM_CHUNKY_LIST_AL;
    case 0x4747UL: return MM_CHUNKY_LIST_GG;
    case 0x4147UL: return MM_CHUNKY_LIST_AG;
    }
    return MM_CHUNKY_LIST_NONE;
}

static void directive(const MMChunkyCollectionListing *listing, const char *text)
{
    listing->write_line(listing->context, text, strlen(text), 1);
}

static void collection_modes(const MMChunkyCollectionListing *listing, MMChunkyCollectionMode *mode,
                             int foreign_order, int system)
{
    if (foreign_order != mode->foreign_order)
    {
        directive(listing, foreign_order != listing->windows_is_native ? "WINBO" : "MACBO");
        mode->foreign_order = foreign_order;
    }
    if (system != mode->system)
    {
        directive(listing, system == listing->windows_system ? "WINOSK" : "MACOSK");
        mode->system = system;
    }
}

static void list_memory(const MMChunkyCollectionListing *listing, const void *bytes, MMU32 length)
{
    MMMemoryStream memory;

    mm_memory_stream_init(&memory, bytes, length);
    mm_chunky_list_bytes(&memory.stream, 2, listing->write_line, listing->context);
}

static void list_array(const MMChunkyCollectionListing *listing, const MMCollectionHeader *header,
                       const MMCollectionStorage *storage)
{
    MMU32 index;
    const unsigned char *data = storage->buffers[0].data;
    const unsigned char *bitmap = storage->buffers[1].data;

    for (index = 0; index < header->count; ++index)
    {
        if (bitmap && !(bitmap[index / 8] & (1U << (index % 8))))
            directive(listing, "\tFREE");
        else
        {
            directive(listing, "\tITEM");
            list_memory(listing, data + index * header->width, header->width);
        }
    }
}

static void list_group(const MMChunkyCollectionListing *listing, const MMGroup *group)
{
    MMU32 index, variable;
    MMGroupLocation slot;
    const unsigned char *row;

    for (index = 0; index < group->count; ++index)
    {
        if (mm_group_read_slot(group, index, &slot) != MM_GROUP_SLOT_PRESENT)
        {
            directive(listing, "\tFREE");
            continue;
        }
        directive(listing, "\tITEM");
        if (!slot.bytes)
            continue;
        row = group->storage->buffers[0].data + slot.offset;
        if (group->fixed_bytes)
            list_memory(listing, row, group->fixed_bytes);
        variable = (MMU32)slot.bytes - group->fixed_bytes;
        if (variable)
        {
            directive(listing, "\tVAR");
            list_memory(listing, row + group->fixed_bytes, variable);
        }
    }
}

static void list_string(const MMChunkyCollectionListing *listing, const void *text, MMU32 length)
{
    static const unsigned char prefix[] = {'\t', '\t', '"'};
    static const unsigned short wide_prefix[] = {'\t', '\t', '"'};
    unsigned char narrow[MM_CHUNKY_LISTING_LINE_LIMIT + 2];
    unsigned short wide[MM_CHUNKY_LISTING_LINE_LIMIT + 2];
    MMStringValue value;
    const void *line;
    unsigned unit = listing->native_unit;

    value.buffer = unit == 1 ? (void *)narrow : (void *)wide;
    value.unit = unit;
    value.context = NULL;
    value.check_copy = NULL;
    mm_string_value_set(&value, text, (MMI32)length);
    mm_string_value_expand(&value);

    /* String::FFormatSz leaves just the prefix if the whole %s field
     * would fill the remaining capacity, including the closing quote. */
    length = unit == 1 ? narrow[0] : wide[0];
    if (length >= MM_CHUNKY_LISTING_LINE_LIMIT - sizeof(prefix))
    {
        directive(listing, "\t\t\"");
        return;
    }
    if (unit == 1)
    {
        memmove(narrow + 3, narrow + 1, length);
        memcpy(narrow, prefix, sizeof(prefix));
        narrow[length + 3] = '"';
        line = narrow;
    }
    else
    {
        memmove(wide + 3, wide + 1, length * sizeof(*wide));
        memcpy(wide, wide_prefix, sizeof(wide_prefix));
        wide[length + 3] = '"';
        line = wide;
    }
    listing->write_line(listing->context, line, length + 4, unit);
}

static int string_is_free(const MMStringTable *strings, MMU32 index)
{
    MMU32 offset;
    memcpy(&offset, strings->entries + index * strings->stride, sizeof(offset));
    return offset == 0xffffffffUL;
}

static void list_strings(const MMChunkyCollectionListing *listing, const MMStringTable *strings)
{
    MMU32 index, length, extra = strings->stride - 4;
    const void *text;

    for (index = 0; index < strings->count; ++index)
    {
        if (!mm_strings_get(strings, index, &text, &length))
        {
            directive(listing, "\tFREE");
            continue;
        }
        directive(listing, "\tITEM");
        list_string(listing, text, length);
        if (extra)
            list_memory(listing, strings->entries + index * strings->stride + 4, extra);
    }
}

static int prepare_group(MMGroup *group, MMCollectionStorage *storage, const MMCollectionHeader *header,
                         int foreign_order)
{
    MMU32 index;
    MMGroupLocation slot;

    group->storage = storage;
    group->fixed_bytes = header->width;
    group->count = header->count;
    group->bytes = header->bytes;
    group->free_count = header->free_count;
    group->trash_unused = 0;
    if (foreign_order)
        mm_group_swap_locations(group);
    for (index = 0; index < group->count; ++index)
        if (mm_group_read_slot(group, index, &slot) == MM_GROUP_SLOT_INVALID)
            return 0;
    return 1;
}

static int prepare_strings(MMStringTable *strings, MMCollectionStorage *storage, const MMCollectionHeader *header,
                           const MMChunkyCollectionListing *listing, int foreign_order)
{
    MMU32 index, length;
    const void *text;

    strings->strings = storage->buffers[0].data;
    strings->entries = storage->buffers[1].data;
    strings->bytes = header->bytes;
    strings->count = header->count;
    strings->stride = header->width;
    strings->string_capacity = storage->buffers[0].capacity;
    strings->entry_capacity = storage->buffers[1].capacity;
    strings->free_count = header->free_count;
    strings->unit_bytes = listing->native_unit;
    strings->trash_unused = 0;
    strings->context = storage;
    strings->reserve = mm_strings_reserve_storage;
    if (foreign_order)
        mm_strings_swap_offsets(strings);
    if (header->system != listing->native_system &&
        !mm_strings_translate(strings, header->system, listing->native_system, listing->convert_string, listing->context))
        return 0;
    for (index = 0; index < strings->count; ++index)
        if (!string_is_free(strings, index) && !mm_strings_get(strings, index, &text, &length))
            return 0;
    return 1;
}

int mm_chunky_list_collection(const MMReadStream *stream, MMChunkyListKind kind, int packed, MMU32 format,
                              const MMChunkyCollectionListing *listing, MMChunkyCollectionMode *mode)
{
    static const struct { int kind, allocated; const char *name; } layouts[] = {
        {MM_COLLECTION_ARRAY, 0, "GL"}, {MM_COLLECTION_ALLOCATED_ARRAY, 0, "AL"},
        {MM_COLLECTION_GROUP, 0, "GG"}, {MM_COLLECTION_GROUP, 1, "AG"},
        {MM_COLLECTION_STRINGS, 0, "GST"}, {MM_COLLECTION_STRINGS, 1, "AST"}
    };
    const unsigned short one = 1;
    MMCollectionHeader header;
    MMCollectionStorage storage;
    MMGroup group;
    MMStringTable strings;
    char text[sizeof("\tGST(2147483647)")];
    int result, index, foreign_order, system;
    MMU32 width;

    if (kind < MM_CHUNKY_LIST_GL || kind > MM_CHUNKY_LIST_AST || !mode || !listing || !listing->write_line ||
        (listing->native_unit != 1 && listing->native_unit != 2))
        return MM_STORAGE_ARGUMENT;
    index = kind - MM_CHUNKY_LIST_GL;
    result = mm_collection_header_read(stream, layouts[index].kind, layouts[index].allocated, &header);
    if (result != MM_STORAGE_OK)
        return result;
    mm_storage_init(&storage, NULL);
    result = mm_storage_read(&storage, stream, header.header_bytes, header.first_bytes, header.second_bytes);
    if (result != MM_STORAGE_OK)
        goto done;

    foreign_order = header.big_endian != (*(const unsigned char *)&one == 0);
    if ((header.kind == MM_COLLECTION_GROUP && !prepare_group(&group, &storage, &header, foreign_order)) ||
        (header.kind == MM_COLLECTION_STRINGS && !prepare_strings(&strings, &storage, &header, listing, foreign_order)))
    {
        result = MM_STORAGE_ARGUMENT;
        goto done;
    }

    if (packed)
        mm_chunky_list_pack(format, listing->write_line, listing->context);
    /* Directives track the raw OSK word, even when the payload's byte order
     * differs. String decoding uses the normalized value from the header. */
    system = foreign_order ? ((header.system & 255) << 8) | (header.system >> 8) : header.system;
    collection_modes(listing, mode, foreign_order, system);
    width = header.kind == MM_COLLECTION_STRINGS ? header.width - 4 : header.width;
    sprintf(text, "\t%s(%lu)", layouts[index].name, (unsigned long)width);
    directive(listing, text);
    if (header.kind == MM_COLLECTION_STRINGS)
        list_strings(listing, &strings);
    else if (header.kind == MM_COLLECTION_GROUP)
        list_group(listing, &group);
    else
        list_array(listing, &header, &storage);

done:
    mm_storage_dispose(&storage);
    return result;
}
