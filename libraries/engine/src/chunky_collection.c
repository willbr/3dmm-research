/* Chunky collection compilation converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_collection.h"
#include "mm/collections.h"
#include <string.h>

typedef struct CollectionSyntax
{
    int token, kind, allocated, header_error, size_error;
} CollectionSyntax;

static const CollectionSyntax syntaxes[] = {
    {ttGl, MM_COLLECTION_ARRAY, 0, ertListHead, ertListEntrySize},
    {ttAl, MM_COLLECTION_ALLOCATED_ARRAY, 1, ertListHead, ertListEntrySize},
    {ttGg, MM_COLLECTION_GROUP, 0, ertGroupHead, ertGroupEntrySize},
    {ttAg, MM_COLLECTION_GROUP, 1, ertGroupHead, ertGroupEntrySize},
    {ttGst, MM_COLLECTION_STRINGS, 0, ertGstHead, ertGstEntrySize},
    {ttAst, MM_COLLECTION_STRINGS, 1, ertGstHead, ertGstEntrySize}
};

typedef struct Collection
{
    const CollectionSyntax *syntax;
    MMU32 width;
    MMCollectionStorage storage, holes_storage;
    MMArray holes;
    union
    {
        MMArray array;
        MMGroup group;
        MMStringTable strings;
    } rows;
    MMCollectionHeader wire;
} Collection;

typedef struct Parser
{
    const MMChunkySource *source;
    const MMChunkyCollectionHost *host;
    Collection collection;
} Parser;

static void report(const Parser *parser, int error)
{
    parser->source->report(parser->source->context, error);
}

static int failed(const Parser *parser)
{
    return parser->host->has_error(parser->host->context);
}

static int next(const Parser *parser, MMChunkySourceToken *token)
{
    return parser->source->next(parser->source->context, token, MM_CHUNKY_READ_CLEAN);
}

static void skip_end(const Parser *parser)
{
    MMChunkySourceToken token;
    while (next(parser, &token) && token.kind != ttEndChunk)
        ;
}

static int valid_width(const CollectionSyntax *syntax, MMI32 width)
{
    if (width < 0 || width >= MM_STORAGE_LIMIT)
        return 0;
    if (syntax->kind == MM_COLLECTION_STRINGS)
        return width % 4 == 0;
    return syntax->kind == MM_COLLECTION_GROUP || width > 0;
}

static MMU32 growth(MMU32 width, MMU32 rows, MMU32 extra)
{
    if (width > (MM_STORAGE_LIMIT - 1 - extra) / rows)
        return 0;
    return (rows * width + extra + 3) & ~(MMU32)3;
}

static void init_array(MMArray *array, MMCollectionStorage *storage, MMU32 width, int trash_unused)
{
    array->storage = storage;
    array->width = width;
    array->free_count = -1;
    array->trash_unused = trash_unused;
    storage->min_grow[0] = growth(width, 20, 0);
}

static int init_collection(Parser *parser, const CollectionSyntax *syntax, MMU32 width)
{
    Collection *c = &parser->collection;
    const MMChunkyCollectionHost *host = parser->host;
    MMCollectionStorage *storage = &c->storage;
    MMGroup *group;
    MMStringTable *strings;
    int trash_unused = host->trash_unused;
    MMI32 free_count = syntax->allocated ? 0 : -1;

    memset(c, 0, sizeof(*c));
    if (mm_storage_init(storage, host->allocator) != MM_STORAGE_OK ||
        mm_storage_init(&c->holes_storage, host->allocator) != MM_STORAGE_OK)
        return 0;
    c->syntax = syntax;
    c->width = width;
    init_array(&c->holes, &c->holes_storage, 4, trash_unused);
    switch (syntax->kind)
    {
    case MM_COLLECTION_GROUP:
        group = &c->rows.group;
        group->storage = storage;
        group->fixed_bytes = width;
        group->free_count = free_count;
        group->trash_unused = trash_unused;
        storage->min_grow[0] = growth(width, 10, 130);
        storage->min_grow[1] = 80;
        break;
    case MM_COLLECTION_STRINGS:
        strings = &c->rows.strings;
        strings->stride = width + 4;
        strings->unit_bytes = parser->source->unit_bytes;
        strings->free_count = free_count;
        strings->trash_unused = trash_unused;
        strings->context = storage;
        strings->reserve = mm_strings_reserve_storage;
        storage->min_grow[0] = growth(strings->unit_bytes, 100, 10);
        storage->min_grow[1] = growth(strings->stride, 10, 0);
        break;
    default:
        /* Authored rows are appended before the bitmap exposes any holes. */
        init_array(&c->rows.array, storage, width, trash_unused);
        break;
    }
    return 1;
}

static void record_hole(Parser *parser)
{
    Collection *c = &parser->collection;
    MMU32 index;
    if (!c->syntax->allocated)
    {
        report(parser, ertBadFree);
        return;
    }
    if (failed(parser))
        return;
    switch (c->syntax->kind)
    {
    case MM_COLLECTION_GROUP: index = c->rows.group.count; break;
    case MM_COLLECTION_STRINGS: index = c->rows.strings.count; break;
    default: index = c->rows.array.count; break;
    }
    if (mm_array_add(&c->holes, &index, NULL) != MM_STORAGE_OK)
        report(parser, ertOom);
}

static void copy_payload(const Parser *parser, unsigned char *output, MMU32 used, MMU32 capacity)
{
    if (used)
        parser->host->read_payload(parser->host->context, output, used);
    if (used < capacity)
        memset(output + used, 0, capacity - used);
}

static void add_array_row(Parser *parser, MMU32 used)
{
    Collection *c = &parser->collection;
    MMU32 index;
    if (mm_array_add(&c->rows.array, NULL, &index) != MM_STORAGE_OK)
        report(parser, ertOom);
    else
        copy_payload(parser, c->storage.buffers[0].data + index * c->width, used, c->width);
}

static void add_group_row(Parser *parser, MMU32 used, MMU32 *index)
{
    Collection *c = &parser->collection;
    MMGroupLocation loc;
    if (mm_group_add(&c->rows.group, 0, NULL, NULL, index) != MM_STORAGE_OK)
        report(parser, ertOom);
    else if (c->width)
    {
        if (mm_group_location(&c->rows.group, *index, &loc) != MM_STORAGE_OK)
            report(parser, ertOom);
        else
            copy_payload(parser, c->storage.buffers[0].data + loc.offset, used, c->width);
    }
}

static int numeric_row(Parser *parser, MMChunkySourceToken *token, int hole)
{
    const MMChunkySource *source = parser->source;
    Collection *c = &parser->collection;
    MMU32 used, index = 0;
    MMGroupLocation loc;
    int group = c->syntax->kind == MM_COLLECTION_GROUP;

    if (!next(parser, token))
        return 0;
    if (!hole)
        mm_chunky_parse_data(source, token);
    used = source->size(source->context);
    if (used > c->width)
        report(parser, ertItemOverflow);
    if (!failed(parser))
    {
        if (group)
            add_group_row(parser, used, &index);
        else
            add_array_row(parser, used);
    }
    if (!group || hole || token->kind != ttVar)
        return 1;
    if (!next(parser, token))
        return 0;

    parser->host->clear_payload(parser->host->context);
    mm_chunky_parse_data(source, token);
    if (failed(parser) || !(used = source->size(source->context)))
        return 1;
    if (mm_group_insert_bytes(&c->rows.group, index, 0, used, NULL) != MM_STORAGE_OK ||
        mm_group_location(&c->rows.group, index, &loc) != MM_STORAGE_OK)
        report(parser, ertOom);
    else
        copy_payload(parser, c->storage.buffers[0].data + loc.offset + c->width, used, used);
    return 1;
}

static int string_row(Parser *parser, MMChunkySourceToken *token, int hole)
{
    const MMChunkySource *source = parser->source;
    Collection *c = &parser->collection;
    MMStringTable *strings = &c->rows.strings;
    MMLexText label;
    MMU32 used, index = 0;

    label.length = 0;
    if (!next(parser, token))
        return 0;
    if (!hole)
    {
        if (!mm_chunky_copy_text(&label, token, strings->unit_bytes))
        {
            report(parser, ertNeedString);
            skip_end(parser);
            return 0;
        }
        if (!next(parser, token))
            return 0;
    }

    if (!failed(parser))
    {
        if (!mm_strings_add(strings, &label.units, label.length, NULL, &index))
            report(parser, ertOom);
        else if (hole && c->width)
            memset(strings->entries + index * strings->stride + 4, 0, c->width);
    }
    if (!c->width || hole)
        return 1;

    parser->host->clear_payload(parser->host->context);
    mm_chunky_parse_data(source, token);
    used = source->size(source->context);
    if (used > c->width)
        report(parser, ertItemOverflow);
    if (!failed(parser))
        copy_payload(parser, strings->entries + index * strings->stride + 4, used, c->width);
    return 1;
}

static int allocate_bitmap(Collection *c)
{
    MMArray *array = &c->rows.array;
    MMU32 bytes = (array->count + 7) / 8;
    unsigned used = array->count % 8;
    unsigned char *bits;
    if (mm_storage_ensure(&c->storage, array->count * array->width, bytes, 0) != MM_STORAGE_OK)
        return 0;
    bits = c->storage.buffers[1].data;
    if (bytes)
        memset(bits, 255, bytes);
    if (used)
        bits[bytes - 1] = (unsigned char)((1U << used) - 1);
    array->allocated = 1;
    array->free_count = 0;
    return 1;
}

static int remove_holes(Collection *c)
{
    MMU32 i, index;
    for (i = c->holes.count; i > 0;)
    {
        --i;
        memcpy(&index, c->holes_storage.buffers[0].data + i * 4, 4);
        switch (c->syntax->kind)
        {
        case MM_COLLECTION_GROUP:
            if (mm_group_delete(&c->rows.group, index) != MM_STORAGE_OK)
                return 0;
            break;
        case MM_COLLECTION_STRINGS:
            if (!mm_strings_delete(&c->rows.strings, index))
                return 0;
            break;
        default:
            if (mm_array_delete(&c->rows.array, index, 1) != MM_STORAGE_OK)
                return 0;
            break;
        }
    }
    return 1;
}

static int initialize_group_padding(MMGroup *group)
{
    MMU32 i, end, padding;
    MMGroupLocation loc;
    int status;
    for (i = 0; i < group->count; ++i)
    {
        status = mm_group_read_slot(group, i, &loc);
        if (status == MM_GROUP_SLOT_INVALID)
            return 0;
        if (status == MM_GROUP_SLOT_FREE)
            continue;
        end = (MMU32)loc.offset + (MMU32)loc.bytes;
        padding = (0U - (MMU32)loc.bytes) & 3U;
        if (padding > group->bytes - end)
            return 0;
        if (padding)
            memset(group->storage->buffers[0].data + end, group->trash_unused ? 0xa3 : 0, padding);
    }
    return 1;
}

static int prepare_wire(Parser *parser)
{
    const MMChunkySource *source = parser->source;
    Collection *c = &parser->collection;
    MMCollectionHeader *wire = &c->wire;
    const unsigned short one = 1;
    int swapped = source->format->byte_order != source->native_order;
    MMGroup *group;
    MMStringTable *strings;

    if (c->syntax->kind == MM_COLLECTION_ALLOCATED_ARRAY && !allocate_bitmap(c))
        return 0;
    if (!remove_holes(c))
        return 0;
    wire->kind = c->syntax->kind;
    wire->big_endian = (*(const unsigned char *)&one == 0) != swapped;
    wire->system = source->format->os_kind;
    wire->width = c->width;
    switch (wire->kind)
    {
    case MM_COLLECTION_GROUP:
        group = &c->rows.group;
        if (!initialize_group_padding(group))
            return 0;
        wire->count = group->count;
        wire->bytes = group->bytes;
        wire->free_count = group->free_count;
        if (swapped)
            mm_group_swap_locations(group);
        break;
    case MM_COLLECTION_STRINGS:
        strings = &c->rows.strings;
        wire->width = strings->stride;
        wire->count = strings->count;
        wire->bytes = strings->bytes;
        wire->free_count = strings->free_count;
        if (!mm_strings_wire_system(source->native_encoding, wire->system, &wire->system) ||
            !mm_strings_translate_same(strings, wire->system, source->native_encoding, 0))
            return 0;
        if (swapped)
            mm_strings_swap_offsets(strings);
        break;
    default:
        wire->count = c->rows.array.count;
        wire->free_count = c->rows.array.free_count;
        break;
    }
    return mm_collection_layout(wire) == MM_STORAGE_OK;
}

static int write_collection(void *context, const MMStorageWriter *writer)
{
    Collection *c = context;
    const MMCollectionHeader *wire = &c->wire;
    unsigned char header[20];
    return mm_collection_header_write(wire, header, sizeof(header)) == MM_STORAGE_OK &&
           mm_storage_write(&c->storage, writer, header, wire->header_bytes,
                            wire->first_bytes, wire->second_bytes) == MM_STORAGE_OK;
}

static void read_rows(Parser *parser)
{
    const MMChunkyCollectionHost *host = parser->host;
    Collection *c = &parser->collection;
    MMChunkySourceToken token;
    int hole, strings = c->syntax->kind == MM_COLLECTION_STRINGS;
    if (!next(parser, &token))
        return;
    for (;;)
    {
        if (!strings)
            host->clear_payload(host->context);
        hole = token.kind == ttFree;
        if (hole)
            record_hole(parser);
        else if (token.kind != ttItem)
            break;
        if (!(strings ? string_row(parser, &token, hole) : numeric_row(parser, &token, hole)))
            return;
    }
    if (token.kind != ttEndChunk)
    {
        report(parser, ertNeedEndChunk);
        skip_end(parser);
    }
    if (!failed(parser) &&
        (!prepare_wire(parser) ||
         !host->save(host->context, c->wire.header_bytes + c->wire.first_bytes + c->wire.second_bytes,
                     write_collection, c)))
        report(parser, ertOom);
}

void mm_chunky_parse_collection(const MMChunkySource *source, const MMChunkyCollectionHost *host, int kind)
{
    Parser parser;
    MMChunkyHeaderField field = {0, NULL};
    const CollectionSyntax *syntax;
    size_t i;

    parser.source = source;
    parser.host = host;
    for (i = 0; i < sizeof(syntaxes) / sizeof(*syntaxes) && syntaxes[i].token != kind; ++i)
        ;
    if (i == sizeof(syntaxes) / sizeof(*syntaxes))
    {
        report(&parser, ertBadToken);
        return;
    }
    syntax = &syntaxes[i];
    if (mm_chunky_parse_header(source, &field, 1) != 1)
        report(&parser, syntax->header_error);
    else if (!valid_width(syntax, field.number))
        report(&parser, syntax->size_error);
    else if (init_collection(&parser, syntax, (MMU32)field.number))
    {
        read_rows(&parser);
        mm_storage_dispose(&parser.collection.holes_storage);
        mm_storage_dispose(&parser.collection.storage);
        return;
    }
    else
        report(&parser, ertOom);
    skip_end(&parser);
}
