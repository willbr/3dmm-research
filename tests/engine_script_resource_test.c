#include "mm/script_resource.h"
#include "mm/script_compile.h"
#include "mm/script_kidspace.h"
#include "mm/lex_token.h"
#include "mm/script_runtime.h"
#include "mm/script_opcodes.h"
#include "mm/script_strings.h"
#include "mm/script_vm.h"
#include "mm/text.h"
#include "mm/wide.h"
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
                        MMScriptStrings imported;
                        const void *text;
                        MMU32 units;
                        trace.calls = 0;
                        CHECK(!mm_script_registry_merge(out, put_string, &trace) && trace.calls == 2);
                        CHECK(mm_script_strings_init(&imported, h.host.unit_bytes, 0, NULL) == MM_STORAGE_OK);
                        CHECK(mm_script_registry_import(out, &imported));
                        CHECK(mm_script_strings_get(&imported, -17, &text, &units) && units == 3);
                        trace.calls = 0;
                        CHECK(!put_string(&trace, -17, text, units));
                        mm_script_strings_dispose(&imported);
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

#define VM_IMMEDIATES(n) ((MMU32)(n) << 16)
#define VM_VARIABLE(op) (((MMU32)(op) << 24) | VM_IMMEDIATES(1) | 0xabcdUL)
#define VM_VERSION 0x000a000aUL

typedef struct VMScopeFixture
{
    MMScriptVariables object, global, remote;
    unsigned calls;
} VMScopeFixture;

static MMScriptVariables *vm_variables(void *context, int scope, MMI32 object)
{
    VMScopeFixture *fixture = context;
    ++fixture->calls;
    if (scope == MM_SCRIPT_SCOPE_THIS)
        return &fixture->object;
    if (scope == MM_SCRIPT_SCOPE_GLOBAL)
        return &fixture->global;
    CHECK(scope == MM_SCRIPT_SCOPE_REMOTE && object == 123);
    return &fixture->remote;
}

static int attach_versioned_program(Host *host, MMScriptVM *vm, const MMU32 *words, size_t count,
                                    unsigned current, unsigned minimum)
{
    MMCompiledScript *script = mm_script_create(&host->host);
    size_t i;
    int ok;
    CHECK(script);
    for (i = 0; i < count; ++i)
        CHECK(mm_script_add_word(script, mm_signed_word(words[i])));
    ok = mm_script_vm_attach(vm, script, current, minimum, NULL, 0);
    mm_script_release(script);
    return ok;
}

static int attach_program(Host *host, MMScriptVM *vm, const MMU32 *words, size_t count)
{
    return attach_versioned_program(host, vm, words, count, 10, 10);
}

static void execute_scripts(void)
{
    static const MMScriptVMServices services = {vm_variables, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    static const MMU32 pause[] = {
        VM_VERSION, VM_IMMEDIATES(2), 0xfffffff1UL, 4, kopDiv,
        VM_IMMEDIATES(1) | kopPause, 0x7fffffffUL, kopInc,
        VM_IMMEDIATES(1), 0x80000000UL, kopEq, kopAdd, kopReturn
    };
    static const MMU32 branch[] = {
        VM_VERSION, VM_IMMEDIATES(2), 0xcc000008UL, 1, VM_IMMEDIATES(1) | kopGoNz,
        999, VM_IMMEDIATES(1), 888, VM_IMMEDIATES(1), 42, kopReturn
    };
    static const MMU32 scopes[] = {
        VM_VERSION, VM_IMMEDIATES(2), 0xffffffa8UL, 0xffffffffUL,
        VM_VARIABLE(kopPopLocArray), 9, VM_IMMEDIATES(1), 0xffffffffUL,
        VM_VARIABLE(kopPushLocArray), 9, VM_VARIABLE(kopPopThisVar), 9,
        VM_VARIABLE(kopPushThisVar), 9, VM_VARIABLE(kopPopGlobalVar), 9,
        VM_VARIABLE(kopPushGlobalVar), 9, VM_IMMEDIATES(1), 123,
        VM_VARIABLE(kopPopRemoteVar), 9, VM_IMMEDIATES(1), 123,
        VM_VARIABLE(kopPushRemoteVar), 9, kopReturn
    };
    static const MMU32 short_instruction[] = {VM_VERSION, VM_IMMEDIATES(2), 1};
    static const MMU32 short_variable[] = {VM_VERSION, (MMU32)kopPushLocVar << 24};
    static const MMU32 underflow[] = {VM_VERSION, kopAdd};
    static const MMU32 divide_overflow[] = {VM_VERSION, VM_IMMEDIATES(2), 0x80000000UL, 0xffffffffUL, kopDiv};
    static const MMU32 invalid_label[] = {VM_VERSION, VM_IMMEDIATES(2), 0xc1000001UL, 0, kopGoNz};
    static const MMU32 future[] = {0x000b000bUL, kopExit};
    VMScopeFixture fixture;
    MMScriptVM vm;
    MMI32 value;
    Host host;

    memset(&fixture, 0, sizeof(fixture));
    init(&host);
    CHECK(mm_script_variables_init(&fixture.object, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_variables_init(&fixture.global, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_variables_init(&fixture.remote, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_vm_init(&vm, NULL, 1, NULL, &services, &fixture) == MM_STORAGE_OK);

    CHECK(attach_program(&host, &vm, pause, sizeof(pause) / sizeof(*pause)));
    CHECK(mm_script_vm_resume(&vm) && vm.paused && vm.pc == 7 && vm.stack.count == 2);
    CHECK(mm_script_vm_resume(&vm) && !vm.paused && vm.result == -2);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live && vm.result == -2 && !vm.script);
    CHECK(attach_program(&host, &vm, branch, sizeof(branch) / sizeof(*branch)));
    CHECK(mm_script_vm_resume(&vm) && !vm.paused && vm.result == 42 && vm.stack.count == 0);
    mm_script_vm_dispose(&vm);

    CHECK(attach_program(&host, &vm, scopes, sizeof(scopes) / sizeof(*scopes)));
    CHECK(mm_script_vm_resume(&vm) && !vm.paused && vm.result == -88 && fixture.calls == 6);
    CHECK(mm_script_variables_get(&vm.locals, 0xffffabcdUL, 9, &value) && value == -88);
    mm_script_vm_dispose(&vm);
    CHECK(mm_script_variables_get(&fixture.object, 0xabcd, 9, &value) && value == -88);
    CHECK(mm_script_variables_get(&fixture.global, 0xabcd, 9, &value) && value == -88);
    CHECK(mm_script_variables_get(&fixture.remote, 0xabcd, 9, &value) && value == -88);
    mm_script_variables_dispose(&fixture.object);
    mm_script_variables_dispose(&fixture.global);
    mm_script_variables_dispose(&fixture.remote);

    CHECK(attach_program(&host, &vm, short_instruction, sizeof(short_instruction) / sizeof(*short_instruction)));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_INSTRUCTION && !vm.paused);
    mm_script_vm_dispose(&vm);
    CHECK(attach_program(&host, &vm, short_variable, sizeof(short_variable) / sizeof(*short_variable)));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_VARIABLE);
    mm_script_vm_dispose(&vm);
    CHECK(attach_program(&host, &vm, underflow, sizeof(underflow) / sizeof(*underflow)));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_OPERAND);
    mm_script_vm_dispose(&vm);
    CHECK(attach_program(&host, &vm, divide_overflow, sizeof(divide_overflow) / sizeof(*divide_overflow)));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_OPERAND);
    mm_script_vm_dispose(&vm);
    CHECK(attach_program(&host, &vm, invalid_label, sizeof(invalid_label) / sizeof(*invalid_label)));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_OPERAND);
    mm_script_vm_dispose(&vm);
    CHECK(!attach_program(&host, &vm, future, sizeof(future) / sizeof(*future)) &&
          vm.error == MM_SCRIPT_ERROR_VERSION);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
}

static void runtime_strings(void)
{
    MMScriptStrings registry;
    const void *text;
    MMU32 count;
    MMI32 id;
    char large[255];

    CHECK(mm_script_strings_init(&registry, 1, 1, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_strings_put(&registry, mm_signed_word(0x80000001UL), "occupied", 8));
    CHECK(mm_script_strings_add(&registry, &id, "literal", 7) && (MMU32)id == 0x80000002UL);
    CHECK(mm_script_strings_put(&registry, 10, "source", 6));
    CHECK(mm_script_strings_put(&registry, 20, "destination", 11));
    CHECK(mm_script_strings_put(&registry, 30, "next", 4));
    CHECK(!mm_script_strings_move(&registry, 10, 15));
    CHECK(mm_script_strings_move(&registry, 20, 20));
    CHECK(mm_script_strings_move(&registry, 10, 20));
    CHECK(mm_script_strings_get(&registry, 10, &text, &count) && count == 6 && !memcmp(text, "source", 6));
    CHECK(mm_script_strings_get(&registry, 20, &text, &count) && count == 4 && !memcmp(text, "next", 4));
    CHECK(!mm_script_strings_get(&registry, 30, &text, &count) && !text && !count);
    CHECK(mm_script_strings_move(&registry, 10, 20));
    CHECK(!mm_script_strings_get(&registry, 20, &text, &count));
    CHECK(mm_script_strings_get(&registry, 10, &text, &count) && count == 6);

    memset(large, 'x', sizeof(large));
    CHECK(mm_storage_lock(&registry.storage, 0) == MM_STORAGE_OK);
    CHECK(!mm_script_strings_add(&registry, &id, large, sizeof(large)) && (MMU32)id == 0x80000003UL);
    CHECK(!mm_script_strings_get(&registry, id, &text, &count));
    CHECK(mm_storage_unlock(&registry.storage, 0) == MM_STORAGE_OK);
    CHECK(mm_script_strings_add(&registry, &id, large, sizeof(large)) && (MMU32)id == 0x80000004UL);
    CHECK(mm_script_strings_get(&registry, id, &text, &count) && count == sizeof(large));
    mm_script_strings_delete(&registry, id);
    CHECK(!mm_script_strings_get(&registry, id, &text, &count));
    mm_script_strings_dispose(&registry);
}

static void execute_string_scripts(void)
{
    static const MMU32 program[] = {
        VM_VERSION, VM_IMMEDIATES(3), 100, 2, 1,
        ((MMU32)kopPushLocVar << 24) | VM_IMMEDIATES(1), 0, kopCopySubStr, kopPop,
        VM_IMMEDIATES(3), 100, 101, 100, kopConcatStrs, kopPop,
        VM_IMMEDIATES(1), 100, kopLenStr, kopSetReturn, kopPause,
        VM_IMMEDIATES(2), 102, 0x80000000UL, kopNumToStr, kopPop,
        VM_IMMEDIATES(3), 9, 7, 102, kopStrToNum, kopReturn
    };
    static const unsigned char narrow[] = {'a', 'b', 'c', 'd'};
    static const unsigned short wide[] = {'a', 'b', 'c', 'd'};
    static const unsigned char suffix[] = {' ', 'x'};
    static const unsigned short wide_suffix[] = {' ', 0x03a9};
    static const unsigned char expected[] = {'b', 'c', ' ', 'x'};
    static const unsigned short wide_expected[] = {'b', 'c', ' ', 0x03a9};
    Host host;
    MMScriptStrings registry;
    MMScriptVM vm;
    MMCompiledScript *script;
    const void *text;
    MMU32 i, count;
    unsigned unit;

    for (unit = 1; unit <= 2; ++unit)
    {
        init(&host);
        host.host.unit_bytes = unit;
        if (unit == 2)
            host.host.encoding = MM_TEXT_UTF16_LE;
        script = mm_script_create(&host.host);
        CHECK(script && mm_script_add_literal(script, unit == 1 ? (const void *)narrow : wide, 4, &i) && !i);
        for (i = 0; i < sizeof(program) / sizeof(*program); ++i)
            CHECK(mm_script_add_word(script, mm_signed_word(program[i])));
        CHECK(mm_script_strings_init(&registry, unit, 1, NULL) == MM_STORAGE_OK);
        CHECK(mm_script_strings_put(&registry, 101, unit == 1 ? (const void *)suffix : wide_suffix, 2));
        CHECK(mm_script_vm_init(&vm, NULL, 1, &registry, NULL, NULL) == MM_STORAGE_OK);
        CHECK(mm_script_vm_attach(&vm, script, 10, 10, NULL, 0));
        mm_script_release(script);
        CHECK(mm_script_strings_get(&registry, mm_signed_word(0x80000001UL), &text, &count) && count == 4);
        CHECK(mm_script_vm_resume(&vm) && vm.paused && vm.result == 4);
        CHECK(mm_script_strings_get(&registry, 100, &text, &count) && count == 4 &&
              !memcmp(text, unit == 1 ? (const void *)expected : wide_expected, 4 * unit));
        CHECK(mm_script_vm_resume(&vm) && !vm.paused && vm.result == mm_signed_word(0x80000000UL));
        mm_script_vm_dispose(&vm);
        CHECK(!mm_script_strings_get(&registry, mm_signed_word(0x80000001UL), &text, &count));
        CHECK(mm_script_strings_get(&registry, 100, &text, &count) && count == 4);
        mm_script_strings_dispose(&registry);
        CHECK(!host.collections_live);
    }
}

typedef struct FixtureAllocator
{
    int fail, speculate;
    int releases;
} FixtureAllocator;

static void *fixture_resize(void *context, void *old, MMU32 old_bytes, MMU32 new_bytes, int priority)
{
    FixtureAllocator *allocator = context;
    (void)old_bytes;
    CHECK(priority == MM_STORAGE_NORMAL || (allocator->speculate && priority == MM_STORAGE_SPEED));
    return allocator->fail ? NULL : realloc(old, new_bytes);
}

static void fixture_release(void *context, void *data, MMU32 bytes)
{
    FixtureAllocator *allocator = context;
    CHECK(bytes > 0);
    ++allocator->releases;
    free(data);
}

static void random_decks(void)
{
    static const MMI32 first[] = {2, 1, 0, 3, 4, 5};
    static const MMI32 second[] = {2, 1, 0, 4, 3, 5};
    static const MMI32 cards[] = {91, -8, 999, 75};
    static const MMI32 custom[] = {-8, 91, 75, 999};
    static const MMI32 repeated[] = {75, 999, 91, -8};
    MMRandom random;
    MMShuffle deck;
    MMStorageAllocator storage;
    FixtureAllocator allocator;
    size_t i;

    CHECK(mm_random_seed_from_ticks(0x12345678UL) == 0x78563412UL);
    random.seed = 0x963a8b66UL;
    CHECK(mm_random_bounded(&random, 0x07ffffffUL) == 4194303 && random.seed == 0x08000001UL);
    random.seed = 0x12345678UL;
    memset(&allocator, 0, sizeof(allocator));
    storage.context = &allocator;
    storage.resize = fixture_resize;
    storage.release = fixture_release;
    CHECK(mm_shuffle_init(&deck, &random, &storage) == MM_STORAGE_OK);
    CHECK(mm_shuffle_next(&deck, 0) == 0 && random.seed == 0x12345678UL);
    for (i = 0; i < sizeof(first) / sizeof(*first); ++i)
        CHECK(mm_shuffle_next(&deck, 6) == first[i]);
    for (i = 0; i < sizeof(second) / sizeof(*second); ++i)
        CHECK(mm_shuffle_next(&deck, 0) == second[i]);
    CHECK(mm_shuffle_set(&deck, 4, cards));
    for (i = 0; i < sizeof(custom) / sizeof(*custom); ++i)
        CHECK(mm_shuffle_next(&deck, 0) == custom[i]);
    for (i = 0; i < sizeof(repeated) / sizeof(*repeated); ++i)
        CHECK(mm_shuffle_next(&deck, 0) == repeated[i]);
    CHECK(mm_shuffle_next(&deck, 4) == 0 && mm_shuffle_next(&deck, 4) == 3 && !deck.custom);
    CHECK(random.seed == 0xabd57425UL);

    allocator.fail = 1;
    random.seed = 0xffeeddccUL;
    CHECK(mm_shuffle_next(&deck, 5) == 0 && random.seed == 99738687);
    CHECK(!deck.count && !deck.storage.buffers[0].data && allocator.releases == 1);
    CHECK(mm_shuffle_next(&deck, 0) == 0 && random.seed == 99738687);
    mm_shuffle_dispose(&deck);
    CHECK(allocator.releases == 1);
}

static void record_scale(void *context, MMU32 scale)
{
    *(MMU32 *)context = scale;
}

static void execute_random_scripts(void)
{
    static const MMU32 prepare[] = {
        VM_VERSION, VM_IMMEDIATES(1), 6, kopShuffle, kopNextCard, kopReturn
    };
    static const MMU32 next[] = {VM_VERSION, kopNextCard, kopReturn};
    static const MMU32 list[] = {
        VM_VERSION, VM_IMMEDIATES(5), 91, 0xfffffff8UL, 999, 75, 4, kopShuffleList, kopNextCard, kopReturn
    };
    static const MMU32 draw[] = {
        VM_VERSION, VM_IMMEDIATES(1), 0x80000000UL, kopScaleTime,
        VM_IMMEDIATES(1), 1000, kopRnd, kopReturn
    };
    static const MMU32 draw_list[] = {
        VM_VERSION, VM_IMMEDIATES(4), 10, 20, 30, 3, kopRndList, kopReturn
    };
    Host host;
    MMScriptVM first, second;
    MMScriptVMServices services;
    MMRandom random, deck_random;
    MMShuffle deck;
    MMU32 clock_scale = 0;

    init(&host);
    random.seed = 0x13579bdfUL;
    deck_random.seed = 0x12345678UL;
    CHECK(mm_shuffle_init(&deck, &deck_random, NULL) == MM_STORAGE_OK);
    memset(&services, 0, sizeof(services));
    services.random = &random;
    services.deck = &deck;
    services.scale_time = record_scale;
    CHECK(mm_script_vm_init(&first, NULL, 1, NULL, &services, &clock_scale) == MM_STORAGE_OK);
    CHECK(mm_script_vm_init(&second, NULL, 1, NULL, &services, &clock_scale) == MM_STORAGE_OK);

    CHECK(attach_program(&host, &first, prepare, sizeof(prepare) / sizeof(*prepare)));
    CHECK(mm_script_vm_resume(&first) && first.result == 2);
    mm_script_vm_dispose(&first);
    CHECK(attach_program(&host, &second, next, sizeof(next) / sizeof(*next)));
    CHECK(mm_script_vm_resume(&second) && second.result == 1);
    mm_script_vm_dispose(&second);
    CHECK(attach_program(&host, &first, list, sizeof(list) / sizeof(*list)));
    CHECK(mm_script_vm_resume(&first) && first.result == 91 && first.stack.count == 4);
    CHECK(!first.stack.storage.buffers[0].locks);
    mm_script_vm_dispose(&first);
    CHECK(attach_program(&host, &second, next, sizeof(next) / sizeof(*next)));
    CHECK(mm_script_vm_resume(&second) && second.result == -8);
    mm_script_vm_dispose(&second);

    CHECK(attach_program(&host, &first, draw, sizeof(draw) / sizeof(*draw)));
    CHECK(mm_script_vm_resume(&first) && first.result == 792 && clock_scale == 0x80000000UL);
    mm_script_vm_dispose(&first);
    CHECK(attach_program(&host, &second, draw_list, sizeof(draw_list) / sizeof(*draw_list)));
    CHECK(mm_script_vm_resume(&second) && second.result == 30 && random.seed == 897371577);
    mm_script_vm_dispose(&second);
    mm_shuffle_dispose(&deck);
    CHECK(!host.collections_live);
}

typedef struct KidObject
{
    MMI32 id;
    MMPixelRect bounds;
    int fixed;
    struct KidObject *parent, *child;
} KidObject;

typedef struct KidFixture
{
    KidObject parent, child;
    MMQueuedCommand command;
    MMI32 params[2], count;
    MMU32 script;
    int by_number, runs, run_status;
    int cells, missing, argument_failures, run_failures;
} KidFixture;

static void *kid_find(void *context, MMI32 id)
{
    KidFixture *fixture = context;
    return id == 1 ? &fixture->parent : id == 2 ? &fixture->child : NULL;
}

static MMI32 kid_id(void *context, void *object)
{
    (void)context;
    return ((KidObject *)object)->id;
}

static void *kid_relative(void *context, void *object, int relation)
{
    KidObject *gob = object;
    (void)context;
    return relation == MM_KIDSPACE_PARENT ? gob->parent :
           relation == MM_KIDSPACE_FIRST_CHILD ? gob->child : NULL;
}

static int kid_rect(void *context, void *object, int mode, MMPixelRect *out)
{
    KidObject *gob = object;
    (void)context;
    if (mode == MM_KIDSPACE_MOVABLE_RECT && gob->fixed)
        return 0;
    *out = gob->bounds;
    if (mode == MM_KIDSPACE_LOCAL_RECT)
    {
        out->right -= out->left;
        out->bottom -= out->top;
        out->left = out->top = 0;
    }
    return 1;
}

static void kid_set_rect(void *context, void *object, const MMPixelRect *in)
{
    (void)context;
    ((KidObject *)object)->bounds = *in;
}

static int kid_point(void *context, void *object, int mode, MMPixelPoint *out)
{
    KidObject *gob = object;
    (void)context;
    if (mode == MM_KIDSPACE_MOUSE)
    {
        out->x = -7;
        out->y = 6;
    }
    else if (!gob->parent)
        return 0;
    else
    {
        out->x = 3;
        out->y = 4;
        if (mode == MM_KIDSPACE_PARENT_REGISTRATION)
        {
            out->x += gob->bounds.left;
            out->y += gob->bounds.top;
        }
    }
    return 1;
}

static void kid_enqueue(void *context, const MMQueuedCommand *command)
{
    ((KidFixture *)context)->command = *command;
}

static int kid_run(void *context, void *object, MMU32 script, int by_number, void *params,
                   MMI32 count, MMI32 *out)
{
    KidFixture *fixture = context;
    CHECK(object == &fixture->child && (count == 0 || count == 2));
    ++fixture->runs;
    fixture->script = script;
    fixture->by_number = by_number;
    fixture->count = count;
    if (params)
        memcpy(fixture->params, params, sizeof(fixture->params));
    else
        CHECK(count == 0);
    *out = 73;
    return fixture->run_status;
}

static void kid_cell(void *context, void *object, MMU32 child, unsigned flags, MMU32 tag,
                     MMI32 dx, MMI32 dy, MMU32 delay)
{
    KidFixture *fixture = context;
    CHECK(object == &fixture->child && child == 0x80000000UL && dx == 5 && dy == -2 && delay == 9);
    CHECK(flags == MM_KIDSPACE_REP_SKIP_ANIMATION && !tag);
    ++fixture->cells;
}

static void kid_notice(void *context, int reason, unsigned opcode, MMI32 id, MMU32 script)
{
    KidFixture *fixture = context;
    if (reason == MM_KIDSPACE_MISSING_OBJECT)
    {
        CHECK((opcode == kopResizeGob && id == 1) || (opcode == kopRunScriptGob && id == 99));
        ++fixture->missing;
    }
    else
    {
        CHECK(opcode == kopRunScriptCnoThis && id == 2 && script == 701);
        if (reason == MM_KIDSPACE_ARGUMENT_ALLOCATION)
            ++fixture->argument_failures;
        else if (reason == MM_KIDSPACE_NESTED_FAILURE)
            ++fixture->run_failures;
    }
}

static void execute_kidspace_scripts(void)
{
    static const MMKidspaceScriptServices services = {
        kid_find, kid_id, kid_relative, NULL, kid_rect, kid_set_rect, kid_point,
        kid_find, kid_enqueue, kid_run, kid_cell, kid_notice, NULL, NULL, NULL, NULL, NULL, NULL
    };
    static const MMU32 geometry[] = {
        0x101d101dUL, VM_IMMEDIATES(3), 21, 22, 1, kopResizeGob,
        VM_IMMEDIATES(3), 70, 60, 2, kopMoveAbsGob,
        VM_IMMEDIATES(2), 0xfffffffcUL, 10, kopMoveRelThis,
        VM_IMMEDIATES(2), 12, 0xfffffffdUL, kopResizeThis,
        VM_IMMEDIATES(1), 1, kopXGob, kopWidthThis, kopAdd, kopHeightThis, kopAdd, kopGidParThis, kopAdd,
        VM_IMMEDIATES(2), 1, 2, kopFIsDescendent, kopAdd,
        VM_IMMEDIATES(1), 1, kopGidChild, kopAdd, kopYMouseThis, kopAdd, kopReturn
    };
    static const MMU32 command[] = {
        0x101d101dUL, VM_IMMEDIATES(6), 44, 33, 0xffffffeaUL, 11, 2, 900, kopEnqueueCid, kopExit
    };
    static const MMU32 invoke[] = {
        0x101d101dUL, VM_IMMEDIATES(4), 311, 0xffffff99UL, 701, 2, kopRunScriptCnoThis, kopReturn
    };
    static const MMU32 missing[] = {
        0x101d101dUL, VM_IMMEDIATES(3), 701, 99, 0, kopRunScriptGob, kopReturn
    };
    static const MMU32 animation[] = {
        0x101d101dUL, VM_IMMEDIATES(4), 9, 0xfffffffeUL, 5, 0x80000000UL, VM_IMMEDIATES(1) | kopCell, 17, kopReturn
    };
    static const MMPixelRect parent = {10, 20, 110, 220}, child = {20, 30, 50, 70};
    const unsigned current = 0x101d, minimum = 0x1015;
    Host host;
    MMScriptVM vm;
    KidFixture fixture;
    FixtureAllocator failure;
    MMStorageAllocator allocator;

    init(&host);
    memset(&fixture, 0, sizeof(fixture));
    memset(&failure, 0, sizeof(failure));
    fixture.parent.id = 1;
    fixture.parent.bounds = parent;
    fixture.parent.fixed = 1;
    fixture.parent.child = &fixture.child;
    fixture.child.id = 2;
    fixture.child.bounds = child;
    fixture.child.parent = &fixture.parent;
    fixture.run_status = 1;
    allocator.context = &failure;
    allocator.resize = fixture_resize;
    allocator.release = fixture_release;
    CHECK(mm_script_vm_init(&vm, &allocator, 1, NULL, NULL, &fixture) == MM_STORAGE_OK);
    vm.kidspace = &services;
    vm.this_object = 2;

    CHECK(attach_versioned_program(&host, &vm, geometry, sizeof(geometry) / sizeof(*geometry), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 32 && fixture.missing == 1);
    CHECK(fixture.child.bounds.left == 67 && fixture.child.bounds.top == 62 &&
          fixture.child.bounds.right == 67 && fixture.child.bounds.bottom == 74);
    CHECK(fixture.parent.bounds.right == 110 && fixture.parent.bounds.bottom == 220);
    mm_script_vm_dispose(&vm);

    CHECK(attach_versioned_program(&host, &vm, command, sizeof(command) / sizeof(*command), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && fixture.command.id == 900 && fixture.command.target == &fixture.child);
    CHECK(!fixture.command.group && fixture.command.arguments[0] == 11 && fixture.command.arguments[1] == -22 &&
          fixture.command.arguments[2] == 33 && fixture.command.arguments[3] == 44);
    mm_script_vm_dispose(&vm);

    CHECK(attach_versioned_program(&host, &vm, invoke, sizeof(invoke) / sizeof(*invoke), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 73 && fixture.runs == 1 && fixture.count == 2);
    CHECK(fixture.params[0] == -103 && fixture.params[1] == 311 && fixture.by_number && fixture.script == 701);
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, invoke, sizeof(invoke) / sizeof(*invoke), current, minimum));
    failure.fail = 1;
    fixture.run_status = 0;
    CHECK(mm_script_vm_resume(&vm) && vm.result == 0 && fixture.runs == 2 && !fixture.count);
    CHECK(fixture.argument_failures == 1 && fixture.run_failures == 1);
    failure.fail = 0;
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, missing, sizeof(missing) / sizeof(*missing), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 0 && fixture.runs == 2 && fixture.missing == 2);
    mm_script_vm_dispose(&vm);

    CHECK(attach_versioned_program(&host, &vm, animation, sizeof(animation) / sizeof(*animation), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.paused && fixture.cells == 1 && vm.stack.count == 1);
    CHECK(mm_script_vm_resume(&vm) && !vm.paused && fixture.cells == 1 && vm.result == 17);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
}

typedef struct TimedKidspace
{
    MMAlarmClock clock;
    int references, starts, stops, retained, missing_clock, failed_alarm;
    int current_gob, other_gob, mixer, attached_plays, global_plays, mouse_plays, fallbacks;
    unsigned operations;
    MMI32 volume;
} TimedKidspace;

static void *timed_find(void *context, MMI32 id)
{
    TimedKidspace *fixture = context;
    return id == 2 ? &fixture->current_gob : id == 1 ? &fixture->other_gob : NULL;
}

static MMI32 timed_unique(void *context)
{
    (void)context;
    return 77;
}

static MMAlarmClock *timed_clock(void *context, MMI32 id)
{
    TimedKidspace *fixture = context;
    return fixture->references && fixture->clock.id == id ? &fixture->clock : NULL;
}

static int timed_create(void *context, MMI32 id)
{
    TimedKidspace *fixture = context;
    CHECK(!fixture->references && id == 77);
    fixture->references = 1;
    return mm_alarm_clock_init(&fixture->clock, fixture, id, 0, NULL, 1);
}

static void timed_retain(void *context, MMAlarmClock *clock)
{
    TimedKidspace *fixture = context;
    CHECK(clock == &fixture->clock);
    ++fixture->references;
}

static void timed_release(void *context, MMAlarmClock *clock)
{
    TimedKidspace *fixture = context;
    CHECK(clock == &fixture->clock && fixture->references > 0);
    if (!--fixture->references)
        CHECK(mm_alarm_clock_dispose(clock));
}

static int timed_now(void *context, MMU32 *milliseconds)
{
    (void)context;
    *milliseconds = 43;
    return 1;
}

static void timed_start(void *context, MMAlarmClock *clock, MMU32 time)
{
    TimedKidspace *fixture = context;
    static const MMClockServices services = {NULL, timed_now, NULL};
    CHECK(clock == &fixture->clock && mm_alarm_clock_start(clock, time, &services));
    ++fixture->starts;
}

static void timed_stop(void *context, MMAlarmClock *clock)
{
    TimedKidspace *fixture = context;
    CHECK(clock == &fixture->clock);
    ++fixture->stops;
}

static int timed_alarm(void *context, MMAlarmClock *clock, void *target, MMI32 delay, MMI32 script)
{
    TimedKidspace *fixture = context;
    CHECK(clock == &fixture->clock);
    return mm_alarm_clock_schedule(clock, delay, target, script, 0, 0);
}

static int timed_sound_object(void *context, void *object)
{
    return object == &((TimedKidspace *)context)->current_gob;
}

static MMI32 timed_play(void *context, void *object, const MMSoundPlay *play)
{
    TimedKidspace *fixture = context;
    CHECK(play->type == 0x57415645UL && play->number == 0x80000010UL && play->queue == 5 &&
          play->volume == 2748 && play->plays == 3 && !play->offset && play->priority == -7 &&
          play->sound_class == -8);
    if (object)
    {
        CHECK(object == &fixture->current_gob);
        ++fixture->attached_plays;
        return 81;
    }
    ++fixture->global_plays;
    return fixture->mixer ? 82 : 0;
}

static MMI32 timed_mouse(void *context, void *object, MMU32 type, MMU32 number)
{
    TimedKidspace *fixture = context;
    CHECK(object == &fixture->current_gob && type == 0x4d494453UL && number == 0xffffffffUL);
    ++fixture->mouse_plays;
    return 83;
}

static int timed_control(void *context, int operation, MMI32 first, MMI32 second, MMI32 *out)
{
    TimedKidspace *fixture = context;
    if (!fixture->mixer)
        return 0;
    fixture->operations |= 1U << operation;
    switch (operation)
    {
    case MM_SOUND_ALL_STOP: case MM_SOUND_ALL_RESUME: case MM_SOUND_ALL_PLAYING:
        CHECK(first == 7 && second == 6);
        *out = 1;
        break;
    case MM_SOUND_INSTANCE_PAUSE:
        CHECK(first == 81 && !second);
        break;
    case MM_SOUND_VOLUME:
        fixture->volume = first;
        break;
    case MM_SOUND_GET_VOLUME:
        *out = fixture->volume;
        break;
    default:
        CHECK(0);
    }
    return 1;
}

static void timed_notice(void *context, int reason, unsigned opcode, MMI32 id, MMU32 detail)
{
    TimedKidspace *fixture = context;
    (void)detail;
    switch (reason)
    {
    case MM_KIDSPACE_CLOCK_RETAINED:
        CHECK(opcode == kopCreateClock && id == 77);
        ++fixture->retained;
        break;
    case MM_KIDSPACE_CLOCK_MISSING:
        CHECK(opcode == kopTimeCur && id == 88);
        ++fixture->missing_clock;
        break;
    case MM_KIDSPACE_ALARM_FAILED:
        CHECK(opcode == kopSetAlarm && id == 88);
        ++fixture->failed_alarm;
        break;
    case MM_KIDSPACE_SOUND_FALLBACK:
        CHECK(opcode == kopPlaySoundGob && id == 1);
        ++fixture->fallbacks;
        break;
    default:
        CHECK(0);
    }
}

static void execute_timed_scripts(void)
{
    static const MMKidspaceClockServices clock = {
        timed_clock, timed_create, timed_retain, timed_release, timed_start, timed_stop, timed_alarm
    };
    static const MMKidspaceSoundServices sound = {timed_play, timed_mouse, timed_control};
    static const MMKidspaceScriptServices services = {
        timed_find, NULL, NULL, timed_unique, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, timed_notice, &clock, &sound, timed_sound_object, NULL, NULL, NULL
    };
    static const MMU32 clocks[] = {
        0x101d101dUL, VM_IMMEDIATES(1), 0, kopCreateClock, kopPop,
        VM_IMMEDIATES(1), 77, kopCreateClock, kopPop,
        VM_IMMEDIATES(2), 0x80000025UL, 77, kopStartClock,
        VM_IMMEDIATES(2), 2, 77, kopSetAlarm,
        VM_IMMEDIATES(3), 0x80000003UL, 3, 77, kopSetAlarmThis,
        VM_IMMEDIATES(4), 9, 4, 77, 1, kopSetAlarmGob,
        VM_IMMEDIATES(1), 77, kopStopClock,
        VM_IMMEDIATES(1), 77, kopDestroyClock,
        VM_IMMEDIATES(1), 77, kopTimeCur, kopReturn
    };
    static const MMU32 missing[] = {
        0x101d101dUL, VM_IMMEDIATES(2), 2, 88, kopSetAlarm,
        VM_IMMEDIATES(1), 88, kopTimeCur, kopReturn
    };
    static const MMU32 sounds[] = {
        0x101d101dUL, VM_IMMEDIATES(7), 0xfffffff8UL, 0xfffffff9UL, 3, 2748, 5, 0x80000010UL, 0x57415645UL,
        kopPlaySoundThis, kopPop,
        VM_IMMEDIATES(8), 0xfffffff8UL, 0xfffffff9UL, 3, 2748, 5, 0x80000010UL, 0x57415645UL, 1,
        kopPlaySoundGob, kopPop,
        VM_IMMEDIATES(3), 0xffffffffUL, 0x4d494453UL, 2, kopPlayMouseSoundGob, kopPop,
        VM_IMMEDIATES(2), 6, 7, kopStopSoundClass,
        VM_IMMEDIATES(2), 6, 7, kopResumeSoundClass,
        VM_IMMEDIATES(1), 81, kopPauseSound,
        VM_IMMEDIATES(1), 0x87654321UL, kopSetMasterVolume,
        VM_IMMEDIATES(2), 6, 7, kopPlayingSoundClass, kopGetMasterVolume, kopAdd, kopReturn
    };
    static const MMU32 no_mixer[] = {
        0x101d101dUL, VM_IMMEDIATES(1), 83, kopPlayingSound,
        VM_IMMEDIATES(2), 6, 7, kopPlayingSoundClass, kopGetMasterVolume, kopReturn
    };
    const unsigned current = 0x101d, minimum = 0x1015;
    TimedKidspace fixture;
    MMClockAlarm alarm;
    Host host;
    MMScriptVM vm;

    init(&host);
    memset(&fixture, 0, sizeof(fixture));
    CHECK(mm_script_vm_init(&vm, NULL, 1, NULL, NULL, &fixture) == MM_STORAGE_OK);
    vm.kidspace = &services;
    vm.this_object = 2;
    CHECK(attach_versioned_program(&host, &vm, clocks, sizeof(clocks) / sizeof(*clocks), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && (MMU32)vm.result == 0x80000025UL && !vm.stack.count);
    CHECK(fixture.references == 1 && fixture.retained == 1 && fixture.starts == 1 && fixture.stops == 1);
    CHECK(fixture.clock.count == 3 && fixture.clock.stamp == 43);
    CHECK(mm_alarm_clock_at(&fixture.clock, 0, &alarm) && alarm.target == &fixture.other_gob &&
          alarm.time == 0x80000029UL && alarm.value == 9);
    CHECK(mm_alarm_clock_at(&fixture.clock, 1, &alarm) && alarm.target == &fixture.current_gob &&
          alarm.time == 0x80000028UL && (MMU32)alarm.value == 0x80000003UL);
    CHECK(mm_alarm_clock_at(&fixture.clock, 2, &alarm) && alarm.target == &fixture.current_gob &&
          alarm.time == 0x80000027UL && alarm.value == -1);
    mm_script_vm_dispose(&vm);
    timed_release(&fixture, &fixture.clock);
    CHECK(attach_versioned_program(&host, &vm, missing, sizeof(missing) / sizeof(*missing), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 0 && fixture.missing_clock == 1 && fixture.failed_alarm == 1);
    mm_script_vm_dispose(&vm);

    fixture.mixer = 1;
    CHECK(attach_versioned_program(&host, &vm, sounds, sizeof(sounds) / sizeof(*sounds), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && (MMU32)vm.result == 0x87654322UL && !vm.stack.count);
    CHECK(fixture.attached_plays == 1 && fixture.global_plays == 1 && fixture.mouse_plays == 1 && fixture.fallbacks == 1);
    CHECK(fixture.operations == ((1U << MM_SOUND_ALL_STOP) | (1U << MM_SOUND_ALL_RESUME) |
          (1U << MM_SOUND_ALL_PLAYING) | (1U << MM_SOUND_INSTANCE_PAUSE) |
          (1U << MM_SOUND_VOLUME) | (1U << MM_SOUND_GET_VOLUME)));
    mm_script_vm_dispose(&vm);

    fixture.mixer = 0;
    CHECK(attach_versioned_program(&host, &vm, no_mixer, sizeof(no_mixer) / sizeof(*no_mixer), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 0 && !vm.stack.count);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
}

typedef struct ScriptObject
{
    MMI32 id;
    struct ScriptObject *parent, *first, *next;
} ScriptObject;

typedef struct ObjectFixture
{
    ScriptObject world, current, child, help;
    MMI32 state, plane, frame;
    int forgotten, releases, playing, reps, filtered, no_slip, streamed, tooltip;
    int missing, invalid_states, destroyed_world, failed_filters;
    MMI32 released[3];
} ObjectFixture;

static void *script_object_find(void *context, MMI32 id)
{
    ObjectFixture *fixture = context;
    return id == 1 ? &fixture->world : id == 2 && fixture->current.parent ? &fixture->current : NULL;
}

static MMI32 script_object_id(void *context, void *object)
{
    (void)context;
    return ((ScriptObject *)object)->id;
}

static void *script_object_relative(void *context, void *object, int relation)
{
    (void)context;
    CHECK(relation == MM_KIDSPACE_FIRST_CHILD);
    return ((ScriptObject *)object)->first;
}

static int script_object_kind(void *context, void *object)
{
    return object == &((ObjectFixture *)context)->current;
}

static void *script_object_world(void *context)
{
    return &((ObjectFixture *)context)->world;
}

static void script_object_forget(void *context)
{
    ++((ObjectFixture *)context)->forgotten;
}

static void *script_object_create_child(void *context, void *parent, MMI32 id, MMU32 descriptor)
{
    ObjectFixture *fixture = context;
    ScriptObject *object = parent;
    CHECK(fixture->forgotten > 0 && descriptor == 0x80000001UL && id == -17);
    if (object == &fixture->current)
        return NULL;
    CHECK(object == &fixture->world && object->first == &fixture->current);
    fixture->child.id = id;
    fixture->child.parent = object;
    fixture->child.next = object->first;
    object->first = &fixture->child;
    return object->first;
}

static void *script_object_create_help(void *context, void *parent, MMU32 topic)
{
    ObjectFixture *fixture = context;
    ScriptObject *object = parent;
    CHECK(object == &fixture->current && fixture->forgotten == 2 && topic == 52);
    fixture->help.id = 81;
    fixture->help.parent = object;
    object->first = &fixture->help;
    return object->first;
}

static void script_object_release(void *context, void *object)
{
    ObjectFixture *fixture = context;
    ScriptObject *child = object;
    CHECK(fixture->forgotten > 0 && child->parent && child->parent->first == child && fixture->releases < 3);
    child->parent->first = child->next;
    child->parent = NULL;
    fixture->released[fixture->releases++] = child->id;
}

static void script_object_change(void *context, void *object, MMI32 state)
{
    ObjectFixture *fixture = context;
    CHECK(object == &fixture->current && state == 32766 && fixture->forgotten == 4);
    fixture->state = state;
}

static MMI32 script_object_state(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    return ((ObjectFixture *)context)->state;
}

static MMI32 script_object_plane(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    return ((ObjectFixture *)context)->plane;
}

static void script_object_set_plane(void *context, void *object, MMI32 plane)
{
    CHECK(script_object_kind(context, object));
    ((ObjectFixture *)context)->plane = plane;
}

static void script_object_play(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    ((ObjectFixture *)context)->playing = -1;
}

static int script_object_playing(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    return ((ObjectFixture *)context)->playing;
}

static void script_object_stop(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    ((ObjectFixture *)context)->playing = 0;
}

static void script_object_goto(void *context, void *object, MMI32 frame)
{
    CHECK(script_object_kind(context, object));
    ((ObjectFixture *)context)->frame = frame;
}

static MMI32 script_object_frame(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    return ((ObjectFixture *)context)->frame;
}

static MMI32 script_object_frames(void *context, void *object)
{
    CHECK(script_object_kind(context, object));
    return 18;
}

static int script_object_filter(void *context, void *object, MMI32 command, MMI32 id, MMU32 script)
{
    CHECK(script_object_kind(context, object) && command == 102 && id == -9 && script == 0xffffffffUL);
    ++((ObjectFixture *)context)->filtered;
    return 0;
}

static void script_object_no_slip(void *context, void *object, int enabled)
{
    CHECK(script_object_kind(context, object) && enabled == 1);
    ++((ObjectFixture *)context)->no_slip;
}

static void script_object_stream(void *context, void *object, int enabled)
{
    CHECK(script_object_kind(context, object) && !enabled);
    ++((ObjectFixture *)context)->streamed;
}

static void script_object_tooltip(void *context, void *object, MMI32 id)
{
    CHECK(script_object_kind(context, object) && id == -21);
    ++((ObjectFixture *)context)->tooltip;
}

static void script_object_rep(void *context, void *object, MMU32 child, unsigned flags, MMU32 tag,
                              MMI32 dx, MMI32 dy, MMU32 delay)
{
    static const MMU32 tags[] = {MM_KIDSPACE_TAG_ANIMATION, MM_KIDSPACE_TAG_BITMAP, 0};
    ObjectFixture *fixture = context;
    int index = fixture->reps++;
    CHECK(object == &fixture->current && index < 3 && child == 0xf0000000UL + index && !dx && !dy && !delay);
    CHECK(tag == tags[index] && flags == (index ? MM_KIDSPACE_REP_KILL_ANIMATION : 0));
}

static void script_object_notice(void *context, int reason, unsigned opcode, MMI32 id, MMU32 detail)
{
    ObjectFixture *fixture = context;
    switch (reason)
    {
    case MM_KIDSPACE_MISSING_OBJECT:
        CHECK((opcode == kopCreateHelpGob && id == 99) || (opcode == kopCurFrameGob && id == 1));
        ++fixture->missing;
        break;
    case MM_KIDSPACE_INVALID_STATE:
        CHECK(opcode == kopChangeStateGob && id == 2 && (detail == 0xffffffffUL || detail == 32767));
        ++fixture->invalid_states;
        break;
    case MM_KIDSPACE_FILTER_FAILED:
        CHECK(opcode == kopFilterCmdsGob && id == 2);
        ++fixture->failed_filters;
        break;
    case MM_KIDSPACE_DESTROY_WORLD:
        CHECK(opcode == kopDestroyGob && id == 1);
        ++fixture->destroyed_world;
        break;
    default:
        CHECK(0);
    }
}

static void execute_kidspace_objects(void)
{
    static const MMKidspaceObjectServices objects = {
        script_object_world, script_object_create_child, script_object_create_help,
        script_object_release, script_object_change, script_object_state, script_object_plane, script_object_set_plane,
        script_object_play, script_object_playing, script_object_stop, script_object_frame, script_object_frames,
        script_object_goto, script_object_filter, script_object_no_slip, script_object_tooltip, script_object_stream
    };
    static const MMKidspaceScriptServices services = {
        script_object_find, script_object_id, script_object_relative, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, script_object_rep, script_object_notice, NULL, NULL, script_object_kind, &objects,
        script_object_forget, NULL
    };
    static const MMU32 create[] = {
        0x101d101dUL, VM_IMMEDIATES(3), 0x80000001UL, 0xffffffefUL, 1, kopCreateChildGob,
        VM_IMMEDIATES(1), 52, kopCreateHelpThis, kopAdd,
        VM_IMMEDIATES(2), 0x80000001UL, 0xffffffefUL, kopCreateChildThis, kopAdd,
        VM_IMMEDIATES(2), 52, 99, kopCreateHelpGob, kopAdd, kopReturn
    };
    static const MMU32 control[] = {
        0x101d101dUL, VM_IMMEDIATES(2), 0xffffffffUL, 2, kopChangeStateGob,
        VM_IMMEDIATES(2), 32767, 2, kopChangeStateGob,
        VM_IMMEDIATES(1), 32766, kopChangeStateThis, kopStateThis,
        VM_IMMEDIATES(1), 0xf0000000UL, kopAnimateThis,
        VM_IMMEDIATES(2), 0xf0000001UL, 2, kopSetPictureGob,
        VM_IMMEDIATES(1), 0xf0000002UL, kopSetRepThis,
        VM_IMMEDIATES(2), 0xfffffff7UL, 2, kopSetZGob, kopZThis, kopAdd,
        kopPlayThis, VM_IMMEDIATES(1), 2, kopPlayingGob, kopAdd,
        VM_IMMEDIATES(2), 3, 2, kopGotoFrameGob, kopCurFrameThis, kopAdd, kopCountFramesThis, kopAdd,
        VM_IMMEDIATES(1), 1, kopCurFrameGob, kopAdd,
        VM_IMMEDIATES(4), 0xffffffffUL, 0xfffffff7UL, 102, 2, kopFilterCmdsGob,
        VM_IMMEDIATES(1), 0xfffffffbUL, kopSetNoSlipThis,
        VM_IMMEDIATES(2), 0xffffffebUL, 2, kopSetToolTipSourceGob,
        VM_IMMEDIATES(1), 0, kopStreamThis,
        VM_IMMEDIATES(1), 2, kopStopGob, kopPlayingThis, kopAdd, kopReturn
    };
    static const MMU32 destroy[] = {
        0x101d101dUL, kopDestroyChildrenThis,
        VM_IMMEDIATES(1), 1, kopDestroyGob, kopDestroyThis, kopGidThis, kopReturn
    };
    const unsigned current = 0x101d, minimum = 0x1015;
    ObjectFixture fixture;
    Host host;
    MMScriptVM vm;

    init(&host);
    memset(&fixture, 0, sizeof(fixture));
    fixture.world.id = 1;
    fixture.world.first = &fixture.current;
    fixture.current.id = 2;
    fixture.current.parent = &fixture.world;
    CHECK(mm_script_vm_init(&vm, NULL, 1, NULL, NULL, &fixture) == MM_STORAGE_OK);
    vm.kidspace = &services;
    vm.this_object = 2;

    CHECK(attach_versioned_program(&host, &vm, create, sizeof(create) / sizeof(*create), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 64 && !vm.stack.count);
    CHECK(fixture.forgotten == 3 && fixture.missing == 1);
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, control, sizeof(control) / sizeof(*control), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 32779 && !vm.stack.count);
    CHECK(fixture.invalid_states == 2 && fixture.missing == 2 && fixture.reps == 3 && !fixture.playing);
    CHECK(fixture.filtered == 1 && fixture.failed_filters == 1 && fixture.forgotten == 4);
    CHECK(fixture.no_slip == 1 && fixture.streamed == 1 && fixture.tooltip == 1);
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, destroy, sizeof(destroy) / sizeof(*destroy), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 2 && !vm.stack.count);
    CHECK(fixture.destroyed_world == 1 && fixture.forgotten == 6 && fixture.releases == 3 && !fixture.world.first);
    CHECK(fixture.released[0] == 81 && fixture.released[1] == -17 && fixture.released[2] == 2);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
}

typedef struct AppFixture
{
    Host *host;
    int palettes, loads, transitions, edit_reads, edit_writes, launches, helps;
    int messages, destinations[3], forgotten, failures, begins, ends;
    MMU32 modifiers, events;
    unsigned text_unit;
    char text[3][MM_KIDSPACE_TEXT_LIMIT + 1];
} AppFixture;

static MMScriptCollection *app_palette(void *context, MMU32 number)
{
    AppFixture *app = context;
    Host *host = app->host;
    MMI32 bytes;
    void *palette;
    ++app->loads;
    if (number == 9)
        return NULL;
    CHECK(number == 8);
    collection_wire(host, 0, 0, 0);
    root(host);
    CHECK(mm_script_colors_fetch(&host->block.owner, &palette, &bytes, &host->host));
    CHECK(mm_owned_block_clear(&host->block.owner) == MM_DATA_OK);
    return palette;
}

static void app_set_palette(void *context, const MMKidspacePalette *palette)
{
    CHECK(palette && palette->count == 2 && !memcmp(palette->rows, "abcdefgh", 8));
    ++((AppFixture *)context)->palettes;
}

static void app_transition(void *context, MMI32 kind, MMI32 parameter, MMU32 milliseconds,
                           MMU32 color, const MMKidspacePalette *palette)
{
    AppFixture *app = context;
    CHECK(kind == 4 && parameter == -3 && color == 0x80230405UL);
    if (app->transitions++ == 0)
        CHECK(milliseconds == 1016 && palette && palette->count == 2 && !memcmp(palette->rows, "abcdefgh", 8));
    else
        CHECK(milliseconds == 2863311514UL && !palette);
}

static MMU32 app_modifiers(void *context)
{
    return ((AppFixture *)context)->modifiers;
}

static void app_change_modifiers(void *context, MMU32 set, MMU32 toggle)
{
    AppFixture *app = context;
    CHECK(set == 0x10 && toggle == 0x04000000UL);
    app->modifiers = (app->modifiers | set) ^ toggle;
}

static void *app_object(void *context, MMI32 id)
{
    AppFixture *app = context;
    return id == 2 ? &app->edit_reads : id == 3 ? &app->edit_writes : NULL;
}

static int app_is_edit(void *context, void *object)
{
    return object == &((AppFixture *)context)->edit_reads;
}

static MMU32 app_read_edit(void *context, void *object, void *text, MMU32 capacity)
{
    CHECK(app_is_edit(context, object) && capacity == 255);
    ++((AppFixture *)context)->edit_reads;
    memset(text, 'e', capacity);
    return capacity;
}

static void app_write_edit(void *context, void *object, const void *text, MMU32 count)
{
    AppFixture *app = context;
    CHECK(app_is_edit(context, object));
    if (app->edit_writes++ == 0)
        CHECK(count == 255 && ((const char *)text)[0] == 'e' && ((const char *)text)[254] == 'e');
    else
        CHECK(!text && !count);
}

static int app_get_prop(void *context, MMI32 id, MMI32 *out)
{
    CHECK(((AppFixture *)context)->forgotten > 0 && (id == 5 || id == 6));
    *out = -450;
    return id == 5;
}

static int app_set_prop(void *context, MMI32 id, MMI32 value)
{
    CHECK(((AppFixture *)context)->forgotten > 0 && id == 7 && value == -22);
    return 0;
}

static int app_launch(void *context, const void *text, MMU32 count)
{
    CHECK(count == 3 && !memcmp(text, "toy", 3));
    ++((AppFixture *)context)->launches;
    return -3;
}

static void app_begin(void *context)
{
    ++((AppFixture *)context)->begins;
}

static void app_end(void *context, int all)
{
    CHECK(all == 1);
    ++((AppFixture *)context)->ends;
}

static int app_help(void *context, MMU32 topic, MMI32 *result)
{
    ++((AppFixture *)context)->helps;
    CHECK(topic == 27 || topic == 28);
    *result = 310;
    return topic == 27;
}

static void app_flush(void *context, MMU32 mask)
{
    ((AppFixture *)context)->events = mask;
}

static void app_message(void *context, int destination, const void *text, MMU32 count)
{
    AppFixture *app = context;
    int index = app->messages++;
    MMU32 i;
    CHECK(index < 3 && count <= MM_KIDSPACE_TEXT_LIMIT);
    app->destinations[index] = destination;
    if (app->text_unit == 1)
        memcpy(app->text[index], text, count);
    else
    {
        const unsigned short *wide = text;
        CHECK(count > 0 && wide[count - 1] == 0x263a);
        for (i = 0; i + 1 < count; ++i)
        {
            CHECK(wide[i] < 128);
            app->text[index][i] = (char)wide[i];
        }
        app->text[index][count - 1] = '!';
    }
    app->text[index][count] = 0;
}

static void app_forget(void *context)
{
    ++((AppFixture *)context)->forgotten;
}

static void app_notice(void *context, int reason, unsigned opcode, MMI32 id, MMU32 detail)
{
    AppFixture *app = context;
    CHECK(reason == MM_KIDSPACE_MISSING_EDIT || reason == MM_KIDSPACE_GET_PROPERTY_FAILED ||
          reason == MM_KIDSPACE_SET_PROPERTY_FAILED || reason == MM_KIDSPACE_LAUNCH_STRING_MISSING);
    if (reason == MM_KIDSPACE_MISSING_EDIT)
        CHECK(opcode == kopGetEdit && id == 3);
    else if (reason == MM_KIDSPACE_GET_PROPERTY_FAILED)
        CHECK(opcode == kopGetProp && id == 6);
    else if (reason == MM_KIDSPACE_SET_PROPERTY_FAILED)
        CHECK(opcode == kopSetProp && id == 7 && mm_signed_word(detail) == -22);
    else
        CHECK(opcode == kopLaunch && id == 99);
    app->failures |= 1 << (reason - MM_KIDSPACE_MISSING_EDIT);
}

static void execute_kidspace_app(void)
{
    static MMKidspaceAppServices app = {
        app_palette, app_set_palette, app_transition, app_modifiers, app_change_modifiers,
        app_is_edit, app_read_edit, app_write_edit, app_get_prop, app_set_prop, app_launch,
        app_begin, app_end, app_help, app_flush, app_message, 0
    };
    static const MMKidspaceScriptServices services = {
        app_object, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        app_notice, NULL, NULL, NULL, NULL, app_forget, &app
    };
    static const MMU32 graphics[] = {
        0x101d101dUL, VM_IMMEDIATES(1), 8, kopSetColorTable,
        VM_IMMEDIATES(1), 9, kopSetColorTable,
        VM_IMMEDIATES(1), 0xffffffffUL, kopSetColorTable,
        VM_IMMEDIATES(5), 8, 0x80230405UL, 61, 0xfffffffdUL, 4, kopTransition,
        VM_IMMEDIATES(5), 0xffffffffUL, 0x80230405UL, 0xffffffffUL, 0xfffffffdUL, 4, kopTransition,
        VM_IMMEDIATES(2), 0x04000000UL, 0x10, kopChangeModifierState, kopGetModifierState, kopReturn
    };
    static const MMU32 commands[] = {
        0x101d101dUL,
        VM_IMMEDIATES(2), 93, 2, kopGetEdit, VM_IMMEDIATES(2), 93, 2, kopSetEdit,
        VM_IMMEDIATES(2), 99, 2, kopSetEdit, VM_IMMEDIATES(2), 99, 3, kopGetEdit,
        VM_IMMEDIATES(1), 5, kopGetProp, VM_IMMEDIATES(1), 6, kopGetProp, kopAdd,
        VM_IMMEDIATES(2), 0xffffffeaUL, 7, kopSetProp,
        VM_IMMEDIATES(1), 91, kopLaunch, kopAdd, VM_IMMEDIATES(1), 99, kopLaunch, kopAdd,
        VM_IMMEDIATES(2), 71, 27, kopModalHelp, kopAdd,
        VM_IMMEDIATES(2), 0xfffffff7UL, 28, kopModalHelp, kopAdd,
        kopStartLongOp, VM_IMMEDIATES(1), 0x80000000UL, kopEndLongOp,
        VM_IMMEDIATES(1), 0xf0000001UL, kopFlushUserEvents, kopReturn
    };
    static const MMU32 messages[] = {
        0x101d101dUL, VM_IMMEDIATES(3), 0xfffffff7UL, 17, 2, kopAlert,
        VM_IMMEDIATES(4), 91, 99, 92, 3, kopPrintStr,
        VM_IMMEDIATES(3), 9, 17, 2, kopPrintStat, kopAdd, kopReturn
    };
    static const MMU32 statistics[] = {
        0x101d101dUL, VM_IMMEDIATES(2), 91, 1, kopPrintStrStat, kopExit
    };
    static const MMU32 malformed[] = {0x101d101dUL, VM_IMMEDIATES(1), 0x7fffffffUL, kopAlert, kopExit};
    static const MMU32 unused[] = {0x101d101dUL, kopUNUSED100, kopExit};
    static const unsigned short wide[] = {'W', 'i', 'd', 'e', 0x263a};
    const unsigned current = 0x101d, minimum = 0x1015;
    AppFixture fixture;
    MMScriptStrings registry;
    Host host;
    MMScriptVM vm;

    init(&host);
    memset(&fixture, 0, sizeof(fixture));
    fixture.host = &host;
    fixture.modifiers = 0x81234567UL;
    fixture.text_unit = 1;
    CHECK(mm_script_strings_init(&registry, 1, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_strings_put(&registry, 91, "toy", 3) && mm_script_strings_put(&registry, 92, "World", 5));
    CHECK(mm_script_vm_init(&vm, NULL, 1, &registry, NULL, &fixture) == MM_STORAGE_OK);
    vm.kidspace = &services;
    app.statistics_enabled = 0;

    CHECK(attach_versioned_program(&host, &vm, graphics, sizeof(graphics) / sizeof(*graphics), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && (MMU32)vm.result == 0x85234577UL);
    CHECK(fixture.loads == 3 && fixture.palettes == 1 && fixture.transitions == 2 && fixture.forgotten == 2);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
    CHECK(attach_versioned_program(&host, &vm, commands, sizeof(commands) / sizeof(*commands), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == -148 && !vm.stack.count);
    CHECK(fixture.edit_reads == 1 && fixture.edit_writes == 2 && fixture.launches == 1 && fixture.helps == 2);
    CHECK(fixture.failures == 15 && fixture.begins == 1 && fixture.ends == 1 && fixture.events == 0xf0000001UL);
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, messages, sizeof(messages) / sizeof(*messages), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && vm.result == 26 && !vm.stack.count && fixture.messages == 2);
    CHECK(fixture.destinations[0] == MM_KIDSPACE_ALERT &&
          !strcmp(fixture.text[0], "Script Message ('\001\001\001\001', 0x0, 6): 17 (0x11) -9 (0xFFFFFFF7) "));
    CHECK(fixture.destinations[1] == MM_KIDSPACE_PRINT && strstr(fixture.text[1], "): Worldtoy"));
    mm_script_vm_dispose(&vm);
    app.statistics_enabled = 1;
    CHECK(attach_versioned_program(&host, &vm, statistics, sizeof(statistics) / sizeof(*statistics), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && !vm.stack.count && fixture.messages == 3);
    CHECK(fixture.destinations[2] == MM_KIDSPACE_STATISTICS && strstr(fixture.text[2], "): toy"));
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, malformed, sizeof(malformed) / sizeof(*malformed), current, minimum));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_OPERAND && fixture.messages == 3);
    mm_script_vm_dispose(&vm);
    CHECK(attach_versioned_program(&host, &vm, unused, sizeof(unused) / sizeof(*unused), current, minimum));
    CHECK(!mm_script_vm_resume(&vm) && vm.error == MM_SCRIPT_ERROR_OPCODE);
    mm_script_vm_dispose(&vm);
    mm_script_strings_dispose(&registry);

    fixture.messages = 0;
    fixture.text_unit = 2;
    host.host.unit_bytes = 2;
    host.host.encoding = MM_TEXT_UTF16_LE;
    CHECK(mm_script_strings_init(&registry, 2, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_strings_put(&registry, 91, wide, sizeof(wide) / sizeof(*wide)));
    CHECK(attach_versioned_program(&host, &vm, statistics, sizeof(statistics) / sizeof(*statistics), current, minimum));
    CHECK(mm_script_vm_resume(&vm) && fixture.messages == 1 && !vm.stack.count);
    CHECK(fixture.destinations[0] == MM_KIDSPACE_STATISTICS && strstr(fixture.text[0], "): Wide!"));
    mm_script_vm_dispose(&vm);
    mm_script_strings_dispose(&registry);
    CHECK(!host.collections_live);
}

typedef struct CompileTrace
{
    int errors[4], count;
} CompileTrace;

static void compile_diagnostic(void *context, int error)
{
    CompileTrace *trace = context;
    CHECK(trace->count < 4);
    trace->errors[trace->count++] = error;
}

static void emit_script(void)
{
    Host host;
    MMScriptEmitter *emitter;
    MMCompiledScript *script;
    const void *text;
    MMU32 units, i;
    CompileTrace trace;
    FixtureAllocator failure;
    MMStorageAllocator allocator;

    init(&host);
    memset(&trace, 0, sizeof(trace));
    emitter = mm_script_emitter_create(&host.host, 10, 9, compile_diagnostic, &trace);
    CHECK(emitter);
    mm_script_emit_label_reference(emitter, "exit", 4);
    mm_script_emit_opcode(emitter, kopGo);
    mm_script_emit_label(emitter, "body", 4);
    mm_script_emit_variable(emitter, kopPushLocVar, 0xfe7d, 0x25db1000UL);
    for (i = 0; i < 255; ++i)
        mm_script_emit_immediate(emitter, (MMI32)i);
    mm_script_emit_label(emitter, "Exit", 4);
    mm_script_emit_literal(emitter, "x", 1);
    mm_script_emit_label(emitter, "exit", 4);
    mm_script_emit_immediate(emitter, 17);
    mm_script_emit_label_reference(emitter, "Exit", 4);
    mm_script_emit_opcode(emitter, kopReturn);
    script = mm_script_emitter_finish(emitter);
    mm_script_emitter_destroy(emitter);
    CHECK(script && !trace.count && host.collections_live == 1 && mm_script_word_count(script) == 268);
    CHECK(mm_script_word(script, 0) == 0x000a0009UL && mm_script_word(script, 1) == VM_IMMEDIATES(1));
    CHECK((MMU32)mm_script_word(script, 2) == 0xcc000108UL && mm_script_word(script, 3) == kopGo);
    CHECK(mm_script_word(script, 4) == 0x01fffe7dUL && mm_script_word(script, 5) == 0x25db1000UL);
    for (i = 0; i < 254; ++i)
        CHECK(mm_script_word(script, 6 + i) == (MMI32)i);
    CHECK(mm_script_word(script, 260) == VM_IMMEDIATES(1) && mm_script_word(script, 261) == 254);
    CHECK(mm_script_word(script, 262) == 0x01010000UL && mm_script_word(script, 263) == 0);
    CHECK(mm_script_word(script, 264) == VM_IMMEDIATES(2) && mm_script_word(script, 265) == 17);
    CHECK((MMU32)mm_script_word(script, 266) == 0xcc000106UL && mm_script_word(script, 267) == kopReturn);
    CHECK(mm_script_literal(script, 0, &text, &units) && units == 1 && !memcmp(text, "x", 1));
    mm_script_release(script);

    emitter = mm_script_emitter_create(&host.host, 10, 10, compile_diagnostic, &trace);
    CHECK(emitter);
    mm_script_emit_label_reference(emitter, "missing", 7);
    mm_script_emit_label_reference(emitter, "same", 4);
    mm_script_emit_label(emitter, "same", 4);
    mm_script_emit_label(emitter, "same", 4);
    CHECK(!mm_script_emitter_finish(emitter) && trace.count == 2);
    CHECK(trace.errors[0] == MM_SCRIPT_COMPILE_DUPLICATE_LABEL && trace.errors[1] == MM_SCRIPT_COMPILE_UNDEFINED_LABEL);
    mm_script_emitter_destroy(emitter);
    CHECK(!host.collections_live);

    memset(&failure, 0, sizeof(failure));
    allocator.context = &failure;
    allocator.resize = fixture_resize;
    allocator.release = fixture_release;
    failure.speculate = 1;
    host.host.storage = &allocator;
    emitter = mm_script_emitter_create(&host.host, 10, 10, compile_diagnostic, &trace);
    CHECK(emitter);
    failure.fail = 1;
    mm_script_emit_label_reference(emitter, "short", 5);
    CHECK(!mm_script_emitter_finish(emitter) && trace.count == 3 && trace.errors[2] == MM_SCRIPT_COMPILE_MEMORY);
    mm_script_emitter_destroy(emitter);
    CHECK(!host.collections_live);
}

typedef struct PostfixSource
{
    const MMScriptToken *tokens;
    MMU32 next;
} PostfixSource;

static int postfix_token(void *context, MMScriptToken *out)
{
    PostfixSource *input = context;
    *out = input->tokens[input->next++];
    return out->kind != ttNil;
}

static MMU32 postfix_opcode(void *context, const void *name, MMU32 length)
{
    (void)context;
    if (length == 2 && !memcmp(name, "Go", 2))
        return kopGo;
    if (length == 6 && !memcmp(name, "Return", 6))
        return kopReturn;
    return 0;
}

static void compile_postfix(void)
{
    static const MMScriptToken tokens[] = {
        {ttSub, 0, NULL, 0}, {ttLong, (-2147483647 - 1), NULL, 0},
        {ttGt, 0, NULL, 0}, {ttName, 0, "value", 5},
        {ttDollar, 0, NULL, 0}, {ttName, 0, "done", 4}, {ttName, 0, "Go", 2},
        {ttLong, 99, NULL, 0}, {ttName, 0, "Return", 6},
        {ttAt, 0, NULL, 0}, {ttName, 0, "done", 4},
        {ttLt, 0, NULL, 0}, {ttName, 0, "value", 5}, {ttName, 0, "Return", 6}, {ttNil, 0, NULL, 0}
    };
    MMScriptPostfixInput input;
    PostfixSource source;
    CompileTrace trace;
    Host host;
    MMScriptEmitter *emitter;
    MMCompiledScript *script;
    MMScriptVM vm;

    init(&host);
    memset(&trace, 0, sizeof(trace));
    source.tokens = tokens;
    source.next = 0;
    input.context = &source;
    input.next = postfix_token;
    input.opcode = postfix_opcode;
    emitter = mm_script_emitter_create(&host.host, 10, 10, compile_diagnostic, &trace);
    CHECK(emitter);
    mm_script_compile_postfix(emitter, &input);
    script = mm_script_emitter_finish(emitter);
    mm_script_emitter_destroy(emitter);
    CHECK(script && !trace.count && source.next == sizeof(tokens) / sizeof(*tokens));
    CHECK(mm_script_vm_init(&vm, NULL, 1, NULL, NULL, NULL) == MM_STORAGE_OK);
    CHECK(mm_script_vm_attach(&vm, script, 10, 10, NULL, 0));
    mm_script_release(script);
    CHECK(mm_script_vm_resume(&vm) && (MMU32)vm.result == 0x80000000UL);
    mm_script_vm_dispose(&vm);
    CHECK(!host.collections_live);
}

int main(void)
{
    scripts();
    collections();
    runtime();
    execute_scripts();
    runtime_strings();
    execute_string_scripts();
    random_decks();
    execute_random_scripts();
    execute_kidspace_scripts();
    execute_timed_scripts();
    execute_kidspace_objects();
    execute_kidspace_app();
    emit_script();
    compile_postfix();
    puts("C89 script resources and bytecode execution passed");
    return 0;
}
