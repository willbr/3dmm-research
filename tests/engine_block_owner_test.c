#include "mm/block_owner.h"
#include "mm/movie_session.h"
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
} PayloadArchive;

static int locate_payload(void *context, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    PayloadArchive *archive = (PayloadArchive *)context;
    CHECK(type == 11);
    if (number > 2)
        return 0;
    out->file = number == 1 ? archive->file : NULL;
    out->offset = number == 1 ? 3 : 0;
    out->length = number != 1 ? 0 : archive->packed ? 9 : 4;
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
int main(void)
{
    golden();
    failures();
    coding();
    payloads();
    return 0;
}
