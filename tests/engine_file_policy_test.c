#include "three_mm/file_text.h"
#include "three_mm/piece_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "file policy check %d: %s\n", __LINE__, #x);                                               \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Host
{
    int files, handles, buffers, locks, fail_speed, fail_normal, fail_temp, fail_read, fail_write, skip, mismatch;
    unsigned reads, writes, temps, queries, conversions, speed, normal, max_read;
    ThreeMMPieceTable *pinned;
    ThreeMMOwnedBlock *target;
    int wide;
} Host;
typedef struct File
{
    unsigned refs;
    ThreeMMU32 size;
    unsigned char bytes[16384];
} File;
typedef struct Handle
{
    ThreeMMU32 size;
    unsigned locks;
    unsigned char bytes[1];
} Handle;
static void pinned(Host *h)
{
    if (h->pinned)
        CHECK(h->pinned->storage.buffers[0].locks == 1 && h->pinned->storage.buffers[1].locks == 1);
}
static void *buffer_resize(void *context, void *old, ThreeMMU32 old_size, ThreeMMU32 n, int priority)
{
    Host *h = (Host *)context;
    void *p;
    if (priority == THREE_MM_STORAGE_SPEED)
    {
        ++h->speed;
        if (h->fail_speed)
            return NULL;
    }
    else
    {
        CHECK(priority == THREE_MM_STORAGE_NORMAL);
        ++h->normal;
        if (h->fail_normal)
            return NULL;
    }
    p = malloc(n);
    CHECK(p);
    if (old)
    {
        memcpy(p, old, n < old_size ? n : old_size);
        free(old);
    }
    else
        ++h->buffers;
    return p;
}
static void buffer_release(void *context, void *p, ThreeMMU32 n)
{
    Host *h = (Host *)context;
    (void)n;
    --h->buffers;
    free(p);
}
static void *temp(void *context)
{
    Host *h = (Host *)context;
    File *f;
    ++h->temps;
    if (h->fail_temp)
        return NULL;
    f = (File *)malloc(sizeof(*f));
    CHECK(f);
    f->refs = 1;
    f->size = 0;
    ++h->files;
    return f;
}
static void retain(void *context, void *file)
{
    (void)context;
    CHECK(((File *)file)->refs);
    ++((File *)file)->refs;
}
static void release(void *context, void *file)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    CHECK(f->refs);
    if (!--f->refs)
    {
        --h->files;
        free(f);
    }
}
static int read_file(void *context, void *file, ThreeMMU32 offset, void *output, size_t n)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    pinned(h);
    ++h->reads;
    if (n > h->max_read)
        h->max_read = (unsigned)n;
    if (h->fail_read && h->reads == (unsigned)h->fail_read)
        return 0;
    CHECK(three_mm_range_contains(f->size, offset, (ThreeMMU32)n));
    memcpy(output, f->bytes + offset, n);
    return 1;
}
static int write_file(void *context, void *file, ThreeMMU32 offset, const void *input, size_t n)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    pinned(h);
    ++h->writes;
    if (h->fail_write && h->writes == (unsigned)h->fail_write)
        return 0;
    if (h->target)
        CHECK(offset >= h->target->view.start &&
              three_mm_range_contains(h->target->view.length, offset - h->target->view.start, (ThreeMMU32)n));
    CHECK(three_mm_range_contains(sizeof(f->bytes), offset, (ThreeMMU32)n));
    memcpy(f->bytes + offset, input, n);
    if (offset + n > f->size)
        f->size = offset + (ThreeMMU32)n;
    return 1;
}
static int handle_allocate(void *context, ThreeMMU32 n, void **out)
{
    Host *h = (Host *)context;
    Handle *p = (Handle *)malloc(sizeof(*p) + n);
    CHECK(p);
    p->size = n;
    p->locks = 0;
    ++h->handles;
    *out = p;
    return 1;
}
static void handle_release(void *context, void *p)
{
    Host *h = (Host *)context;
    CHECK(!((Handle *)p)->locks);
    --h->handles;
    free(p);
}
static ThreeMMU32 handle_size(void *context, void *p)
{
    (void)context;
    return ((Handle *)p)->size;
}
static void *handle_lock(void *context, void *p)
{
    Host *h = (Host *)context;
    pinned(h);
    ++h->locks;
    ++((Handle *)p)->locks;
    return ((Handle *)p)->bytes;
}
static void handle_unlock(void *context, void *p)
{
    Host *h = (Host *)context;
    CHECK(h->locks && ((Handle *)p)->locks);
    --h->locks;
    --((Handle *)p)->locks;
}
static int handle_resize(void *context, void **p, ThreeMMU32 n)
{
    (void)context;
    (void)p;
    (void)n;
    CHECK(0);
    return 0;
}
static void services(Host *h, ThreeMMBlockFileServices *f, ThreeMMHandleServices *m, ThreeMMStorageAllocator *a)
{
    f->context = h;
    f->retain = retain;
    f->release = release;
    f->create_temp = temp;
    f->read = read_file;
    f->write = write_file;
    m->context = h;
    m->allocate = handle_allocate;
    m->release = handle_release;
    m->size = handle_size;
    m->lock = handle_lock;
    m->unlock = handle_unlock;
    m->resize = handle_resize;
    a->context = h;
    a->resize = buffer_resize;
    a->release = buffer_release;
}
static ThreeMMI32 convert(void *context, const void *input, ThreeMMU32 n, int source, void *output, ThreeMMU32 capacity)
{
    Host *h = (Host *)context;
    size_t result;
    if (!output)
    {
        ++h->queries;
        if (h->skip)
            return 0;
    }
    else
        ++h->conversions;
    result = h->wide ? three_mm_text_utf16(NULL, input, n, source, (unsigned short *)output, capacity)
                     : three_mm_text_ansi(NULL, input, n, source, (char *)output, capacity);
    return (ThreeMMI32)result - (output && h->mismatch ? 1 : 0);
}
static ThreeMMFileTextConverter converter(Host *h)
{
    ThreeMMFileTextConverter c;
    memset(&c, 0, sizeof(c));
    c.context = h;
    c.current_encoding = THREE_MM_TEXT_WINDOWS;
    c.default_byte_encoding = THREE_MM_TEXT_WINDOWS;
    c.native_unicode_encoding = THREE_MM_TEXT_UTF16_LE;
    c.output_unit = 1;
    c.convert = convert;
    return c;
}
static ThreeMMU32 hash = 2166136261UL;
static void byte_hash(unsigned char n)
{
    hash = (hash ^ n) * 16777619UL;
}
static void word_hash(ThreeMMU32 n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        byte_hash((unsigned char)(n >> (8 * i)));
}
static File *input_file(Host *h, const unsigned char *bytes, ThreeMMU32 n, ThreeMMBlockFile *region)
{
    File *f = (File *)temp(h);
    CHECK(f);
    memcpy(f->bytes, "prefix-", 7);
    memcpy(f->bytes + 7, bytes, n);
    f->size = 7 + n;
    region->file = f;
    region->offset = 7;
    region->length = n;
    retain(h, f);
    return f;
}
static void text_golden(void)
{
    static const ThreeMMU32 lengths[] = {0, 1, 2, 127, 256, 257, 768, 1025};
    static const int requested[] = {0, THREE_MM_TEXT_MAC, THREE_MM_TEXT_WINDOWS, THREE_MM_TEXT_UTF16_BE,
                                    THREE_MM_TEXT_UTF16_LE};
    Host h;
    ThreeMMBlockFileServices f;
    ThreeMMHandleServices m;
    ThreeMMStorageAllocator a;
    ThreeMMFileTextConverter c;
    ThreeMMBlockFile region;
    File *original, *output;
    unsigned char bytes[8192], value;
    ThreeMMU32 n, i;
    unsigned kind, s, r;
    for (kind = 0; kind < 6; ++kind)
        for (s = 0; s < 8; ++s)
            for (r = 0; r < 5; ++r)
            {
                memset(&h, 0, sizeof(h));
                services(&h, &f, &m, &a);
                c = converter(&h);
                n = 0;
                if (kind == 2 || kind == 3 || kind == 5)
                {
                    bytes[n++] = kind == 3 ? 0xfe : 0xff;
                    bytes[n++] = kind == 3 ? 0xff : 0xfe;
                }
                for (i = 0; i < lengths[s]; ++i)
                {
                    value = (unsigned char)(i % 7 == 0 ? (kind == 1 ? 0x8e : 0xe9) : 'A' + i % 26);
                    if (kind < 2)
                        bytes[n++] = value;
                    else if (kind == 3)
                    {
                        bytes[n++] = 0;
                        bytes[n++] = value;
                    }
                    else
                    {
                        bytes[n++] = value;
                        bytes[n++] = 0;
                    }
                }
                if (kind == 5)
                    bytes[n++] = 0x7f;
                original = input_file(&h, bytes, n, &region);
                CHECK(three_mm_file_text_translate(&region, &f, &c, &a, requested[r]) == THREE_MM_DATA_OK);
                output = (File *)region.file;
                CHECK(!memcmp(original->bytes + 7, bytes, n));
                word_hash(kind);
                word_hash(lengths[s]);
                word_hash((ThreeMMU32)requested[r]);
                word_hash(region.offset);
                word_hash(region.length);
                word_hash(region.file == original);
                word_hash(output->size);
                for (i = 0; i < region.length; ++i)
                    byte_hash(output->bytes[region.offset + i]);
                release(&h, region.file);
                release(&h, original);
                CHECK(!h.files && !h.buffers && !h.handles && !h.locks);
            }
}
static int piece_read(void *context, const ThreeMMFilePiece *p, void *output)
{
    return read_file(context, p->file, p->offset, output, p->length);
}
static int piece_write(void *context, const ThreeMMFilePiece *p, const void *input)
{
    return write_file(context, p->file, p->offset, input, p->length);
}
static int piece_copy(void *context, const ThreeMMFilePiece *source, const ThreeMMFilePiece *destination)
{
    return write_file(context, destination->file, destination->offset, ((File *)source->file)->bytes + source->offset,
                      source->length);
}
static void make_table(Host *h, ThreeMMPieceTable *t, const ThreeMMStorageAllocator *a)
{
    ThreeMMPieceFiles f;
    ThreeMMFilePiece p;
    File *file;
    unsigned i;
    f.context = h;
    f.retain = retain;
    f.release = release;
    f.create_temp = temp;
    f.read = piece_read;
    f.write = piece_write;
    f.copy = piece_copy;
    CHECK(three_mm_piece_table_init(t, a, &f, 1) == THREE_MM_DATA_OK);
    file = (File *)temp(h);
    file->size = 4096;
    for (i = 0; i < 4096; ++i)
        file->bytes[i] = (unsigned char)(i * 11);
    p.file = file;
    p.offset = 0;
    p.length = 4096;
    CHECK(three_mm_piece_table_replace(t, 0, 0, "front", 5) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace_file(t, 5, 0, &p, 0) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace(t, t->length, 0, "endings", 7) == THREE_MM_DATA_OK);
    release(h, file);
}
static void write_cases(void)
{
    static const ThreeMMU32 ranges[][2] = {{0, 4108},  {2, 4106}, {5, 4096}, {1, 3},
                                           {12, 1024}, {0, 5},    {4101, 7}, {3, 4098}};
    Host h;
    ThreeMMBlockFileServices f;
    ThreeMMHandleServices m;
    ThreeMMStorageAllocator a;
    ThreeMMPieceTable t;
    ThreeMMOwnedBlock target;
    ThreeMMBlockView saved;
    ThreeMMBlockIO io;
    unsigned char model[4108], seed[8192], actual[8192];
    unsigned r, force, i, mode;
    int result;
    memset(&h, 0, sizeof(h));
    services(&h, &f, &m, &a);
    make_table(&h, &t, &a);
    CHECK(three_mm_piece_table_read(&t, 0, model, sizeof(model)) == THREE_MM_DATA_OK);
    for (r = 0; r < 8; ++r)
        for (force = 0; force < 2; ++force)
        {
            CHECK(three_mm_owned_block_init(&target, &m, &f) == THREE_MM_DATA_OK);
            CHECK(three_mm_owned_block_set_temp(&target, ranges[r][1] + 17, force) == THREE_MM_DATA_OK);
            memset(seed, 0xcc, ranges[r][1] + 17);
            CHECK(three_mm_owned_block_io(&target, &io) == THREE_MM_DATA_OK);
            CHECK(three_mm_block_write(&io, 0, seed, ranges[r][1] + 17, 1) == THREE_MM_DATA_OK);
            CHECK(three_mm_block_move_min(&target.view, 11) == THREE_MM_DATA_OK &&
                  three_mm_block_move_lim(&target.view, -6) == THREE_MM_DATA_OK);
            saved = target.view;
            h.pinned = &t;
            h.target = &target;
            CHECK(three_mm_piece_table_write(&t, ranges[r][0], &target, &a) == THREE_MM_DATA_OK);
            h.pinned = NULL;
            h.target = NULL;
            CHECK(!memcmp(&saved, &target.view, sizeof(saved)) && !t.storage.buffers[0].locks &&
                  !t.storage.buffers[1].locks && !h.locks);
            CHECK(three_mm_block_move_min(&target.view, -11) == THREE_MM_DATA_OK &&
                  three_mm_block_move_lim(&target.view, 6) == THREE_MM_DATA_OK);
            CHECK(three_mm_owned_block_io(&target, &io) == THREE_MM_DATA_OK &&
                  three_mm_block_read(&io, 0, actual, target.view.length, 1) == THREE_MM_DATA_OK);
            CHECK(!memcmp(actual, seed, 11) && !memcmp(actual + 11, model + ranges[r][0], ranges[r][1]) &&
                  !memcmp(actual + 11 + ranges[r][1], seed, 6));
            word_hash(ranges[r][0]);
            word_hash(ranges[r][1]);
            word_hash(force);
            word_hash(target.view.length);
            for (i = 0; i < target.view.length; ++i)
                byte_hash(actual[i]);
            CHECK(three_mm_owned_block_clear(&target) == THREE_MM_DATA_OK);
        }
    /* Full native before/after snapshot combines text and supported windows. */
    CHECK(hash == 0xedc4d066UL);
    for (mode = 0; mode < 5; ++mode)
    {
        CHECK(three_mm_owned_block_init(&target, &m, &f) == THREE_MM_DATA_OK &&
              three_mm_owned_block_set_temp(&target, 4125, 1) == THREE_MM_DATA_OK);
        memset(seed, 0xcc, 4125);
        CHECK(three_mm_owned_block_io(&target, &io) == THREE_MM_DATA_OK &&
              three_mm_block_write(&io, 0, seed, 4125, 1) == THREE_MM_DATA_OK);
        CHECK(three_mm_block_move_min(&target.view, 11) == THREE_MM_DATA_OK &&
              three_mm_block_move_lim(&target.view, -6) == THREE_MM_DATA_OK);
        if (mode == 2)
            target.view.length = 7; /* Legacy final-piece overshoot. */
        if (mode == 3)
            target.view.packed = 1;
        saved = target.view;
        h.reads = h.writes = 0;
        h.fail_read = mode == 0 ? 1 : 0;
        h.fail_write = mode == 1 ? 2 : 0;
        h.fail_speed = mode == 4;
        h.pinned = &t;
        h.target = &target;
        result = three_mm_piece_table_write(&t, 0, &target, &a);
        h.pinned = NULL;
        h.target = NULL;
        CHECK(result == (mode == 4   ? THREE_MM_DATA_OK
                         : mode == 3 ? THREE_MM_DATA_PACKED
                         : mode == 2 ? THREE_MM_DATA_ARGUMENT
                                     : THREE_MM_DATA_IO));
        CHECK(!memcmp(&saved, &target.view, sizeof(saved)) && !h.locks && !t.storage.buffers[0].locks &&
              !t.storage.buffers[1].locks);
        if (mode == 2 || mode == 3)
            CHECK(!h.reads && !h.writes);
        h.fail_read = h.fail_write = h.fail_speed = 0;
        CHECK(three_mm_owned_block_clear(&target) == THREE_MM_DATA_OK);
    }
    CHECK(three_mm_piece_table_dispose(&t) == THREE_MM_DATA_OK && !h.files && !h.handles && !h.buffers);
}
static void text_failures(void)
{
    Host h;
    ThreeMMBlockFileServices f;
    ThreeMMHandleServices m;
    ThreeMMStorageAllocator a;
    ThreeMMFileTextConverter c;
    ThreeMMBlockFile region;
    File *original;
    unsigned char bytes[4096];
    unsigned i, mode;
    int result;
    for (mode = 0; mode < 6; ++mode)
    {
        memset(&h, 0, sizeof(h));
        services(&h, &f, &m, &a);
        c = converter(&h);
        bytes[0] = 0xff;
        bytes[1] = 0xfe;
        for (i = 2; i < 1202; i += 2)
        {
            bytes[i] = 'A';
            bytes[i + 1] = 0;
        }
        original = input_file(&h, bytes, 1202, &region);
        h.fail_temp = mode == 0;
        h.fail_read = mode == 1 ? 1 : mode == 2 ? 2 : 0;
        h.fail_write = mode == 3 ? 1 : 0;
        h.mismatch = mode == 4;
        h.skip = mode == 5;
        result = three_mm_file_text_translate(&region, &f, &c, &a, 0);
        CHECK(result == (mode == 5   ? THREE_MM_DATA_OK
                         : mode == 4 ? THREE_MM_DATA_WRONG_SIZE
                         : mode == 0 ? THREE_MM_DATA_MEMORY
                                     : THREE_MM_DATA_IO));
        CHECK(!h.buffers && !h.locks);
        if (mode == 5)
            CHECK(region.file != original && region.length == 0);
        else
            CHECK(region.file == original && region.offset == (mode == 1 ? 7u : 9u) &&
                  region.length == (mode == 1 ? 1202u : 1200u));
        release(&h, region.file);
        release(&h, original);
        CHECK(!h.files);
    }
    memset(&h, 0, sizeof(h));
    services(&h, &f, &m, &a);
    c = converter(&h);
    memset(bytes, 0x8e, 4096);
    original = input_file(&h, bytes, 4096, &region);
    h.fail_speed = 1;
    CHECK(three_mm_file_text_translate(&region, &f, &c, &a, THREE_MM_TEXT_MAC) == THREE_MM_DATA_OK);
    CHECK(h.max_read <= 512 && h.queries == 8 && h.conversions == 8 && region.length == 4096);
    for (i = 0; i < region.length; ++i)
        CHECK(((File *)region.file)->bytes[i] == 0xe9);
    release(&h, region.file);
    release(&h, original);
    CHECK(!h.files && !h.buffers);
    for (mode = 0; mode < 2; ++mode)
    {
        memset(&h, 0, sizeof(h));
        services(&h, &f, &m, &a);
        c = converter(&h);
        c.current_encoding = THREE_MM_TEXT_UTF16_LE;
        c.output_unit = 2;
        h.wide = 1;
        h.fail_normal = mode != 0;
        memset(bytes, 0x8e, 600);
        original = input_file(&h, bytes, 600, &region);
        result = three_mm_file_text_translate(&region, &f, &c, &a, THREE_MM_TEXT_MAC);
        CHECK(result == (mode ? THREE_MM_DATA_MEMORY : THREE_MM_DATA_OK) && !h.buffers && h.speed == 1 &&
              h.normal == 1);
        if (!mode)
        {
            unsigned short first;
            memcpy(&first, ((File *)region.file)->bytes, 2);
            CHECK(first == 0xe9 && region.length == 1200);
        }
        else
            CHECK(region.file == original && region.offset == 7 && region.length == 600);
        release(&h, region.file);
        release(&h, original);
        CHECK(!h.files);
    }
    /* Explicit big-endian signature and same-encoding BOM-only range changes. */
    memset(&h, 0, sizeof(h));
    services(&h, &f, &m, &a);
    c = converter(&h);
    c.native_big_endian = 1;
    c.native_unicode_encoding = THREE_MM_TEXT_UTF16_BE;
    bytes[0] = 0xfe;
    bytes[1] = 0xff;
    bytes[2] = 0;
    bytes[3] = 'A';
    original = input_file(&h, bytes, 4, &region);
    CHECK(three_mm_file_text_translate(&region, &f, &c, &a, THREE_MM_TEXT_WINDOWS) == THREE_MM_DATA_OK &&
          region.length == 1 && ((File *)region.file)->bytes[0] == 'A');
    release(&h, region.file);
    release(&h, original);
    memset(&h, 0, sizeof(h));
    services(&h, &f, &m, &a);
    c = converter(&h);
    c.current_encoding = THREE_MM_TEXT_UTF16_LE;
    c.output_unit = 2;
    bytes[0] = 0xff;
    bytes[1] = 0xfe;
    bytes[2] = 'A';
    bytes[3] = 0;
    original = input_file(&h, bytes, 4, &region);
    i = h.temps;
    CHECK(three_mm_file_text_translate(&region, &f, &c, &a, 0) == THREE_MM_DATA_OK && region.file == original &&
          region.offset == 9 && region.length == 2 && h.temps == i && !h.queries);
    release(&h, region.file);
    release(&h, original);
    CHECK(!h.files && !h.buffers);
}
int main(void)
{
    text_golden();
    write_cases();
    text_failures();
    return 0;
}
