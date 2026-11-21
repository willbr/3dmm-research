#include "mm/script_resource.h"
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
    int row_owner, string_owner, script, wrapped;
    int rows_live, strings_live, adopted_literals, packed, decode_fail, child, missing, bad_width, bad_extra, fail_create;
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
    rows->stride = h->bad_extra ? 4 : 8;
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
static void *wrap(void *v, void *owned)
{
    Host *h = v;
    CHECK((owned == &h->row_owner && h->rows_live == 1) || (owned == &h->string_owner && h->strings_live == 1));
    return h->fail_create ? NULL : &h->wrapped;
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
    h->services.wrap_collection = wrap;
    h->services.release = release;
    h->services.invalidate = invalidate;
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
static void collections(void)
{
    Host h;
    void *out = NULL;
    MMI32 bytes;
    int registry, failed;
    for (registry = 0; registry <= 1; ++registry)
        for (failed = 0; failed <= 3; ++failed)
        {
            init(&h);
            h.packed = h.other = 1;
            h.decoded_bytes = registry ? 4 : 12;
            h.bad_width = h.bad_extra = failed == 1;
            h.fail_create = failed == 2;
            h.decode_fail = failed == 3;
            root(&h);
            CHECK((registry ? mm_script_registry_fetch(&h.block.owner, NULL, &bytes, &h.services)
                            : mm_script_colors_fetch(&h.block.owner, NULL, &bytes, &h.services)) == 1);
            CHECK(bytes == 15 && !h.decodes);
            CHECK((registry ? mm_script_registry_fetch(&h.block.owner, &out, &bytes, &h.services)
                            : mm_script_colors_fetch(&h.block.owner, &out, &bytes, &h.services)) == !failed);
            if (!failed)
            {
                CHECK(out == &h.wrapped);
                CHECK(registry ? (bytes == 4 && !memcmp(h.strings + 4, "DCBA", 4))
                               : (bytes == 12 && !memcmp(h.code, "ABCDEFGHIJKL", 12)));
            }
            else
                CHECK(!h.rows_live && !h.strings_live && h.invalidations == (registry && failed == 3 ? 0 : 2));
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
