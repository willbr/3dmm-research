/* Converted from Kauai kidspace_descriptor.cpp and cursor_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/studio_resource.h"
#include "mm/collections.h"
#include <stdlib.h>
#include <string.h>

enum { KIDSPACE_HEADER = 8, KIDSPACE_LOCATION = 16, KIDSPACE_CURSOR = 28, CURSOR_RECORD = 8 };

static MMU32 word(const unsigned char *p, int big)
{
    if (big)
        return ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3];
    return p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}

static MMI32 signed_word(const unsigned char *p, int big)
{
    MMU32 value = word(p, big);
    return value <= 0x7fffffffUL ? (MMI32)value : -1 - (MMI32)(0xffffffffUL - value);
}

int mm_kidspace_read(MMKidspaceDescriptor *descriptor, const MMReadStream *stream, const MMStorageAllocator *allocator)
{
    unsigned char header[KIDSPACE_HEADER];
    const unsigned char *records;
    MMU32 remaining, offset;
    int big;
    if (stream->length < KIDSPACE_HEADER + KIDSPACE_LOCATION || stream->length % 4 ||
        !mm_read_exact(stream, 0, header, sizeof(header)))
        return 0;
    big = header[0] == 0 && header[1] == 1;
    if (!big && (header[0] != 1 || header[1] != 0))
        return 0;
    if (mm_storage_init(&descriptor->storage, allocator) != MM_STORAGE_OK)
        return 0;
    remaining = stream->length - KIDSPACE_HEADER;
    if (mm_storage_read(&descriptor->storage, stream, KIDSPACE_HEADER, remaining, 0) != MM_STORAGE_OK)
        goto fail;
    records = descriptor->storage.buffers[0].data;
    for (offset = 0; remaining >= KIDSPACE_LOCATION; offset += KIDSPACE_LOCATION)
    {
        remaining -= KIDSPACE_LOCATION;
        if (!word(records + offset, big))
        {
            if (remaining % KIDSPACE_CURSOR)
                break;
            descriptor->kind = signed_word(header + 4, big);
            descriptor->locations = offset / KIDSPACE_LOCATION + 1;
            descriptor->cursors = remaining / KIDSPACE_CURSOR;
            descriptor->big_endian = big;
            return 1;
        }
    }
fail:
    mm_storage_dispose(&descriptor->storage);
    return 0;
}

void mm_kidspace_dispose(MMKidspaceDescriptor *descriptor)
{
    mm_storage_dispose(&descriptor->storage);
}

void mm_kidspace_location(const MMKidspaceDescriptor *descriptor, MMI32 parent, MMKidspaceLocation *out)
{
    const unsigned char *row = descriptor->storage.buffers[0].data;
    int big = descriptor->big_endian;
    MMI32 id = signed_word(row, big);
    while (id && id != parent)
    {
        row += KIDSPACE_LOCATION;
        id = signed_word(row, big);
    }
    out->hidPar = id;
    out->xp = signed_word(row + 4, big);
    out->yp = signed_word(row + 8, big);
    out->zp = signed_word(row + 12, big);
}

int mm_kidspace_cursor(const MMKidspaceDescriptor *descriptor, MMU32 flags, MMU32 state, MMKidspaceCursor *out)
{
    const unsigned char *row = descriptor->storage.buffers[0].data + descriptor->locations * KIDSPACE_LOCATION;
    MMU32 i, mask, required, states;
    int big = descriptor->big_endian;
    state = (MMU32)1 << (state & 31);
    for (i = 0; i < descriptor->cursors; ++i, row += KIDSPACE_CURSOR)
    {
        states = word(row + 8, big);
        mask = word(row, big);
        required = word(row + 4, big);
        if ((states & state) && (mask & flags) == required)
        {
            out->grfcustMask = mask;
            out->grfcust = required;
            out->grfbitSno = states;
            out->cnoCurs = word(row + 12, big);
            out->chidScript = word(row + 16, big);
            out->cidDefault = signed_word(row + 20, big);
            out->cnoTopic = word(row + 24, big);
            return 1;
        }
    }
    return 0;
}

struct MMKidspaceOwner
{
    MMKidspaceDescriptor descriptor;
    MMResourceObject binding;
    MMReference references;
    const MMKidspaceOwnerHost *host;
};

static MMResourceObject *kidspace_binding(void *object) { return &((MMKidspaceOwner *)object)->binding; }
static MMReference *kidspace_references(void *object) { return &((MMKidspaceOwner *)object)->references; }

static void kidspace_destroy(void *object)
{
    MMKidspaceOwner *owner = object;
    const MMStorageAllocator *allocator = owner->host ? owner->host->objects : NULL;
    mm_kidspace_dispose(&owner->descriptor);
    mm_resource_object_dispose(owner, mm_kidspace_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, sizeof(*owner));
    else
        free(owner);
}

static void kidspace_mark(void *object)
{
    MMKidspaceOwner *owner = object;
    if (owner->host && owner->host->mark)
        owner->host->mark(owner);
}

const MMResourceObjectServices *mm_kidspace_owner_services(void)
{
    static const MMResourceObjectServices services = {
        kidspace_binding, kidspace_references, kidspace_destroy, NULL, kidspace_mark};
    return &services;
}

int mm_kidspace_owner_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMKidspaceOwnerHost *host)
{
    const MMStorageAllocator *allocator = host ? host->objects : NULL;
    MMBlockIO io;
    MMReadStream stream;
    MMKidspaceOwner *owner;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    if ((block->view.packed &&
         mm_owned_block_code(block, 0, host ? host->decode_context : NULL, host ? host->decode : NULL, NULL) !=
             MM_DATA_OK) ||
        mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK)
        return 0;
    *bytes = (MMI32)block->view.length;
    if (allocator && (!allocator->resize || !allocator->release))
        return 0;
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, sizeof(*owner), MM_STORAGE_NORMAL)
                      : malloc(sizeof(*owner));
    if (!owner)
        return 0;
    memset(owner, 0, sizeof(*owner));
    owner->host = host;
    mm_reference_init(&owner->references);
    mm_resource_object_init(&owner->binding);
    if (!mm_kidspace_read(&owner->descriptor, &stream, host ? host->records : NULL))
    {
        kidspace_destroy(owner);
        return 0;
    }
    *out = owner;
    return 1;
}

const MMKidspaceDescriptor *mm_kidspace_owner_descriptor(const MMKidspaceOwner *owner) { return &owner->descriptor; }
const MMResourceObject *mm_kidspace_owner_binding(const MMKidspaceOwner *owner) { return &owner->binding; }

void mm_kidspace_owner_retain(MMKidspaceOwner *owner)
{
    mm_reference_add(&owner->references);
}

void mm_kidspace_owner_release(MMKidspaceOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_kidspace_owner_services());
}

void mm_kidspace_owner_priority(MMKidspaceOwner *owner, MMI32 priority)
{
    mm_resource_object_priority(owner, priority, mm_kidspace_owner_services());
}

int mm_studio_cursor_read(const MMReadStream *stream, int width, int height, MMStudioCursor *out)
{
    MMCollectionHeader header;
    MMStudioCursor best;
    unsigned char record[CURSOR_RECORD];
    MMU32 row, offset, planes = 0;
    MMI32 bytes;
    int dimension, status;
    if (width < 1 || height < 1 ||
        mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &header) != MM_STORAGE_OK ||
        header.width != CURSOR_RECORD)
        return 0;
    memset(&best, 0, sizeof(best));
    for (row = 0; row < header.count; ++row)
    {
        status = mm_collection_group_read_row(stream, &header, row, &offset, &bytes);
        if (status != MM_STORAGE_OK)
            return 0;
        offset += header.header_bytes;
        if (!mm_read_exact(stream, offset, record, sizeof(record)))
            return 0;
        dimension = record[6];
        if (word(record, header.big_endian) || dimension != record[7] || (dimension != 16 && dimension != 32) ||
            record[4] >= dimension || record[5] >= dimension || dimension > width || dimension > height ||
            dimension <= best.width || bytes != dimension * dimension / 4)
            continue;
        best.width = best.height = dimension;
        best.pitch = dimension / 8;
        best.hot_x = record[4];
        best.hot_y = record[5];
        planes = offset + CURSOR_RECORD;
    }
    bytes = best.pitch * best.height;
    if (!bytes || !mm_read_exact(stream, planes, best.and_bits, bytes) ||
        !mm_read_exact(stream, planes + bytes, best.xor_bits, bytes))
        return 0;
    *out = best;
    return 1;
}
