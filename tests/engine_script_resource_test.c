#include "mm/script_resource.h"
#include "mm/text.h"
#include "engine_resource_block_fixture.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "script resource check failed at %d\n", __LINE__); exit(1); } } while (0)
#define SCPT 0x53435054UL
#define GSTX 0x47535458UL

typedef struct Host
{
    ResourceTestBlock block;
    MMScriptResourceServices services;
    MMChunkPayloadServices payloads;
    MMResourceCache cache;
    MMCollectionStorage words;
    unsigned char file[64], code[12], strings[8];
    int row_owner, string_owner, script, collections_live;
    int rows_live, strings_live, adopted_literals, packed, decode_fail, child, missing, bad_width, fail_create;
    int decodes, root_lookups, child_lookups, invalidations, other;
    MMU32 decoded_bytes;
} Host;

static int read_file(void *v, void *file, MMU32 off, void *out, size_t n)
{
    Host *h = v;
    CHECK(file == v && off <= sizeof(h->file) && n <= sizeof(h->file) - off);
    memcpy(out, h->file + off, n);
    return 1;
}
static int write_file(void *v, void *file, MMU32 off, const void *in, size_t n)
{
    (void)v; (void)file; (void)off; (void)in; (void)n;
    return 0;
}
static int locate(void *v, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Host *h = v;
    out->file = v;
    if (type == SCPT && number == 7)
    {
        ++h->root_lookups;
        out->offset = 0;
        out->length = h->packed ? 15 : 12;
        *packed = h->packed;
        return 1;
    }
    CHECK(type == GSTX && number == 3);
    ++h->child_lookups;
    out->offset = 32;
    out->length = 4;
    *packed = 0;
    return !h->missing;
}
static int entry(void *v, MMU32 row, MMChunkEntry *out)
{
    Host *h = v;
    memset(out, 0, sizeof(*out));
    out->type = row ? SCPT : GSTX;
    out->number = row ? 7 : 3;
    out->child_count = row && h->child;
    return row < 2;
}
static int child(void *v, MMU32 parent, MMU32 row, MMChunkChild *out)
{
    Host *h = v;
    CHECK(parent == 1 && row == 0);
    out->type = h->child == 2 ? GSTX + 1 : GSTX;
    out->number = 3;
    out->slot = 0;
    return 1;
}
static MMChunkAccess chunks(void *v)
{
    MMChunkAccess access;
    access.context = v;
    access.count = 2;
    access.entry = entry;
    access.child = child;
    return access;
}
static int decode(void *v, void **handle)
{
    Host *h = v;
    CHECK(resource_handle_size(NULL, *handle) == 15);
    ++h->decodes;
    if (h->decode_fail)
        return 0;
    resource_block_decode(handle, h->file, h->decoded_bytes);
    return 1;
}
static void *read_rows(void *v, const MMReadStream *stream, MMArray *rows, int *other)
{
    Host *h = v;
    CHECK(stream->length == 12 && mm_read_exact(stream, 0, h->code, sizeof(h->code)));
    memset(rows, 0, sizeof(*rows));
    rows->storage = &h->words;
    rows->width = h->bad_width ? 6 : 4;
    rows->count = 12 / rows->width;
    h->words.buffers[0].data = h->code;
    *other = h->other;
    ++h->rows_live;
    return &h->row_owner;
}
static void *read_strings(void *v, const MMReadStream *stream, MMStringTable *rows, int *other)
{
    Host *h = v;
    CHECK(stream->length == 4 && mm_read_exact(stream, 0, h->strings + 4, 4));
    memset(rows, 0, sizeof(*rows));
    rows->entries = h->strings;
    rows->stride = 8;
    rows->count = 1;
    *other = h->other;
    ++h->strings_live;
    return &h->string_owner;
}
static void *new_script(void *v, void *rows, void *strings)
{
    Host *h = v;
    CHECK(rows == &h->row_owner && h->rows_live == 1);
    CHECK(!strings || (strings == &h->string_owner && h->strings_live == 1));
    h->adopted_literals = strings != NULL;
    CHECK(!memcmp(h->code, h->file, sizeof(h->code)));
    return h->fail_create ? NULL : &h->script;
}
static void release(void *v, void *owned)
{
    Host *h = v;
    if (owned == &h->string_owner)
        CHECK(h->strings_live-- == 1);
    else
        CHECK(owned == &h->row_owner && h->rows_live-- == 1);
}
static void invalidate(void *v, void *out, size_t n)
{
    Host *h = v;
    CHECK(out && n);
    ++h->invalidations;
}
static void init(Host *h)
{
    memset(h, 0, sizeof(*h));
    resource_block_init(&h->block, h, read_file, write_file);
    memcpy(h->file, "ABCDEFGHIJKL", 12);
    memcpy(h->file + 32, "str!", 4);
    h->decoded_bytes = 12;
    h->services.context = h;
    h->services.decode = decode;
    h->services.read_rows = read_rows;
    h->services.read_strings = read_strings;
    h->services.create_script = new_script;
    h->services.release = release;
    h->payloads.files = h->block.owner.files;
    h->payloads.handles = h->block.owner.handles;
    h->payloads.locate = locate;
    h->payloads.decode = decode;
    h->payloads.decode_context = h;
    h->cache.file = h;
    h->cache.services.live_chunks = chunks;
    h->cache.services.locate = locate;
    h->cache.services.block_files = h->payloads.files;
    h->cache.services.handles = h->payloads.handles;
}
static void root(Host *h)
{
    MMBlockFile file;
    file.file = h;
    file.offset = 0;
    file.length = h->packed ? 15 : 12;
    CHECK(mm_owned_block_set_file(&h->block.owner, &file, h->packed) == MM_DATA_OK);
}

static void scripts(void)
{
    Host h;
    MMChunkAccess access;
    void *out;
    MMI32 bytes;
    int mode;
    for (mode = 0; mode < 6; ++mode)
    {
        init(&h);
        h.child = mode == 2 ? 2 : 1;
        h.packed = h.other = 1;
        h.missing = mode == 3;
        h.bad_width = mode == 4;
        h.fail_create = mode == 5;
        if (mode == 0)
        {
            access = chunks(&h);
            out = mm_script_resource_archive(&access, &h.payloads, &h, SCPT, 7, &h.services);
            CHECK(h.root_lookups == 1);
        }
        else
        {
            root(&h);
            CHECK(mm_script_resource_fetch(&h.cache, SCPT, 7, &h.block.owner, NULL, &bytes, &h.services));
            CHECK(bytes == 15 && !h.decodes && !h.root_lookups && !h.child_lookups);
            CHECK(mm_script_resource_fetch(&h.cache, SCPT, 7, &h.block.owner, &out, &bytes, &h.services) == (mode < 3));
            CHECK(bytes == 15 && !h.root_lookups);
        }
        CHECK(h.decodes == 1 && h.child_lookups == (mode != 2 && mode != 4));
        if (mode < 3)
        {
            CHECK(out == &h.script && h.rows_live == 1 && h.adopted_literals == (mode != 2));
            CHECK(!memcmp(h.code, "DCBAHGFELKJI", 12));
            if (mode != 2)
                CHECK(h.strings_live == 1 && !memcmp(h.strings + 4, "str!", 4));
        }
        else
            CHECK(!out && !h.rows_live && !h.strings_live);
        CHECK(mm_owned_block_clear(&h.block.owner) == MM_DATA_OK);
    }
}
static void *allocate_collection(void *v, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = v;
    CHECK(!old && !old_bytes && priority == MM_STORAGE_NORMAL);
    if (h->fail_create)
        return NULL;
    ++h->collections_live;
    return malloc(bytes);
}

static void release_collection(void *v, void *p, MMU32 bytes)
{
    Host *h = v;
    CHECK(h->collections_live-- > 0 && bytes != 0);
    free(p);
}

static void word(unsigned char *p, MMU32 value, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}

static void collection_wire(Host *h, int registry, int big, int bad_width)
{
    MMCollectionHeader header;
    unsigned char *data;
    memset(&header, 0, sizeof(header));
    header.kind = registry ? MM_COLLECTION_STRINGS : MM_COLLECTION_ARRAY;
    header.big_endian = big;
    header.system = MM_TEXT_WINDOWS;
    header.width = registry != bad_width ? 8 : 4;
    header.count = 2;
    header.bytes = registry ? 8 : 0;
    header.free_count = -1;
    CHECK(mm_collection_layout(&header) == MM_STORAGE_OK);
    CHECK(mm_collection_header_write(&header, h->file, sizeof(h->file)) == MM_STORAGE_OK);
    h->decoded_bytes = header.header_bytes + header.first_bytes + header.second_bytes;
    data = h->file + header.header_bytes;
    if (registry)
    {
        memcpy(data, "\003cat\003dog", 8);
        word(data + 8, 0, big);
        word(data + 12, 73, big);
        word(data + 16, 4, big);
        word(data + 20, 0xffffffefUL, big);
    }
    else
        memcpy(data, "abcdefgh", 8);
}

typedef struct MergeTrace
{
    int calls, wide;
} MergeTrace;

static int put_string(void *context, MMI32 id, const void *text, MMU32 count)
{
    MergeTrace *trace = context;
    const char *expected = trace->calls ? "cat" : "dog";
    unsigned short wide[3];
    CHECK(count == 3 && trace->calls < 2 && id == (trace->calls ? 73 : -17));
    if (trace->wide)
    {
        memcpy(wide, text, sizeof(wide));
        CHECK(wide[0] == expected[0] && wide[1] == expected[1] && wide[2] == expected[2]);
    }
    else
        CHECK(!memcmp(text, expected, 3));
    return trace->calls++ != 0;
}

static size_t widen(void *context, const void *text, size_t bytes, int encoding, void *out, size_t capacity)
{
    const unsigned char *source = text;
    unsigned short *target = out;
    size_t i;
    (void)context;
    CHECK(encoding == MM_TEXT_WINDOWS && bytes <= capacity);
    for (i = 0; i < bytes; ++i)
        target[i] = source[i];
    return bytes;
}

static void collections(void)
{
    Host h;
    MMScriptCollectionHost host;
    MMStorageAllocator alloc;
    void *out;
    MMI32 bytes;
    int registry, big, mode, success;
    MergeTrace trace;
    MMU32 count;
    for (registry = 0; registry <= 1; ++registry)
        for (big = 0; big <= 1; ++big)
            for (mode = 0; mode < (registry ? 6 : 5); ++mode)
            {
                init(&h);
                h.packed = 1;
                success = mode == 0 || mode == 5;
                collection_wire(&h, registry, big, mode == 1);
                h.fail_create = mode == 2;
                h.decode_fail = mode == 3;
                if (mode == 4)
                    --h.decoded_bytes;
                memset(&host, 0, sizeof(host));
                alloc.context = &h;
                alloc.resize = allocate_collection;
                alloc.release = release_collection;
                host.context = &h;
                host.decode = decode;
                host.objects = &alloc;
                trace.wide = mode == 5;
                host.encoding = trace.wide ? MM_TEXT_UTF16_LE : MM_TEXT_WINDOWS;
                host.unit_bytes = trace.wide ? 2 : 1;
                host.convert = widen;
                host.invalidate = invalidate;
                root(&h);
                CHECK((registry ? mm_script_registry_fetch(&h.block.owner, NULL, &bytes, &host)
                                : mm_script_colors_fetch(&h.block.owner, NULL, &bytes, &host)) == 1);
                CHECK(bytes == 15 && !h.decodes);
                out = NULL;
                CHECK((registry ? mm_script_registry_fetch(&h.block.owner, &out, &bytes, &host)
                                : mm_script_colors_fetch(&h.block.owner, &out, &bytes, &host)) == success);
                if (success)
                {
                    CHECK(out && bytes == (MMI32)h.decoded_bytes && h.collections_live == 1);
                    if (registry)
                    {
                        trace.calls = 0;
                        CHECK(!mm_script_registry_merge(out, put_string, &trace) && trace.calls == 2);
                    }
                    else
                    {
                        const void *rows = mm_script_color_rows(out, &count);
                        CHECK(count == 2 && !memcmp(rows, "abcdefgh", 8));
                    }
                    mm_script_collection_priority(out, 1);
                    mm_script_collection_release(out);
                }
                else
                    CHECK(!out && h.invalidations == (registry && mode == 3 ? 0 : 2));
                CHECK(!h.collections_live);
                CHECK(mm_owned_block_clear(&h.block.owner) == MM_DATA_OK);
            }
}
int main(void)
{
    scripts();
    collections();
    puts("C89 script, literal, registry and colour resource reads passed");
    return 0;
}
