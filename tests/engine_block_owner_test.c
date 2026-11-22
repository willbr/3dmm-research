#include "mm/archive_reader.h"
#include "mm/block_owner.h"
#include "mm/movie_session.h"
#include "mm/scene_data.h"
#include "mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "owner check %d: %s\n", __LINE__, #x);                                                     \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Host
{
    int handles, files, locks, alloc_fail, temp_fail, read_fail, write_fail, code_fail;
    unsigned allocs, temps, codes;
} Host;
typedef struct Handle
{
    MMU32 size;
    int locks;
    unsigned char bytes[1];
} Handle;
typedef struct File
{
    unsigned refs;
    MMU32 size;
    unsigned char bytes[8192];
} File;
static int allocate(void *context, MMU32 n, void **out)
{
    Host *h = (Host *)context;
    Handle *p;
    ++h->allocs;
    *out = NULL;
    if (h->alloc_fail)
        return 0;
    p = (Handle *)malloc(sizeof(*p) + n);
    CHECK(p);
    p->size = n;
    p->locks = 0;
    if (n <= 8192)
        memset(p->bytes, 0xcc, n);
    ++h->handles;
    *out = p;
    return 1;
}
static void release_handle(void *context, void *p)
{
    Host *h = (Host *)context;
    CHECK(!((Handle *)p)->locks);
    --h->handles;
    free(p);
}
static MMU32 handle_size(void *context, void *p)
{
    (void)context;
    return ((Handle *)p)->size;
}
static void *lock(void *context, void *p)
{
    Host *h = (Host *)context;
    ++h->locks;
    ++((Handle *)p)->locks;
    return ((Handle *)p)->bytes;
}
static void unlock(void *context, void *p)
{
    Host *h = (Host *)context;
    CHECK(h->locks && ((Handle *)p)->locks);
    --h->locks;
    --((Handle *)p)->locks;
}
static int resize(void *context, void **owned, MMU32 n)
{
    Handle *old = (Handle *)*owned, *next;
    (void)context;
    CHECK(!old->locks);
    next = (Handle *)malloc(sizeof(*next) + n);
    CHECK(next);
    next->size = n;
    next->locks = 0;
    memcpy(next->bytes, old->bytes, n < old->size ? n : old->size);
    free(old);
    *owned = next;
    return 1;
}
static void *create_temp(void *context)
{
    Host *h = (Host *)context;
    File *p;
    ++h->temps;
    if (h->temp_fail)
        return NULL;
    p = (File *)malloc(sizeof(*p));
    CHECK(p);
    p->refs = 1;
    p->size = 0;
    ++h->files;
    return p;
}
static void retain_file(void *context, void *p)
{
    (void)context;
    CHECK(((File *)p)->refs);
    ++((File *)p)->refs;
}
static void release_file(void *context, void *p)
{
    Host *h = (Host *)context;
    File *f = (File *)p;
    CHECK(f->refs);
    if (!--f->refs)
    {
        --h->files;
        free(f);
    }
}
static int read_file(void *context, void *file, MMU32 offset, void *out, size_t n)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    if (h->read_fail)
        return 0;
    CHECK(mm_range_contains(f->size, offset, (MMU32)n));
    memcpy(out, f->bytes + offset, n);
    return 1;
}
static int write_file(void *context, void *file, MMU32 offset, const void *input, size_t n)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    if (h->write_fail)
        return 0;
    CHECK(mm_range_contains(sizeof(f->bytes), offset, (MMU32)n));
    memcpy(f->bytes + offset, input, n);
    if (offset + n > f->size)
        f->size = offset + (MMU32)n;
    return 1;
}
static void services(Host *h, MMHandleServices *handles, MMBlockFileServices *files)
{
    handles->context = h;
    handles->allocate = allocate;
    handles->release = release_handle;
    handles->size = handle_size;
    handles->lock = lock;
    handles->unlock = unlock;
    handles->resize = resize;
    files->context = h;
    files->retain = retain_file;
    files->release = release_file;
    files->create_temp = create_temp;
    files->read = read_file;
    files->write = write_file;
}
static MMU32 hash = 2166136261UL;
static void hash_byte(unsigned char n)
{
    hash = (hash ^ n) * 16777619UL;
}
static void hash_word(MMU32 n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        hash_byte((unsigned char)(n >> (8 * i)));
}
static void golden(void)
{
    static const MMU32 sizes[] = {0, 1, 4096, 0x007fffff, 0x00800000, 0x00800001};
    Host h;
    MMHandleServices handles;
    MMBlockFileServices files;
    MMOwnedBlock owner;
    MMBlockIO io;
    MMBlockFile file;
    unsigned char bytes[4096], actual[4096];
    MMU32 physical;
    void *seed;
    unsigned s, force, packed, i;
    int file_expected;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)(i * 19);
    for (s = 0; s < 6; ++s)
        for (force = 0; force < 2; ++force)
            for (packed = 0; packed < 2; ++packed)
            {
                memset(&h, 0, sizeof(h));
                services(&h, &handles, &files);
                CHECK(mm_owned_block_init(&owner, &handles, &files) == MM_DATA_OK);
                CHECK(allocate(&h, 8, &seed));
                CHECK(mm_owned_block_set_handle(&owner, &seed, packed) == MM_DATA_OK && !seed);
                CHECK(mm_owned_block_set_temp(&owner, sizes[s], force) == MM_DATA_OK &&
                      owner.view.packed == (int)packed);
                file_expected = force || sizes[s] >= 0x00800000;
                CHECK(owner.view.kind == (file_expected ? MM_BLOCK_FILE : MM_BLOCK_MEMORY));
                CHECK(owner.view.length == sizes[s]);
                memset(&file, 0, sizeof(file));
                if (sizes[s] <= 4096)
                {
                    CHECK(mm_owned_block_io(&owner, &io) == MM_DATA_OK);
                    CHECK(mm_block_write(&io, 0, bytes, sizes[s], 1) == MM_DATA_OK);
                    if (sizes[s] == 4096)
                        CHECK(mm_block_move_min(&owner.view, 1) == MM_DATA_OK &&
                              mm_block_move_lim(&owner.view, -2) == MM_DATA_OK);
                }
                physical = 0xffffffffUL;
                if (sizes[s] <= 4096 || file_expected)
                {
                    CHECK(mm_owned_block_materialize(&owner, 1, &file) == MM_DATA_OK);
                    CHECK((file.file != NULL) == (file_expected || sizes[s] != 0));
                    if (file.file)
                    {
                        physical = ((File *)file.file)->size;
                        CHECK(file.length == owner.view.length);
                        if (sizes[s] <= 4096)
                        {
                            CHECK(read_file(&h, file.file, file.offset, actual, file.length));
                            CHECK(!memcmp(actual, bytes + (sizes[s] == 4096), file.length));
                        }
                    }
                }
                hash_word(sizes[s]);
                hash_word(force);
                hash_word(packed);
                hash_word(owner.view.length);
                hash_word(owner.view.kind == MM_BLOCK_MEMORY ? owner.view.capacity : 0);
                hash_word(file.file != NULL);
                hash_word(file.offset);
                hash_word(file.length);
                hash_word(physical);
                if (file.file && sizes[s] <= 4096)
                    for (i = 0; i < file.length; ++i)
                        hash_byte(actual[i]);
                CHECK(mm_owned_block_clear(&owner) == MM_DATA_OK);
                if (file.file)
                {
                    CHECK(((File *)file.file)->refs == 1);
                    if (sizes[s] <= 4096)
                        CHECK(read_file(&h, file.file, file.offset, actual, file.length));
                    release_file(&h, file.file);
                }
                CHECK(!h.handles && !h.files && !h.locks);
                CHECK(mm_owned_block_clear(&owner) == MM_DATA_OK);
            }
    /* Frozen from 17,240 bytes produced by the original native implementation. */
    CHECK(hash == 0x61899d81UL);
}
static void failures(void)
{
    Host h;
    MMHandleServices handles;
    MMBlockFileServices files;
    MMOwnedBlock owner, before;
    MMBlockFile file, sentinel;
    MMBlockIO io;
    void *seed;
    unsigned allocs;
    memset(&h, 0, sizeof(h));
    services(&h, &handles, &files);
    CHECK(mm_owned_block_init(&owner, &handles, &files) == MM_DATA_OK);
    CHECK(allocate(&h, 16, &seed));
    memcpy(((Handle *)seed)->bytes, "abcdefghijklmnop", 16);
    CHECK(mm_owned_block_set_handle(&owner, &seed, 1) == MM_DATA_OK);
    before = owner;
    h.alloc_fail = h.temp_fail = 1;
    CHECK(mm_owned_block_set_temp(&owner, 17, 0) == MM_DATA_MEMORY &&
          !memcmp(&owner, &before, sizeof(owner)));
    allocs = h.allocs;
    CHECK(mm_owned_block_set_temp(&owner, 17, 1) == MM_DATA_MEMORY && h.allocs == allocs &&
          !memcmp(&owner, &before, sizeof(owner)));
    h.alloc_fail = 0;
    memset(&sentinel, 0, sizeof(sentinel));
    sentinel.file = (void *)1;
    sentinel.offset = 123;
    sentinel.length = 456;
    file = sentinel;
    CHECK(mm_owned_block_materialize(&owner, 0, &file) == MM_DATA_PACKED &&
          !memcmp(&file, &sentinel, sizeof(file)));
    CHECK(mm_owned_block_materialize(&owner, 1, &file) == MM_DATA_MEMORY &&
          !memcmp(&file, &sentinel, sizeof(file)));
    h.temp_fail = 0;
    h.write_fail = 1;
    CHECK(mm_owned_block_materialize(&owner, 1, &file) == MM_DATA_IO &&
          !memcmp(&file, &sentinel, sizeof(file)));
    CHECK(!memcmp(&owner, &before, sizeof(owner)) && !h.files && h.handles == 1 && !h.locks);
    h.write_fail = 0;
    CHECK(mm_owned_block_materialize(&owner, 1, &file) == MM_DATA_OK && h.files == 1 &&
          owner.view.kind == MM_BLOCK_MEMORY);
    CHECK(!memcmp(((File *)file.file)->bytes, "abcdefghijklmnop", 16));
    release_file(&h, file.file);
    h.alloc_fail = 1;
    CHECK(mm_owned_block_set_temp(&owner, 17, 0) == MM_DATA_OK && owner.view.kind == MM_BLOCK_FILE &&
          owner.view.packed);
    CHECK(!h.handles && h.files == 1 && ((File *)owner.file)->size == 0);
    h.alloc_fail = 0;
    CHECK(mm_owned_block_io(&owner, &io) == MM_DATA_OK);
    CHECK(mm_block_write(&io, 0, "abc", 3, 1) == MM_DATA_OK && ((File *)owner.file)->size == 3);
    CHECK(mm_owned_block_materialize(&owner, 1, &file) == MM_DATA_OK && file.file == owner.file &&
          ((File *)file.file)->refs == 2);
    CHECK(mm_owned_block_clear(&owner) == MM_DATA_OK && ((File *)file.file)->refs == 1);
    release_file(&h, file.file);
    CHECK(!h.files && !h.handles && !h.locks);
}
static int transform(void *context, void **handle)
{
    Host *h = (Host *)context;
    void *output;
    ++h->codes;
    if (h->code_fail)
        return 0;
    if (!allocate(context, 4, &output))
        return 0;
    memcpy(((Handle *)output)->bytes, "CODE", 4);
    release_handle(context, *handle);
    *handle = output;
    return 1;
}
static void coding(void)
{
    Host h;
    MMHandleServices handles;
    MMBlockFileServices files;
    MMOwnedBlock owner;
    MMBlockFile source;
    File *external;
    void *owned;
    unsigned pack;
    int shrink;
    for (pack = 0; pack < 2; ++pack)
    {
        memset(&h, 0, sizeof(h));
        services(&h, &handles, &files);
        CHECK(mm_owned_block_init(&owner, &handles, &files) == MM_DATA_OK);
        external = (File *)create_temp(&h);
        external->size = 16;
        memcpy(external->bytes, "abcdefghijklmnop", 16);
        source.file = external;
        source.offset = 0;
        source.length = 16;
        CHECK(mm_owned_block_set_file(&owner, &source, !pack) == MM_DATA_OK && external->refs == 2);
        CHECK(mm_owned_block_code(&owner, !pack, &h, transform, &shrink) == MM_DATA_OK && !h.codes &&
              external->refs == 2);
        h.read_fail = 1;
        CHECK(mm_owned_block_code(&owner, pack, &h, transform, &shrink) != MM_DATA_OK && !h.handles &&
              external->refs == 2);
        h.read_fail = 0;
        h.code_fail = 1;
        CHECK(mm_owned_block_code(&owner, pack, &h, transform, &shrink) == MM_DATA_CODEC &&
              owner.file == external && external->refs == 2 && !h.handles);
        h.code_fail = 0;
        CHECK(mm_owned_block_code(&owner, pack, &h, transform, &shrink) == MM_DATA_OK);
        CHECK(!owner.file && owner.view.kind == MM_BLOCK_MEMORY && owner.view.packed == (int)pack &&
              external->refs == 1);
        CHECK(!memcmp(((Handle *)owner.view.memory)->bytes, "CODE", 4));
        CHECK(mm_owned_block_take_handle(&owner, 1, &owned, &shrink) == MM_DATA_OK && !owner.view.memory &&
              owner.view.kind == MM_BLOCK_EMPTY);
        release_handle(&h, owned);
        release_file(&h, external);
        CHECK(mm_owned_block_clear(&owner) == MM_DATA_OK && !h.files && !h.handles && !h.locks);
    }
    memset(&h, 0, sizeof(h));
    services(&h, &handles, &files);
    CHECK(mm_owned_block_init(&owner, &handles, &files) == MM_DATA_OK && allocate(&h, 16, &owned));
    memcpy(((Handle *)owned)->bytes, "abcdefghijklmnop", 16);
    CHECK(mm_owned_block_set_handle(&owner, &owned, 0) == MM_DATA_OK && !owned);
    CHECK(mm_block_move_min(&owner.view, 3) == MM_DATA_OK &&
          mm_block_move_lim(&owner.view, -8) == MM_DATA_OK);
    h.code_fail = 1;
    CHECK(mm_owned_block_code(&owner, 1, &h, transform, &shrink) == MM_DATA_CODEC);
    CHECK(owner.view.start == 0 && owner.view.length == 5 && owner.view.capacity == 5 && !owner.view.packed &&
          !memcmp(((Handle *)owner.view.memory)->bytes, "defgh", 5));
    CHECK(mm_owned_block_clear(&owner) == MM_DATA_OK && !h.handles && !h.files && !h.locks);
}

typedef struct PayloadArchive
{
    File *file;
    int packed;
    MMU32 packed_bytes;
} PayloadArchive;

static int locate_payload(void *context, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    PayloadArchive *archive = (PayloadArchive *)context;
    CHECK(type == 11);
    if (number > 2)
        return 0;
    out->file = number == 1 ? archive->file : NULL;
    out->offset = number == 1 ? 3 : 0;
    out->length = number != 1 ? 0 : archive->packed ? archive->packed_bytes : 4;
    *packed = archive->packed;
    return 1;
}

static int reject_payload(void *context, const MMReadStream *stream)
{
    Host *h = (Host *)context;
    unsigned char bytes[4];
    CHECK(h->handles == 1 && stream->length == 4);
    CHECK(mm_read_exact(stream, 1, bytes, 3) && !memcmp(bytes, "ODE", 3));
    CHECK(!mm_read_exact(stream, 3, bytes, 2));
    return MM_MOVIE_MEMORY;
}

static void payloads(void)
{
    Host h;
    MMHandleServices handles;
    MMBlockFileServices files;
    MMChunkPayloadServices input;
    PayloadArchive archive;
    MMChunkEntry entry;
    unsigned char output[5];
    memset(&h, 0, sizeof(h));
    memset(&entry, 0, sizeof(entry));
    services(&h, &handles, &files);
    archive.file = (File *)create_temp(&h);
    CHECK(archive.file);
    archive.file->size = 16;
    memcpy(archive.file->bytes, "0123456789abcdef", 16);
    archive.packed = 0;
    archive.packed_bytes = 9;
    input.handles = &handles;
    input.files = &files;
    input.locate = locate_payload;
    input.decode_context = &h;
    input.decode = transform;
    entry.type = 11;
    entry.number = 1;

    CHECK(mm_movie_block_read(&input, &archive, &entry, output, 4) == MM_MOVIE_OK);
    CHECK(!memcmp(output, "3456", 4) && archive.file->refs == 1 && !h.codes);
    memset(output, 0x5a, sizeof(output));
    CHECK(mm_movie_block_read(&input, &archive, &entry, output, 5) == MM_MOVIE_BAD_FILE);
    CHECK(output[0] == 0x5a && archive.file->refs == 1);
    h.read_fail = 1;
    CHECK(mm_movie_block_read(&input, &archive, &entry, output, 4) == MM_MOVIE_IO);
    CHECK(archive.file->refs == 1);
    h.read_fail = 0;

    archive.packed = 1;
    CHECK(mm_movie_block_payload(&input, &archive, &entry, reject_payload, &h) == MM_MOVIE_MEMORY);
    CHECK(h.codes == 1 && !h.handles && !h.locks && archive.file->refs == 1);
    h.code_fail = 1;
    CHECK(mm_movie_block_payload(&input, &archive, &entry, reject_payload, &h) == MM_MOVIE_BAD_FILE);
    CHECK(h.codes == 2 && !h.handles && !h.locks && archive.file->refs == 1);
    entry.number = 2;
    CHECK(mm_movie_block_read(&input, &archive, &entry, NULL, 0) == MM_MOVIE_OK);
    entry.number = 3;
    CHECK(mm_movie_block_read(&input, &archive, &entry, NULL, 0) == MM_MOVIE_BAD_FILE);
    CHECK(h.codes == 2 && archive.file->refs == 1);
    release_file(&h, archive.file);
    CHECK(!h.files && !h.handles && !h.locks);
}

typedef struct PayloadObserver
{
    MMChunkPayloadCache *cache;
    File *file;
    unsigned invalidations, acceptances;
    int reject;
    unsigned previous_refs;
} PayloadObserver;

static void invalidate_payload(void *context)
{
    PayloadObserver *observer = (PayloadObserver *)context;
    CHECK(observer->cache->position == (size_t)-1);
    observer->previous_refs = observer->file->refs;
    ++observer->invalidations;
}

static int accept_payload(void *context)
{
    PayloadObserver *observer = (PayloadObserver *)context;
    CHECK(observer->cache->position == (size_t)-1);
    ++observer->acceptances;
    return !observer->reject;
}

static void payload_cache(void)
{
    Host h;
    MMHandleServices handles;
    MMBlockFileServices files;
    MMChunkPayloadServices input;
    MMChunkPayloadCache cache;
    MMChunkPayloadCacheCallbacks callbacks;
    PayloadArchive archive;
    PayloadObserver observer;
    MMChunkEntry entry;
    MMBlockIO io;
    MMReadStream stream;
    unsigned char bytes[8];
    memset(&h, 0, sizeof(h));
    memset(&entry, 0, sizeof(entry));
    memset(&observer, 0, sizeof(observer));
    services(&h, &handles, &files);
    archive.file = (File *)create_temp(&h);
    CHECK(archive.file);
    archive.file->size = 12;
    memcpy(archive.file->bytes, "abcKCDC\0\0\0\4z", 12);
    archive.packed = 0;
    archive.packed_bytes = 9;
    input.handles = &handles;
    input.files = &files;
    input.locate = locate_payload;
    input.decode_context = &h;
    input.decode = transform;
    observer.cache = &cache;
    observer.file = archive.file;
    callbacks.context = &observer;
    callbacks.invalidate = invalidate_payload;
    callbacks.accept = accept_payload;
    entry.type = 11;
    entry.number = 1;

    CHECK(mm_chunk_payload_cache_init(&cache, &input) == MM_CHUNK_PAYLOAD_OK);
    CHECK(!mm_chunk_payload_cache_stream(&cache, &io, &stream));
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 0, &entry, 0, &callbacks) == MM_CHUNK_PAYLOAD_OK);
    CHECK(mm_chunk_payload_cache_stream(&cache, &io, &stream) && stream.length == 4);
    CHECK(mm_read_exact(&stream, 0, bytes, 4) && !memcmp(bytes, "KCDC", 4));
    CHECK(archive.file->refs == 2 && observer.invalidations == 1 && observer.acceptances == 1);
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 0, &entry, 0, &callbacks) == MM_CHUNK_PAYLOAD_OK);
    CHECK(observer.invalidations == 1 && observer.acceptances == 1);

    archive.packed = 1;
    CHECK(mm_chunk_read_stored(&input, &archive, 11, 1, bytes, sizeof(bytes)) &&
          !memcmp(bytes, "KCDC\0\0\0\4", sizeof(bytes)) && !h.codes && archive.file->refs == 2);
    CHECK(!mm_chunk_read_stored(&input, &archive, 11, 3, bytes, sizeof(bytes)) && archive.file->refs == 2);
    entry.flags = MM_CHUNK_PACKED;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 8, &callbacks) == MM_CHUNK_PAYLOAD_LIMIT);
    CHECK(observer.previous_refs == 2 && archive.file->refs == 1 && !h.codes);
    CHECK(!mm_chunk_payload_cache_stream(&cache, &io, &stream));
    archive.file->bytes[10] = 10;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_LIMIT);
    archive.file->bytes[10] = 4;
    archive.packed_bytes = 7;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_BAD_HEADER);
    archive.packed_bytes = 9;
    h.read_fail = 1;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_BAD_HEADER);
    CHECK(!h.codes && observer.acceptances == 1 && archive.file->refs == 1);
    h.read_fail = 0;
    h.code_fail = 1;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_DECODE_FAILED);
    h.code_fail = 0;
    observer.reject = 1;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_INVALID);
    CHECK(h.codes == 2 && !h.handles && archive.file->refs == 1 && !mm_chunk_payload_cache_stream(&cache, &io, &stream));
    observer.reject = 0;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 1, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_OK);
    CHECK(mm_chunk_payload_cache_stream(&cache, &io, &stream) && stream.length == 4 && h.handles == 1);
    CHECK(mm_read_exact(&stream, 0, bytes, 4) && !memcmp(bytes, "CODE", 4));
    entry.number = 3;
    CHECK(mm_chunk_payload_cache_load(&cache, &archive, 2, &entry, 9, &callbacks) == MM_CHUNK_PAYLOAD_MISSING);
    CHECK(!h.handles && observer.invalidations == 9 && observer.acceptances == 3);
    CHECK(mm_chunk_payload_cache_clear(&cache) == MM_CHUNK_PAYLOAD_OK && archive.file->refs == 1);
    release_file(&h, archive.file);
    CHECK(!h.files && !h.handles && !h.locks);
}
enum { SOURCE_ROW, ROOT_ROW, SCENE_ROW, DIRECTORY_ROWS, ADMISSION_ROWS = 6 };

typedef struct DirectoryFixture
{
    File *file;
    MMChunkEntry rows[ADMISSION_ROWS];
    MMChunkChild children[3];
    MMU32 count;
    unsigned roots, scenes, sources, names;
    int cancel, missing_scene, fallback, rootless;
} DirectoryFixture;

static int directory_entry(void *context, MMU32 position, MMChunkEntry *out)
{
    const DirectoryFixture *fixture = (const DirectoryFixture *)context;
    *out = fixture->rows[position];
    return 1;
}

static int directory_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    const DirectoryFixture *fixture = (const DirectoryFixture *)context;
    CHECK(fixture->rows[parent].type == MM_MOVIE_CHUNK);
    *out = fixture->children[position];
    return 1;
}

static int directory_locate(void *context, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    const MMChunkEntry *entry;
    MMU32 i;
    for (i = 0; i < fixture->count; ++i)
    {
        entry = &fixture->rows[i];
        if (entry->type != type || entry->number != number)
            continue;
        if (i == SCENE_ROW && fixture->missing_scene)
            return 0;
        out->file = fixture->file;
        out->offset = entry->offset;
        out->length = entry->bytes;
        *packed = 0;
        return 1;
    }
    return 0;
}

static int directory_root(void *context, MMU32 number)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    CHECK(number == 7);
    ++fixture->roots;
    return 1;
}

static int directory_scene(void *context, MMU32 slot, const MMMovieSessionScene *scene)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    CHECK(slot == 4 && scene->number == 12 && scene->first_frame == 2 && scene->last_frame == 9);
    ++fixture->scenes;
    return 1;
}

static int directory_source(void *context, const MMMovieSourceName *source)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    if (fixture->fallback)
        CHECK(source->id == MM_MOVIE_RETAIL_SOURCE_ID && source->size == sizeof(MM_MOVIE_RETAIL_SOURCE_NAME) - 1 &&
              !memcmp(source->bytes, MM_MOVIE_RETAIL_SOURCE_NAME, source->size));
    else
    {
        CHECK(source->id == 9);
        if (!(fixture->sources & 1))
            CHECK(source->size == 1 && source->bytes[0] == 'X');
        else
            CHECK(!source->size);
    }
    ++fixture->sources;
    return !fixture->cancel;
}

static void archive_directory(void)
{
    const unsigned char source[] = {
        1, 0, 3, 3,  8, 0, 0, 0,  2, 0, 0, 0,  5, 0, 0, 0,  255, 255, 255, 255,
        1, 'X', 0, 0, 0,  0, 0, 0, 0, 9, 0, 0, 0,  3, 0, 0, 0, 9, 0, 0, 0
    };
    const unsigned char root[] = {1, 0, 3, 3,  2, 0, 2, 0};
    const unsigned char scene[] = {1, 0, 3, 3,  9, 0, 0, 0,  2, 0, 0, 0,  1, 0, 0, 0};
    DirectoryFixture fixture = {0};
    const MMChunkEntry rows[] = {
        {0x47535420UL, 5, 0, sizeof(source), 0, 0, 1, 0},
        {MM_MOVIE_CHUNK, 7, sizeof(source), sizeof(root), 0, 2, 0, 24},
        {MM_SCENE_CHUNK, 12, sizeof(source) + sizeof(root), sizeof(scene), 0, 0, 1, 0}
    };
    const MMChunkChild children[] = {{0x47535420UL, 5, 1}, {MM_SCENE_CHUNK, 12, 4}};
    Host host = {0};
    MMHandleServices handles;
    MMBlockFileServices files;
    MMChunkPayloadServices payload = {0};
    MMChunkAccess chunks = {0, DIRECTORY_ROWS, directory_entry, directory_child};
    MMArchiveReaderOptions options = {1024, 1024, 0, NULL, NULL, NULL, NULL, NULL};
    MMArchiveDirectorySink sink = {0, directory_root, directory_scene, directory_source};
    MMArchiveReader *reader;
    MMArchiveDirectoryError error;
    MMMovieSession *retained;
    MMMovieSessionScene selected;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];

    services(&host, &handles, &files);
    fixture.file = (File *)create_temp(&host);
    CHECK(fixture.file);
    CHECK(write_file(&host, fixture.file, rows[SOURCE_ROW].offset, source, sizeof(source)));
    CHECK(write_file(&host, fixture.file, rows[ROOT_ROW].offset, root, sizeof(root)));
    CHECK(write_file(&host, fixture.file, rows[SCENE_ROW].offset, scene, sizeof(scene)));
    memcpy(fixture.rows, rows, sizeof(rows));
    memcpy(fixture.children, children, sizeof(children));
    fixture.count = DIRECTORY_ROWS;
    payload.handles = &handles;
    payload.files = &files;
    payload.locate = directory_locate;
    payload.decode_context = &host;
    payload.decode = transform;
    chunks.context = sink.context = &fixture;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    fixture.missing_scene = 1;
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_PAYLOAD &&
          error.root == 7 && error.position == SCENE_ROW && error.payload_status == MM_ARCHIVE_MISSING_PAYLOAD);
    CHECK(fixture.roots == 1 && !fixture.scenes && !mm_archive_reader_movie(reader));
    fixture.missing_scene = 0;
    fixture.cancel = 1;
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_CANCELLED &&
          fixture.scenes == 1 && fixture.sources == 1 && !mm_archive_reader_movie(reader));
    fixture.cancel = 0;
    fixture.sources = 0;
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_OK && fixture.sources == 2);
    retained = mm_archive_reader_movie(reader);
    CHECK(retained && mm_movie_session_scene(retained, 4, &selected) == MM_MOVIE_OK && selected.number == 12);
    CHECK(mm_movie_session_source_name(retained, 9, name) == MM_MOVIE_OK && !strcmp(name, "X"));
    fixture.cancel = 1;
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_CANCELLED &&
          mm_archive_reader_movie(reader) == retained);
    mm_archive_reader_close(reader);
    CHECK(fixture.file->refs == 1 && !host.handles);

    fixture.cancel = 0;
    options.max_string_bytes = 1;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT &&
          error.position == SOURCE_ROW && !mm_archive_reader_movie(reader));
    mm_archive_reader_close(reader);

    fixture.fallback = 1;
    fixture.children[0] = children[1];
    fixture.rows[ROOT_ROW].child_count = 1;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_OK && fixture.sources == 4);
    mm_archive_reader_close(reader);
    chunks.count = 0;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    CHECK(mm_archive_reader_directory(reader, &sink, &error) == MM_ARCHIVE_DIRECTORY_OK &&
          fixture.sources == 4 && !mm_archive_reader_movie(reader));
    mm_archive_reader_close(reader);
    CHECK(fixture.file->refs == 1 && !host.handles && !host.locks);
    release_file(&host, fixture.file);
    CHECK(!host.files);
}

static int admission_scene(void *context, MMU32 slot, const MMMovieSessionScene *scene)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    CHECK(scene->first_frame == 2 && scene->last_frame == 9 && scene->transition == 1);
    CHECK(slot == (fixture->rootless ? fixture->scenes : 4));
    CHECK(scene->number == (fixture->scenes ? 0xffffffffUL : 12));
    ++fixture->scenes;
    return 1;
}

static int admission_source(void *context, const MMMovieSourceName *source)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    CHECK(source->id == (fixture->rootless ? 1 : MM_MOVIE_RETAIL_SOURCE_ID));
    CHECK(source->size == sizeof(MM_MOVIE_RETAIL_SOURCE_NAME) - 1 &&
          !memcmp(source->bytes, MM_MOVIE_RETAIL_SOURCE_NAME, source->size));
    ++fixture->sources;
    return 1;
}

static int admission_name(void *context, MMI32 id, const void *text, MMU32 units)
{
    DirectoryFixture *fixture = (DirectoryFixture *)context;
    CHECK(id == 20 && units == 1 && *(const char *)text == (fixture->names ? 'B' : 'A'));
    ++fixture->names;
    return !fixture->cancel;
}

static void movie_admission(void)
{
    unsigned char actor[44] = {1, 0, 3, 3};
    unsigned char little[56] = {
        1, 0, 3, 3, 32, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 255, 255, 255, 255, 1, 'A', 0, 0
    };
    unsigned char big[56] = {
        0, 1, 3, 3, 0, 0, 0, 32, 0, 0, 0, 1, 0, 0, 0, 4, 255, 255, 255, 255, 1, 'B', 0, 0
    };
    const unsigned char root[] = {1, 0, 3, 3, 2, 0, 2, 0};
    const unsigned char scene[] = {1, 0, 3, 3, 9, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0};
    const MMChunkEntry rows[] = {
        {0x41435452UL, 0xffffffffUL, 0, sizeof(actor), 0, 0, 0, 0},
        {0x47535420UL, 5, 44, sizeof(little), 0, 0, 1, 0},
        {0x47535420UL, 6, 100, sizeof(big), 0, 0, 1, 0},
        {MM_MOVIE_CHUNK, 7, 156, sizeof(root), 0, 3, 0, 36},
        {MM_SCENE_CHUNK, 12, 164, sizeof(scene), 0, 0, 1, 0},
        {MM_SCENE_CHUNK, 0xffffffffUL, 180, sizeof(scene), 0, 0, 0, 0}
    };
    const MMChunkChild children[] = {{0x47535420UL, 5, 0}, {0x47535420UL, 6, 0}, {MM_SCENE_CHUNK, 12, 4}};
    const MMMovieActor roll = {20, 1, 2, 0, 0x544d504cUL, 4};
    DirectoryFixture fixture = {0};
    Host host = {0};
    MMHandleServices handles;
    MMBlockFileServices files;
    MMChunkPayloadServices payload = {0};
    MMChunkAccess chunks = {NULL, ADMISSION_ROWS, directory_entry, directory_child};
    const MMArchiveReaderOptions options = {1024, 1024, MM_TEXT_WINDOWS, NULL, NULL, NULL, NULL, NULL};
    MMArchiveMovieOpenSink sink = {{NULL, directory_root, admission_scene, admission_source}, admission_name};
    MMArchiveReader *reader;
    MMArchiveDirectoryError error;
    MMArchiveMovieIds ids = {101, 102, 103, 0, 0};
    services(&host, &handles, &files);
    fixture.file = (File *)create_temp(&host);
    CHECK(fixture.file);
    actor[16] = 10;
    mm_movie_write_actor(&roll, little + 28, 0);
    mm_movie_write_actor(&roll, big + 28, 1);
    CHECK(write_file(&host, fixture.file, rows[0].offset, actor, sizeof(actor)));
    CHECK(write_file(&host, fixture.file, rows[1].offset, little, sizeof(little)));
    CHECK(write_file(&host, fixture.file, rows[2].offset, big, sizeof(big)));
    CHECK(write_file(&host, fixture.file, rows[3].offset, root, sizeof(root)));
    CHECK(write_file(&host, fixture.file, rows[4].offset, scene, sizeof(scene)));
    CHECK(write_file(&host, fixture.file, rows[5].offset, scene, sizeof(scene)));
    memcpy(fixture.rows, rows, sizeof(rows));
    memcpy(fixture.children, children, sizeof(children));
    fixture.count = ADMISSION_ROWS;
    payload.handles = &handles;
    payload.files = &files;
    payload.locate = directory_locate;
    payload.decode_context = &host;
    payload.decode = transform;
    chunks.context = sink.directory.context = &fixture;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    fixture.cancel = 1;
    CHECK(mm_archive_reader_movie_open(reader, &sink, &ids, &error) == MM_ARCHIVE_DIRECTORY_CANCELLED);
    CHECK(fixture.names == 1 && ids.next_actor_id == 103 && !mm_archive_reader_movie(reader));
    fixture.cancel = fixture.names = fixture.scenes = 0;
    CHECK(mm_archive_reader_movie_open(reader, &sink, &ids, &error) == MM_ARCHIVE_DIRECTORY_OK);
    CHECK(fixture.roots == 2 && fixture.scenes == 1 && fixture.sources == 2 && fixture.names == 2);
    CHECK(ids.next_scene == 13 && ids.scene_exhausted && ids.actor_chunk_exhausted && ids.next_actor_id == 21);
    CHECK(mm_archive_reader_movie(reader));
    mm_archive_reader_close(reader);

    fixture.rootless = 1;
    fixture.scenes = fixture.sources = fixture.names = 0;
    memmove(fixture.rows + 3, fixture.rows + 4, 2 * sizeof(*fixture.rows));
    chunks.count = --fixture.count;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    CHECK(mm_archive_reader_movie_open(reader, &sink, &ids, &error) == MM_ARCHIVE_DIRECTORY_OK);
    CHECK(fixture.scenes == 2 && fixture.sources == 1 && !fixture.names && !mm_archive_reader_movie(reader));
    CHECK(ids.next_actor_id == 11 && ids.scene_exhausted && ids.actor_chunk_exhausted);
    mm_archive_reader_close(reader);

    actor[0] = 0;
    CHECK(write_file(&host, fixture.file, rows[0].offset, actor, sizeof(actor)));
    fixture.scenes = 0;
    CHECK(mm_archive_reader_open(&chunks, &fixture, &payload, &options, &reader) == MM_ARCHIVE_READ_OK);
    CHECK(mm_archive_reader_movie_open(reader, &sink, &ids, &error) == MM_ARCHIVE_DIRECTORY_PAYLOAD);
    CHECK(error.position == 0 && error.payload_status == MM_ARCHIVE_INVALID_RECORD && ids.next_actor_id == 11);
    mm_archive_reader_close(reader);
    CHECK(fixture.file->refs == 1 && !host.handles && !host.locks);
    release_file(&host, fixture.file);
    CHECK(!host.files);
}

int main(void)
{
    golden();
    failures();
    coding();
    payloads();
    payload_cache();
    archive_directory();
    movie_admission();
    return 0;
}
