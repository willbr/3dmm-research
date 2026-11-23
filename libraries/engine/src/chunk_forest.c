/* Embedded forests converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_forest.h"
#include "mm/chunk_owner.h"
#include <string.h>
#define FOREST_LIMIT 0x7fffffffUL
static MMU32 get_word(const unsigned char *p, unsigned width, int big)
{
    MMU32 value = 0;
    unsigned i;
    for (i = 0; i < width; ++i)
        value |= (MMU32)p[i] << (8 * (big ? width - 1 - i : i));
    return value;
}
static void put_word(unsigned char *p, unsigned width, int big, MMU32 value)
{
    unsigned i;
    for (i = 0; i < width; ++i)
        p[i] = (unsigned char)(value >> (8 * (big ? width - 1 - i : i)));
}
int mm_forest_write(const MMForestExport *output, const MMStorageAllocator *allocator, MMU32 type,
                          MMU32 number, MMU32 offset, MMU32 *bytes)
{
    MMChunkWalk walk;
    MMChunkChild node, parent;
    MMChunkEntry entry;
    MMU32 flags, total = 0, position = offset;
    unsigned char descriptor[24];
    int result;
    if (!output || !output->entry || (output->write && !output->copy) || offset > FOREST_LIMIT)
        return MM_CHUNK_BAD_FILE;
    result = mm_chunk_walk_init(&walk, allocator);
    if (result != MM_CHUNK_OK)
        return result;
    result = mm_chunk_walk_reset(&walk, &output->graph, type, number);
    if (result != MM_CHUNK_OK)
        goto done;
    while (mm_chunk_walk_next(&walk, 0, &node, &parent, &flags))
    {
        if (flags & MM_WALK_ERROR)
        {
            result = MM_CHUNK_MEMORY;
            goto done;
        }
        if (!(flags & MM_WALK_PRE))
            continue;
        if (!output->entry(output->context, node.type, node.number, &entry) || entry.bytes > FOREST_LIMIT - 24 ||
            total > FOREST_LIMIT - 24 - entry.bytes || (output->write && position > FOREST_LIMIT - 24 - entry.bytes))
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        total += 24 + entry.bytes;
        if (!output->write)
            continue;
        put_word(descriptor, 2, output->big_endian, 1);
        put_word(descriptor + 2, 2, output->big_endian, (MMU32)output->system);
        put_word(descriptor + 4, 4, output->big_endian, node.type);
        put_word(descriptor + 8, 4, output->big_endian, flags & MM_WALK_ROOT ? 0xffffffffUL : node.slot);
        put_word(descriptor + 12, 4, output->big_endian, entry.bytes);
        put_word(descriptor + 16, 4, output->big_endian, entry.child_count);
        put_word(descriptor + 20, 4, output->big_endian, entry.flags & (MM_CHUNK_PACKED | MM_CHUNK_FOREST));
        if (!output->write(output->context, position, descriptor, sizeof(descriptor)) ||
            !output->copy(output->context, node.type, node.number, position + 24, entry.bytes))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        position += 24 + entry.bytes;
    }
    if (bytes)
        *bytes = total;
done:
    mm_chunk_walk_dispose(&walk);
    return result;
}
typedef struct ForestParent
{
    MMU32 type, number, remaining;
} ForestParent;
int mm_forest_read(const MMReadStream *stream, MMU32 offset, MMU32 bytes,
                         const MMForestImport *destination, const MMStorageAllocator *allocator, int copy)
{
    MMCollectionStorage storage;
    MMArray stack;
    ForestParent current, node;
    unsigned char descriptor[24];
    MMU32 end, payload, children, flags, slot;
    int big, result = MM_CHUNK_OK;
    if (!stream || !destination || !destination->add || !destination->adopt ||
        (bytes && (offset > stream->length || bytes > stream->length - offset)) || offset > FOREST_LIMIT ||
        bytes > FOREST_LIMIT - offset)
        return MM_CHUNK_BAD_FILE;
    if (mm_storage_init(&storage, allocator) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    storage.min_grow[0] = 128;
    memset(&stack, 0, sizeof(stack));
    stack.storage = &storage;
    stack.width = sizeof(ForestParent);
    stack.free_count = -1;
    memset(&current, 0, sizeof(current));
    end = offset + bytes;
    while (offset < end)
    {
        if (end - offset < 24)
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        if (!mm_read_exact(stream, offset, descriptor, 24))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        offset += 24;
        if (descriptor[0] == 1 && descriptor[1] == 0)
            big = 0;
        else if (descriptor[0] == 0 && descriptor[1] == 1)
            big = 1;
        else
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        payload = get_word(descriptor + 12, 4, big);
        if (payload > end - offset)
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        node.type = get_word(descriptor + 4, 4, big);
        children = get_word(descriptor + 16, 4, big);
        slot = get_word(descriptor + 8, 4, big);
        flags = get_word(descriptor + 20, 4, big) & (MM_CHUNK_PACKED | (copy ? 0 : MM_CHUNK_FOREST));
        if (!destination->add(destination->context, node.type, offset, payload, flags, copy != 0, &node.number))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        offset += payload;
        if (stack.count)
        {
            if (!destination->adopt(destination->context, current.type, current.number, node.type, node.number, slot))
            {
                result = MM_CHUNK_IO;
                goto done;
            }
            --current.remaining;
        }
        if (children && children <= FOREST_LIMIT)
        {
            node.remaining = children;
            if (mm_array_add(&stack, &current, NULL) != MM_STORAGE_OK)
            {
                result = MM_CHUNK_MEMORY;
                goto done;
            }
            current = node;
        }
        else
            while (!current.remaining && stack.count)
                mm_array_pop(&stack, &current);
    }
    if (stack.count || current.remaining)
        result = MM_CHUNK_BAD_FILE;
done:
    mm_storage_dispose(&storage);
    return result;
}

typedef struct ForestArchiveImport
{
    const MMChunkOwnerRead *reader;
    MMChunkEdit edit;
    void *source;
} ForestArchiveImport;

static int import_copied_chunk(const ForestArchiveImport *import, MMU32 type, MMU32 offset, MMU32 bytes,
                               MMU32 flags, MMU32 *number)
{
    const MMChunkOwnerRead *reader = import->reader;
    MMBlockFile extent;
    MMOwnedBlock block;
    int ok;
    if (mm_owned_block_init(&block, reader->payloads.handles, reader->payloads.files) != MM_DATA_OK)
        return 0;
    extent.file = import->source;
    extent.offset = offset;
    extent.length = bytes;
    ok = mm_owned_block_set_file(&block, &extent, (flags & MM_CHUNK_PACKED) != 0) == MM_DATA_OK &&
         mm_chunk_add_block_copy(&import->edit, reader->payloads.files, reader->storage->allocator, type,
                                 &block, number) == MM_CHUNK_OK;
    mm_owned_block_clear(&block);
    return ok;
}

static int import_chunk(void *context, MMU32 type, MMU32 offset, MMU32 bytes, MMU32 flags,
                        int copy, MMU32 *number)
{
    const ForestArchiveImport *import = context;
    const MMChunkEdit *edit = &import->edit;
    MMChunkEntry entry = {0};
    MMU32 position;
    if (copy)
        return import_copied_chunk(import, type, offset, bytes, flags, number);
    if (mm_chunk_unique(edit, type, &position, &entry.number) != MM_CHUNK_OK || !edit->insert(edit->context, position))
        return 0;
    entry.type = type;
    entry.offset = bytes ? offset : 0;
    entry.bytes = bytes;
    entry.flags = MM_CHUNK_LONER | flags;
    edit->store(edit->context, position, &entry, 1);
    *number = entry.number;
    return 1;
}

static int adopt_chunk(void *context, MMU32 parent, MMU32 number, MMU32 type, MMU32 child, MMU32 slot)
{
    const ForestArchiveImport *import = context;
    MMChunkChild link;
    link.type = type;
    link.number = child;
    link.slot = slot;
    return mm_chunk_adopt(&import->edit, parent, number, &link, 1) == MM_CHUNK_OK;
}

int mm_chunk_reader_import_forest(const MMChunkOwnerRead *reader, const MMReadStream *stream,
                                  void *source, MMU32 offset, MMU32 bytes, int copy)
{
    ForestArchiveImport import;
    MMForestImport services = {NULL, import_chunk, adopt_chunk};
    import.reader = reader;
    import.edit = mm_chunk_owner_editor(reader->storage, reader->write.max_references,
                                        reader->write.scan_all_references);
    import.source = source;
    services.context = &import;
    return mm_forest_read(stream, offset, bytes, &services, reader->storage->allocator, copy);
}

typedef struct ForestArchiveExport
{
    MMChunkOwnerStorage *storage;
    void *destination;
} ForestArchiveExport;

static int export_chunk(void *context, MMU32 type, MMU32 number, MMChunkEntry *entry)
{
    const ForestArchiveExport *archive = context;
    const MMChunkAccess access = mm_chunk_owner_access(archive->storage);
    MMU32 position;
    return mm_chunk_find(&access, type, number, &position) == MM_CHUNK_OK &&
           mm_chunk_at(&access, position, entry) == MM_CHUNK_OK;
}

static int export_descriptor(void *context, MMU32 offset, const void *bytes, size_t count)
{
    const ForestArchiveExport *archive = context;
    return archive->storage->io.write_at(archive->destination, offset, bytes, count);
}

static int export_payload(void *context, MMU32 type, MMU32 number, MMU32 offset, MMU32 bytes)
{
    const ForestArchiveExport *archive = context;
    const MMChunkOwnerFileIO *io = &archive->storage->io;
    MMBlockFile extent;
    int packed;
    return mm_chunk_owner_locate(archive->storage, type, number, &extent, &packed) && extent.length == bytes &&
           io->copy(io->context, extent.file, extent.offset, archive->destination, offset, bytes);
}

int mm_chunk_reader_export_forest(const MMChunkOwnerRead *reader, void *destination, MMU32 type,
                                  MMU32 number, MMU32 offset, MMU32 *bytes)
{
    const unsigned short order = 1;
    const MMChunkEdit edit = mm_chunk_owner_editor(reader->storage, reader->write.max_references,
                                                  reader->write.scan_all_references);
    ForestArchiveExport archive;
    MMForestExport output = {0};
    archive.storage = reader->storage;
    archive.destination = destination;
    output.context = &archive;
    output.graph = mm_chunk_edit_graph(&edit);
    output.entry = export_chunk;
    output.big_endian = *(const unsigned char *)&order == 0;
    output.system = reader->write.system;
    if (destination)
    {
        if (!archive.storage->io.write_at || !archive.storage->io.copy)
            return MM_CHUNK_BAD_FILE;
        output.write = export_descriptor;
        output.copy = export_payload;
    }
    return mm_forest_write(&output, reader->storage->allocator, type, number, offset, bytes);
}
