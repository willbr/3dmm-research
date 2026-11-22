#ifndef MOVIE_ARCHIVE_FIXTURE_H
#define MOVIE_ARCHIVE_FIXTURE_H

#include "mm/chunk_owner.h"

// Seed or inspect the working archive through the same owner used by the movie.
struct MovieArchiveFixture
{
    MMChunkOwnerRead *reader;
    MMArchiveWriter writer;

    explicit MovieArchiveFixture(MMResourceCache *cache)
        : reader(cache ? mm_resource_cache_reader(cache) : nullptr),
          writer(cache ? mm_resource_cache_writer(cache) : MMArchiveWriter{})
    {
    }

    explicit MovieArchiveFixture(PChunkyResourceFile resources)
        : MovieArchiveFixture(resources ? resources->CoreCache() : nullptr)
    {
    }

    MMChunkAccess Chunks() const { return mm_chunk_edit_access(&writer.edit); }

    bool Add(MMU32 bytes, ChunkTagOrType type, ChunkNumber *number, PDataBlock block = nullptr) const
    {
        return mm_chunk_add_block(&writer.edit, type, bytes, number, block ? block->CoreBlock() : nullptr) == MM_CHUNK_OK;
    }

    bool AddBytes(const void *bytes, MMU32 count, ChunkTagOrType type, ChunkNumber *number) const
    {
        return mm_chunk_add_bytes(&writer.edit, writer.payloads->files, type, bytes, count, number) == MM_CHUNK_OK;
    }

    bool PutBytes(const void *bytes, MMU32 count, ChunkTagOrType type, ChunkNumber number) const
    {
        return mm_chunk_put_bytes(&writer.edit, writer.payloads->files, type, number, bytes, count) == MM_CHUNK_OK;
    }

    bool Adopt(ChunkTagOrType parentType, ChunkNumber parent, ChunkTagOrType type, ChunkNumber number,
               ChildChunkID slot) const
    {
        const MMChunkChild child{type, number, slot};
        return mm_chunk_adopt(&writer.edit, parentType, parent, &child, 1) == MM_CHUNK_OK;
    }

    bool AddChild(ChunkTagOrType parentType, ChunkNumber parent, ChildChunkID slot, MMU32 bytes,
                  ChunkTagOrType type, ChunkNumber *number, PDataBlock block = nullptr) const
    {
        MMChunkChild child{type, 0, slot};
        if (mm_chunk_add_child_block(&writer.edit, parentType, parent, &child, bytes,
                                      block ? block->CoreBlock() : nullptr) != MM_CHUNK_OK)
            return false;
        *number = child.number;
        return true;
    }

    bool AddChildBytes(ChunkTagOrType parentType, ChunkNumber parent, ChildChunkID slot,
                       const void *bytes, MMU32 count, ChunkTagOrType type, ChunkNumber *number) const
    {
        DataBlock block;
        return AddChild(parentType, parent, slot, count, type, number, &block) && block.FWriteRgb(
            const_cast<void *>(bytes), count, 0);
    }

    bool Find(ChunkTagOrType type, ChunkNumber number, PDataBlock block) const
    {
        return mm_chunk_payload_bind_block(writer.payloads, reader, type, number, block->CoreBlock()) ==
               MM_CHUNK_PAYLOAD_OK;
    }

    bool Link(ChunkTagOrType parentType, ChunkNumber parent, ChildChunkID slot, ChunkTagOrType type,
              ChunkNumber *number) const
    {
        const auto chunks = Chunks();
        return mm_chunk_link_number(&chunks, parentType, parent, slot, type, number) == MM_CHUNK_OK;
    }

    bool SetName(ChunkTagOrType type, ChunkNumber number, PString name) const
    {
        byte wire[kcbMaxDataStn];
        const auto value = ChunkyFile::EncodeName(name, wire);
        return mm_chunk_owner_write_name(reader->storage, type, number, reader->max_name_bytes,
                                         value.wire, value.bytes) == MM_CHUNK_OK;
    }

    void Delete(ChunkTagOrType type, ChunkNumber number) const { mm_chunk_delete(&writer.edit, type, number); }
};

#endif
