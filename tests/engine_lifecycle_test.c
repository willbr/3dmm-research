#include "three_mm/chunk_lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "lifecycle failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Options
{
    ThreeMMChunkOptions initial, *current;
    ThreeMMU32 flags, wanted, mask;
    int fail, sets, reads;
} Options;
static ThreeMMU32 flags(void *c)
{
    return ((Options *)c)->flags;
}
static int set_flags(void *c, ThreeMMU32 value, ThreeMMU32 mask)
{
    Options *o = (Options *)c;
    CHECK(!memcmp(o->current, &o->initial, sizeof(o->initial)));
    ++o->sets;
    o->wanted = value;
    o->mask = mask;
    return !o->fail;
}
static void read_map(void *c)
{
    Options *o = (Options *)c;
    CHECK(!memcmp(o->current, &o->initial, sizeof(o->initial)));
    ++o->reads;
}
static void options(void)
{
    static const ThreeMMU32 mapping[] = {2, 7, 18, 23};
    Options o;
    ThreeMMChunkOptions state, expected;
    ThreeMMChunkOptionServices services;
    unsigned initial, value, mask, current;
    int fail, change, failed;
    ThreeMMU32 requested;
    services.context = &o;
    services.file_flags = flags;
    services.set_file_flags = set_flags;
    services.read_map = read_map;
    for (value = 0; value < 256; ++value)
        CHECK(three_mm_chunk_file_flags(value) == mapping[value & 3]);
    for (initial = 0; initial < 32; ++initial)
        for (value = 0; value < 32; ++value)
            for (mask = 0; mask < 32; ++mask)
                for (current = 0; current < 4; ++current)
                    for (fail = 0; fail < 2; ++fail)
                    {
                        memset(&o, 0, sizeof(o));
                        state.marked = (initial & 1) != 0;
                        state.add_to_extra = (initial & 2) != 0;
                        state.read_from_extra = (initial & 4) != 0;
                        state.free_map_unread = (initial & 8) != 0;
                        state.invalid_main = (initial & 16) != 0;
                        o.initial = state;
                        o.current = &state;
                        o.flags = mapping[current];
                        o.fail = fail;
                        requested = value & mask;
                        change = ((mapping[requested & 3] ^ o.flags) & mapping[mask & 3]) != 0;
                        failed = change && (state.invalid_main || fail);
                        expected = state;
                        if (!failed)
                        {
                            if (requested & 4)
                                expected.marked = 1;
                            if ((requested & 8) || ((mask & 1) && !(requested & 1)))
                                expected.add_to_extra = 1;
                            if (requested & 16)
                                expected.read_from_extra = 1;
                            if (requested & 1)
                                expected.free_map_unread = 0;
                        }
                        CHECK(three_mm_chunk_options_set(&state, &services, value, mask) ==
                              (failed ? THREE_MM_CHUNK_IO : THREE_MM_CHUNK_OK));
                        CHECK(!memcmp(&state, &expected, sizeof(state)) &&
                              o.sets == (change && !o.initial.invalid_main));
                        CHECK(o.reads == (!failed && (requested & 1) && o.initial.free_map_unread));
                        if (o.sets)
                            CHECK(o.wanted == mapping[requested & 3] && o.mask == mapping[mask & 3]);
                    }
}
typedef struct Registry Registry;
typedef struct Link
{
    struct Link *next, *previous;
    Registry *owner;
    int mark, refs, dead, id;
} Link;
struct Registry
{
    Link *first;
    int destroyed;
};
static void *next(void *p)
{
    Link *n = (Link *)p;
    CHECK(!n->dead);
    return n->next;
}
static int marked(void *p)
{
    Link *n = (Link *)p;
    CHECK(!n->dead);
    return n->mark;
}
static ThreeMMI32 references(void *p)
{
    Link *n = (Link *)p;
    CHECK(!n->dead);
    return n->refs;
}
static void clear(void *p)
{
    Link *n = (Link *)p;
    CHECK(!n->dead);
    n->mark = 0;
}
static void destroy(void *p)
{
    Link *n = (Link *)p;
    CHECK(!n->dead && !n->refs && !n->mark);
    if (n->previous)
        n->previous->next = n->next;
    else
        n->owner->first = n->next;
    if (n->next)
        n->next->previous = n->previous;
    n->dead = 1;
    n->next = (Link *)1;
    ++n->owner->destroyed;
}
static void registry(void)
{
    Link nodes[4];
    Registry r;
    ThreeMMChunkRegistry services;
    ThreeMMI32 remaining;
    int i;
    memset(nodes, 0, sizeof(nodes));
    memset(&r, 0, sizeof(r));
    r.first = nodes;
    services.next = next;
    services.marked = marked;
    services.references = references;
    services.clear_mark = clear;
    services.destroy = destroy;
    for (i = 0; i < 4; ++i)
    {
        nodes[i].owner = &r;
        nodes[i].id = i;
        if (i)
            nodes[i].previous = nodes + i - 1;
        if (i < 3)
            nodes[i].next = nodes + i + 1;
    }
    nodes[1].mark = 1;
    nodes[2].refs = 1;
    CHECK(three_mm_chunk_close_unmarked(r.first, &services) == THREE_MM_CHUNK_OK && r.destroyed == 2 &&
          r.first == nodes + 1);
    CHECK(three_mm_chunk_clear_marks(r.first, &services) == THREE_MM_CHUNK_OK && !nodes[1].mark);
    CHECK(three_mm_chunk_close_unmarked(r.first, &services) == THREE_MM_CHUNK_OK && r.destroyed == 3 &&
          r.first == nodes + 2);
    nodes[2].refs = 0;
    CHECK(three_mm_chunk_close_unmarked(r.first, &services) == THREE_MM_CHUNK_OK && r.destroyed == 4 && !r.first);
    remaining = 99;
    CHECK(three_mm_chunk_release(0, 0, &remaining) == -1 && remaining == 99);
    CHECK(three_mm_chunk_release(-1, 1, &remaining) == -1 && remaining == 99);
    CHECK(three_mm_chunk_release(1, 0, &remaining) == 1 && !remaining);
    CHECK(three_mm_chunk_release(1, 1, &remaining) == 0 && !remaining);
    CHECK(three_mm_chunk_release(0x7fffffffL, 0, &remaining) == 0 && remaining == 0x7ffffffeL);
}
typedef struct Token
{
    int refs, id;
} Token;
typedef struct Host
{
    Token main_file, extra_file, old_index, new_index;
    ThreeMMChunkReopenState *current, mirror;
    ThreeMMChunkRuntime *runtime;
    ThreeMMStorageAllocator allocator;
    int live, read_ok, valid, writable, reads, validations, publications, deferred;
    char log[32];
    int logs;
} Host;
static void log_event(Host *h, char ch)
{
    CHECK(h->logs < 31);
    h->log[h->logs++] = ch;
    h->log[h->logs] = 0;
}
static void *resize(void *c, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Host *h = (Host *)c;
    void *p;
    (void)before;
    (void)priority;
    p = realloc(old, bytes);
    CHECK(p != NULL);
    if (!old)
        ++h->live;
    return p;
}
static void release(void *c, void *p, ThreeMMU32 bytes)
{
    Host *h = (Host *)c;
    (void)bytes;
    CHECK(h->live > 0);
    --h->live;
    free(p);
}
static int writable(void *c, void *file)
{
    Host *h = (Host *)c;
    CHECK(file == &h->main_file);
    return h->writable;
}
static void retain_file(void *c, void *file)
{
    Host *h = (Host *)c;
    CHECK(file == &h->main_file);
    ++((Token *)file)->refs;
    log_event(h, 'R');
}
static void mirrored(Host *h)
{
    CHECK(h->mirror.index == h->current->index && h->mirror.main_file == h->current->main_file &&
          h->mirror.extra_file == h->current->extra_file && h->mirror.main_map == h->current->main_map &&
          h->mirror.extra_map == h->current->extra_map);
}
static void release_file(void *c, void *file)
{
    Host *h = (Host *)c;
    mirrored(h);
    CHECK(((Token *)file)->refs > 0);
    --((Token *)file)->refs;
    log_event(h, file == &h->main_file ? 'M' : 'E');
}
static void release_index(void *c, void *index)
{
    Host *h = (Host *)c;
    mirrored(h);
    CHECK(((Token *)index)->refs == 1);
    --((Token *)index)->refs;
    log_event(h, 'I');
}
static void publish(void *c, const ThreeMMChunkReopenState *state)
{
    Host *h = (Host *)c;
    h->mirror = *state;
    ++h->publications;
    log_event(h, 'P');
}
static int read_index(void *c, ThreeMMChunkReopenState *state)
{
    Host *h = (Host *)c;
    CHECK(h->publications == 1 && !h->mirror.index && !h->mirror.extra_file && !h->mirror.main_map &&
          !h->mirror.extra_map && !h->mirror.free_map_unread && h->main_file.refs == 2);
    if (h->runtime)
        CHECK(!h->runtime->count);
    ++h->reads;
    log_event(h, 'D');
    state->index = &h->new_index;
    h->new_index.refs = 1;
    state->main_end = 777;
    CHECK(three_mm_free_map_create(&h->allocator, 1, &state->main_map) == THREE_MM_CHUNK_OK);
    if (h->runtime)
        CHECK(three_mm_chunk_runtime_set(h->runtime, 9, 9, 77) == THREE_MM_CHUNK_OK);
    h->deferred = 99;
    h->mirror = *state;
    return h->read_ok;
}
static int validate(void *c)
{
    Host *h = (Host *)c;
    CHECK(h->mirror.index == &h->new_index);
    ++h->validations;
    log_event(h, 'V');
    return h->valid;
}
static void reopen_cases(void)
{
    Host h;
    ThreeMMChunkReopenState state, original;
    ThreeMMChunkReopenServices services;
    ThreeMMChunkRuntime runtime;
    ThreeMMChunkFreeRange range;
    int mode, use_runtime, result;
    for (use_runtime = 0; use_runtime < 2; ++use_runtime)
        for (mode = 0; mode < 6; ++mode)
        {
            if (!use_runtime && mode == 5)
                continue;
            memset(&h, 0, sizeof(h));
            memset(&state, 0, sizeof(state));
            h.allocator.context = &h;
            h.allocator.resize = resize;
            h.allocator.release = release;
            h.main_file.refs = h.extra_file.refs = h.old_index.refs = 1;
            h.read_ok = mode != 1;
            h.valid = mode != 2;
            h.writable = mode == 3;
            state.main_file = &h.main_file;
            state.extra_file = &h.extra_file;
            state.index = &h.old_index;
            state.main_end = 200;
            state.extra_end = 66;
            state.free_map_unread = 1;
            state.invalid_main = mode == 4;
            CHECK(three_mm_free_map_create(&h.allocator, 1, &state.extra_map) == THREE_MM_CHUNK_OK);
            range.offset = 7;
            range.bytes = 3;
            CHECK(three_mm_free_map_insert(state.extra_map, 0, &range) == THREE_MM_CHUNK_OK);
            CHECK(three_mm_chunk_runtime_init(&runtime, &h.allocator) == THREE_MM_CHUNK_OK);
            if (use_runtime)
            {
                CHECK(three_mm_chunk_runtime_set(&runtime, 1, 1, 11) == THREE_MM_CHUNK_OK);
                h.runtime = &runtime;
            }
            if (mode == 5)
                runtime.storage.buffers[0].locks = 1;
            original = state;
            h.current = &state;
            h.mirror = state;
            services.context = &h;
            services.writable = writable;
            services.retain_file = retain_file;
            services.release_file = release_file;
            services.release_index = release_index;
            services.read_index = read_index;
            services.validate = validate;
            services.publish = publish;
            result = three_mm_chunk_reopen(&state, h.runtime, &services);
            CHECK(result == (mode == 0 ? THREE_MM_CHUNK_OK : mode < 3 ? THREE_MM_CHUNK_IO : THREE_MM_CHUNK_BAD_FILE));
            if (mode == 0)
            {
                CHECK(state.index == &h.new_index && !state.extra_file && !state.extra_map && state.main_map &&
                      state.main_end == 777 && !state.free_map_unread);
                CHECK(h.main_file.refs == 1 && !h.extra_file.refs && !h.old_index.refs && h.new_index.refs == 1 &&
                      !strcmp(h.log, "RPDVPIME"));
                if (use_runtime)
                    CHECK(!three_mm_chunk_runtime_get(&runtime, 1, 1) &&
                          three_mm_chunk_runtime_get(&runtime, 9, 9) == 77);
            }
            else
            {
                CHECK(state.index == original.index && state.extra_map == original.extra_map && !state.main_map &&
                      state.main_end == 200 && state.extra_end == 66 && state.free_map_unread);
                CHECK(h.main_file.refs == 1 && h.extra_file.refs == 1 && h.old_index.refs == 1 && !h.new_index.refs);
                CHECK(three_mm_free_map_get(state.extra_map, 0, &range) == THREE_MM_CHUNK_OK && range.offset == 7 &&
                      range.bytes == 3);
                if (mode < 3)
                    CHECK(!strcmp(h.log, mode == 1 ? "RPDPIM" : "RPDVPIM") && h.deferred == 99);
                else
                    CHECK(!h.reads && !h.publications && !h.logs);
                if (use_runtime)
                    CHECK(three_mm_chunk_runtime_get(&runtime, 1, 1) == 11 &&
                          !three_mm_chunk_runtime_get(&runtime, 9, 9));
            }
            if (mode < 3)
                CHECK(h.reads == 1 && h.validations == (mode != 1) && h.publications == 2);
            runtime.storage.buffers[0].locks = 0;
            --((Token *)state.main_file)->refs;
            if (state.extra_file)
                --((Token *)state.extra_file)->refs;
            --((Token *)state.index)->refs;
            three_mm_free_map_close(state.main_map);
            three_mm_free_map_close(state.extra_map);
            CHECK(three_mm_chunk_runtime_dispose(&runtime) == THREE_MM_CHUNK_OK);
            CHECK(!h.live && !h.main_file.refs && !h.extra_file.refs && !h.old_index.refs && !h.new_index.refs);
        }
}
int main(void)
{
    options();
    registry();
    reopen_cases();
    return 0;
}