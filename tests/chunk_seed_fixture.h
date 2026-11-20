#ifndef CHUNK_SEED_FIXTURE_H
#define CHUNK_SEED_FIXTURE_H
#include "chunk_edit_fixture.h"

static void fixture_chunk_seed_link(ChunkIndexFixture *f, MMU32 parent_type, MMU32 parent,
                                    MMU32 type, MMU32 number, MMU32 slot)
{
    MMChunkAccess access = fixture_chunk_access(f);
    const MMChunkEdit edit = fixture_chunk_edit(f);
    MMChunkChild child;
    MMU32 position;
    if (mm_chunk_find(&access, parent_type, parent, &position) == MM_CHUNK_NOT_FOUND)
        fixture_chunk_add(f, parent_type, parent);
    access = fixture_chunk_access(f);
    if (mm_chunk_find(&access, type, number, &position) == MM_CHUNK_NOT_FOUND)
        fixture_chunk_add(f, type, number);
    child.type = type;
    child.number = number;
    child.slot = slot;
    if (mm_chunk_adopt(&edit, parent_type, parent, &child, 1) != MM_CHUNK_OK)
        abort();
}
#endif
