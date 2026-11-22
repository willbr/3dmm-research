#include "mm/script_resource.h"
#include "mm/script_runtime.h"
#include "mm/text.h"
#include "engine_resource_block_fixture.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "script resource check failed at %d\n", __LINE__); exit(1); } } while (0)
#define SCPT 0x53435054UL
#define GSTX 0x47535458UL

#define LITERAL_START 64

typedef struct Host
{
    ResourceTestBlock block;
    MMScriptHost host;
    MMStorageAllocator allocator;
    MMChunkPayloadServices payloads;
    MMResourceCache cache;
    unsigned char file[128];
    int collections_live, packed, decode_fail, child, missing, fail_create;
    int decodes, root_lookups, child_lookups, invalidations;
    MMU32 decoded_bytes, literal_bytes;
} Host;

static void *allocate_collection(void *, void *, MMU32, MMU32, int);
static void release_collection(void *, void *, MMU32);

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
        out->length = h->packed ? 15 : h->decoded_bytes;
        *packed = h->packed;
        return 1;
    }
    CHECK(type == GSTX && number == 3);
    ++h->child_lookups;
    out->offset = LITERAL_START;
    out->length = h->literal_bytes;
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
    h->allocator.context = h;
    h->allocator.resize = allocate_collection;
    h->allocator.release = release_collection;
    h->host.context = h;
    h->host.decode = decode;
    h->host.objects = &h->allocator;
    h->host.encoding = MM_TEXT_WINDOWS;
    h->host.unit_bytes = 1;
    h->host.invalidate = invalidate;
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
    file.length = h->packed ? 15 : h->decoded_bytes;
    CHECK(mm_owned_block_set_file(&h->block.owner, &file, h->packed) == MM_DATA_OK);
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

static void script_wire(Host *h, int big, int bad_width, int empty)
{
    MMCollectionHeader header;
    unsigned char *data;
    memset(&header, 0, sizeof(header));
    header.kind = MM_COLLECTION_ARRAY;
    header.big_endian = big;
    header.system = MM_TEXT_WINDOWS;
    header.width = bad_width ? 8 : 4;
    header.count = 3;
    header.free_count = -1;
    CHECK(mm_collection_layout(&header) == MM_STORAGE_OK);
    CHECK(mm_collection_header_write(&header, h->file, sizeof(h->file)) == MM_STORAGE_OK);
    h->decoded_bytes = header.header_bytes + header.first_bytes;
    data = h->file + header.header_bytes;
    word(data, 0x11223344UL, big);
    word(data + 4, 0xfffffffeUL, big);
    word(data + 8, 0x01234567UL, big);

    header.kind = MM_COLLECTION_STRINGS;
    header.width = 8;
    header.count = empty ? 0 : 1;
    header.bytes = empty ? 0 : 5;
    CHECK(mm_collection_layout(&header) == MM_STORAGE_OK);
    CHECK(mm_collection_header_write(&header, h->file + LITERAL_START, sizeof(h->file) - LITERAL_START) ==
          MM_STORAGE_OK);
    h->literal_bytes = header.header_bytes + header.first_bytes + header.second_bytes;
    if (!empty)
    {
        data = h->file + LITERAL_START + header.header_bytes;
        memcpy(data, "\004text", 5);
        word(data + 5, 0, big);
        memcpy(data + 9, "id!?", 4);
    }
}

static int write_section(void *context, MMU32 offset, const void *bytes, size_t count)
{
    unsigned char *buffer = context;
    memcpy(buffer + offset, bytes, count);
    return 1;
}

static void scripts(void)
{
    Host h;
    MMChunkAccess access;
    MMCompiledScript *script;
    void *out;
    const void *literal;
    unsigned char saved[48];
    MMStorageWriter writer;
    MMU32 count;
    MMI32 bytes;
    int mode;
    for (mode = 0; mode < 8; ++mode)
    {
        int success = mode < 3 || mode == 7;
        init(&h);
        h.child = mode == 2 ? 2 : 1;
        h.packed = 1;
        h.missing = mode == 3;
        h.fail_create = mode == 5;
        script_wire(&h, mode % 2 == 0, mode == 4, mode == 7);
        if (mode == 6)
            --h.literal_bytes;
        if (mode == 0)
        {
            access = chunks(&h);
            script = mm_script_resource_archive(&access, &h.payloads, &h, SCPT, 7, &h.host);
            CHECK(h.root_lookups == 1);
        }
        else
        {
            root(&h);
            CHECK(mm_script_resource_fetch(&h.cache, SCPT, 7, &h.block.owner, NULL, &bytes, &h.host));
            CHECK(bytes == 15 && !h.decodes && !h.root_lookups && !h.child_lookups);
            CHECK(mm_script_resource_fetch(&h.cache, SCPT, 7, &h.block.owner, &out, &bytes, &h.host) == success);
            CHECK(bytes == 15 && !h.root_lookups);
            script = out;
        }
        CHECK(h.decodes == 1 && h.child_lookups == (mode != 2 && mode != 4 && mode != 5));
        if (success)
        {
            CHECK(script && h.collections_live == 1 && mm_script_word_count(script) == 3);
            CHECK(mm_script_word(script, 0) == 0x11223344L && mm_script_word(script, 1) == -2 &&
                  mm_script_word(script, 2) == 0x01234567L);
            CHECK(mm_script_has_literals(script) == (mode != 2));
            CHECK(mm_script_literal_count(script) == (MMU32)(mode < 2));
            if (mode < 2)
            {
                CHECK(mm_script_literal(script, 0, &literal, &count) && count == 4 && !memcmp(literal, "text", 4));
                writer.context = saved;
                writer.length = mm_script_section_size(script, 1);
                writer.write_at = write_section;
                CHECK(writer.length == h.literal_bytes && writer.length <= sizeof(saved));
                CHECK(mm_script_write_section(script, 1, &writer) &&
                      !memcmp(saved + writer.length - 4, "id!?", 4));
            }
            else
            {
                CHECK(!mm_script_literal(script, 0, &literal, &count));
                CHECK(mm_script_section_size(script, 1) == (mode == 7 ? h.literal_bytes : 0));
            }
            mm_script_retain(script);
            mm_script_release(script);
            CHECK(h.collections_live == 1);
            mm_script_release(script);
        }
        else
            CHECK(!script);
        CHECK(!h.collections_live && mm_owned_block_clear(&h.block.owner) == MM_DATA_OK);
    }
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
                trace.wide = mode == 5;
                h.host.encoding = trace.wide ? MM_TEXT_UTF16_LE : MM_TEXT_WINDOWS;
                h.host.unit_bytes = trace.wide ? 2 : 1;
                h.host.convert = widen;
                root(&h);
                CHECK((registry ? mm_script_registry_fetch(&h.block.owner, NULL, &bytes, &h.host)
                                : mm_script_colors_fetch(&h.block.owner, NULL, &bytes, &h.host)) == 1);
                CHECK(bytes == 15 && !h.decodes);
                out = NULL;
                CHECK((registry ? mm_script_registry_fetch(&h.block.owner, &out, &bytes, &h.host)
                                : mm_script_colors_fetch(&h.block.owner, &out, &bytes, &h.host)) == success);
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
static void runtime(void)
{
    MMScriptStack stack;
    MMScriptVariables local, object;
    MMI32 values[] = {10, 20, 30, 40, 50, 60};
    MMI32 rotated[] = {10, 20, 60, 30, 40, 50};
    MMI32 reversed[] = {10, 20, 50, 40, 30, 60};
    MMI32 pairs[] = {100, 7, 200, -8, 300, 7};
    MMI32 value;

    CHECK(mm_script_stack_init(&stack, NULL, 1) == MM_STORAGE_OK);
    CHECK(mm_script_stack_pop(&stack, &value) == MM_STORAGE_ARGUMENT);
    CHECK(mm_script_stack_rotate(&stack, 1, -2) == MM_STORAGE_OK);
    CHECK(mm_script_stack_reserve(&stack, 6) == MM_STORAGE_OK);
    CHECK(mm_script_stack_push(&stack, values, 6) == MM_STORAGE_OK);
    CHECK(mm_script_stack_rotate(&stack, 4, -1) == MM_STORAGE_OK);
    CHECK(!memcmp(mm_script_stack_top(&stack, 6), rotated, sizeof(rotated)));
    CHECK(mm_script_stack_reverse(&stack, 4) == MM_STORAGE_OK);
    CHECK(!memcmp(mm_script_stack_top(&stack, 6), reversed, sizeof(reversed)));
    CHECK(mm_storage_lock(&stack.storage, 0) == MM_STORAGE_OK);
    CHECK(mm_script_stack_duplicate(&stack, 6) == MM_STORAGE_LOCKED);
    CHECK(stack.count == 6 && !memcmp(mm_script_stack_top(&stack, 6), reversed, sizeof(reversed)));
    CHECK(mm_storage_unlock(&stack.storage, 0) == MM_STORAGE_OK);
    CHECK(mm_script_stack_duplicate(&stack, 6) == MM_STORAGE_OK);
    CHECK(stack.count == 12 && !memcmp(mm_script_stack_top(&stack, 6), reversed, sizeof(reversed)));
    CHECK(mm_script_stack_drop(&stack, 13) == MM_STORAGE_ARGUMENT);
    CHECK(mm_script_stack_select(&stack, 6, 1) == MM_STORAGE_OK);
    CHECK(mm_script_stack_pop(&stack, &value) == MM_STORAGE_OK && value == 30);
    CHECK(!memcmp(mm_script_stack_top(&stack, 6), reversed, sizeof(reversed)));
    CHECK(mm_script_stack_drop(&stack, 6) == MM_STORAGE_OK);
    CHECK(mm_script_stack_push(&stack, pairs, 6) == MM_STORAGE_OK);
    CHECK(mm_script_stack_match(&stack, 0x7fffffff, 7, 41) == MM_STORAGE_ARGUMENT);
    CHECK(mm_script_stack_match(&stack, 3, 7, 41) == MM_STORAGE_OK);
    CHECK(mm_script_stack_pop(&stack, &value) == MM_STORAGE_OK && value == 300);
    CHECK(mm_script_stack_push(&stack, pairs, 6) == MM_STORAGE_OK);
    CHECK(mm_script_stack_match(&stack, 3, 6, 41) == MM_STORAGE_OK);
    CHECK(mm_script_stack_pop(&stack, &value) == MM_STORAGE_OK && value == 41);
    mm_script_stack_dispose(&stack);
    CHECK(stack.count == 0 && !stack.storage.buffers[0].data);
    CHECK(mm_script_stack_push(&stack, &value, 1) == MM_STORAGE_OK);
    mm_script_stack_dispose(&stack);

    CHECK(mm_script_variables_init(&local, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_variables_init(&object, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_variables_set(&local, 0xffffffffUL, 0, -2));
    CHECK(mm_script_variables_set(&local, 0, 0xffffffffUL, -3));
    CHECK(mm_script_variables_set(&local, 0, 1, 10));
    CHECK(mm_script_variables_set(&local, 0, 0xffffffffUL, 12));
    CHECK(mm_script_variables_set(&object, 0, 1, 45));
    CHECK(local.count == 3 && object.count == 1);
    CHECK(mm_script_variables_get(&local, 0xffffffffUL, 0, &value) && value == -2);
    CHECK(mm_script_variables_get(&local, 0, 0xffffffffUL, &value) && value == 12);
    CHECK(mm_script_variables_get(&local, 0, 1, &value) && value == 10);
    CHECK(mm_script_variables_get(&object, 0, 1, &value) && value == 45);
    CHECK(!mm_script_variables_get(&object, 0, 0xffffffffUL, &value) && value == 45);
    mm_script_variables_dispose(&local);
    CHECK(!mm_script_variables_get(&local, 0, 1, &value) && value == 45);
    mm_script_variables_dispose(&object);
}

int main(void)
{
    scripts();
    collections();
    runtime();
    puts("C89 script resources, operand stack and variable scopes passed");
    return 0;
}
