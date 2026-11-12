#include "three_mm/chunk_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "chunk owner failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Token
{
    int refs, error;
    char name;
} Token;
typedef struct Host
{
    ThreeMMChunkOwner owner;
    Token main, extra, index, replacement;
    ThreeMMU32 flags;
    int fail, reads, validates, publishes, sets;
    char log[128];
    int used;
} Host;
static char log_buffer[64];
static int log_used;
static void log_error(char c)
{
    CHECK(log_used < 63);
    log_buffer[log_used++] = c;
    log_buffer[log_used] = 0;
}
static ThreeMMI32 get_error(void *p)
{
    Token *t = (Token *)p;
    log_error(t->name);
    return t->error;
}
static void set_error(void *p, ThreeMMI32 level)
{
    Token *t = (Token *)p;
    log_error((char)(t->name + ('a' - 'A')));
    t->error = level;
}
static void event(Host *h, char c)
{
    CHECK(h->used < 127);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void retain_file(void *c, void *p)
{
    Host *h = (Host *)c;
    CHECK(p == &h->main && h->main.refs == 1);
    ++h->main.refs;
    event(h, '+');
}
static void release_file(void *c, void *p)
{
    Host *h = (Host *)c;
    Token *t = (Token *)p;
    CHECK(t->refs > 0);
    --t->refs;
    event(h, t->name);
}
static void release_index(void *c, void *p)
{
    Host *h = (Host *)c;
    Token *t = (Token *)p;
    CHECK(t->refs == 1);
    --t->refs;
    event(h, t->name);
}
static void publish(void *c, const ThreeMMChunkReopenState *s)
{
    Host *h = (Host *)c;
    CHECK(s == &h->owner.files);
    ++h->publishes;
    event(h, 'P');
}
static int writable(void *c, void *p)
{
    Host *h = (Host *)c;
    CHECK(p == &h->main);
    event(h, 'W');
    return h->fail == 3;
}
static int read_index(void *c, ThreeMMChunkReopenState *s)
{
    Host *h = (Host *)c;
    CHECK(s == &h->owner.files && s->main_file == &h->main && h->main.refs == 2);
    CHECK(!s->index && !s->main_map && !s->extra_file && !s->main_end && !h->owner.runtime.count);
    ++h->reads;
    event(h, 'R');
    s->index = &h->replacement;
    h->replacement.refs = 1;
    s->extra_file = &h->extra;
    h->extra.refs = 1;
    s->main_end = 333;
    s->extra_end = 44;
    CHECK(three_mm_free_map_create(NULL, 1, &s->main_map) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_free_map_create(NULL, 1, &s->extra_map) == THREE_MM_CHUNK_OK);
    if (!h->owner.expanded_runtime)
        CHECK(three_mm_chunk_runtime_set(&h->owner.runtime, 1, 1, 22) == THREE_MM_CHUNK_OK);
    h->owner.map_offset = 888;
    h->owner.map_bytes = 16;
    return h->fail != 1;
}
static int validate(void *c)
{
    Host *h = (Host *)c;
    CHECK(h->owner.files.index == &h->replacement && h->owner.files.main_end == 333);
    ++h->validates;
    event(h, 'V');
    return h->fail != 2;
}
static ThreeMMU32 file_flags(void *c)
{
    return ((Host *)c)->flags;
}
static int set_flags(void *c, ThreeMMU32 flags, ThreeMMU32 mask)
{
    Host *h = (Host *)c;
    CHECK(!h->owner.marked && !h->owner.add_to_extra && !h->owner.read_from_extra);
    ++h->sets;
    event(h, 'S');
    if (h->fail)
        return 0;
    h->flags = (h->flags & ~mask) | (flags & mask);
    return 1;
}
static void read_map(void *c)
{
    Host *h = (Host *)c;
    CHECK(h->owner.files.free_map_unread && !h->owner.marked && !h->owner.add_to_extra && !h->owner.read_from_extra);
    h->owner.files.free_map_unread = 0;
    event(h, 'L');
}
static void init(Host *h, int expanded)
{
    memset(h, 0, sizeof(*h));
    h->main.name = 'M';
    h->extra.name = 'E';
    h->index.name = 'I';
    h->replacement.name = 'J';
    CHECK(three_mm_chunk_owner_init(&h->owner, NULL, expanded) == THREE_MM_CHUNK_OK);
}
static void dispose(Host *h)
{
    ThreeMMChunkDisposal s;
    s.context = h;
    s.release_file = release_file;
    s.release_index = release_index;
    s.publish = publish;
    CHECK(three_mm_chunk_owner_dispose(&h->owner, &s) == THREE_MM_CHUNK_OK);
    CHECK(!h->main.refs && !h->extra.refs && !h->index.refs && !h->replacement.refs);
    CHECK(!h->owner.files.main_file && !h->owner.files.extra_file && !h->owner.files.index && !h->owner.runtime.count);
    CHECK(three_mm_chunk_owner_dispose(&h->owner, &s) == THREE_MM_CHUNK_OK);
}
static void reopen_cases(void)
{
    Host h;
    ThreeMMChunkReopenServices s;
    ThreeMMChunkFreeMap *old_map;
    int expanded, fail, result;
    for (expanded = 0; expanded < 2; ++expanded)
    {
        for (fail = 0; fail < 5; ++fail)
        {
            init(&h, expanded);
            h.fail = fail;
            h.owner.files.main_file = &h.main;
            h.main.refs = 1;
            h.owner.files.index = &h.index;
            h.index.refs = 1;
            h.owner.files.main_end = 200;
            h.owner.files.free_map_unread = 1;
            h.owner.files.invalid_main = fail == 4;
            h.owner.marked = h.owner.add_to_extra = h.owner.read_from_extra = 1;
            h.owner.map_offset = 111;
            h.owner.map_bytes = 8;
            CHECK(three_mm_free_map_create(NULL, 1, &h.owner.files.main_map) == THREE_MM_CHUNK_OK);
            old_map = h.owner.files.main_map;
            if (!expanded)
                CHECK(three_mm_chunk_runtime_set(&h.owner.runtime, 1, 1, 11) == THREE_MM_CHUNK_OK);
            s.context = &h;
            s.writable = writable;
            s.retain_file = retain_file;
            s.release_file = release_file;
            s.release_index = release_index;
            s.read_index = read_index;
            s.validate = validate;
            s.publish = publish;
            result = three_mm_chunk_owner_reopen(&h.owner, &s);
            CHECK(result == (!fail ? THREE_MM_CHUNK_OK : fail >= 3 ? THREE_MM_CHUNK_BAD_FILE : THREE_MM_CHUNK_IO));
            CHECK(h.owner.marked && h.owner.add_to_extra && h.owner.read_from_extra && h.main.refs == 1);
            if (fail)
            {
                CHECK(h.owner.files.index == &h.index && h.owner.files.main_map == old_map &&
                      h.owner.files.main_end == 200);
                CHECK(h.owner.files.free_map_unread && !h.replacement.refs && !h.extra.refs);
                if (!expanded)
                    CHECK(three_mm_chunk_runtime_get(&h.owner.runtime, 1, 1) == 11);
            }
            else
            {
                CHECK(h.owner.files.index == &h.replacement && h.owner.files.main_map != old_map && !h.index.refs);
                CHECK(h.owner.files.main_end == 333 && h.owner.files.extra_end == 44);
                if (!expanded)
                    CHECK(three_mm_chunk_runtime_get(&h.owner.runtime, 1, 1) == 22);
            }
            CHECK(h.owner.map_offset == (fail >= 3 ? 111UL : 888UL));
            CHECK(h.owner.map_bytes == (fail >= 3 ? 8UL : 16UL));
            CHECK(h.reads == (fail < 3) && h.validates == (fail < 3 && fail != 1));
            if (fail >= 3)
                CHECK(!h.publishes);
            else
                CHECK(h.publishes == 2 && !strncmp(h.log, "W+PR", 4));
            dispose(&h);
        }
    }
}
static void policy_cases(void)
{
    Host h;
    ThreeMMChunkOptionServices s;
    ThreeMMChunkSaveState saved;
    ThreeMMChunkStoragePolicy storage;
    ThreeMMChunkFreeMap *map;
    int fail;
    for (fail = 0; fail < 2; ++fail)
    {
        init(&h, 0);
        h.fail = fail;
        h.owner.files.main_file = &h.main;
        h.main.refs = 1;
        h.owner.files.index = &h.index;
        h.index.refs = 1;
        h.owner.files.free_map_unread = 1;
        s.context = &h;
        s.file_flags = file_flags;
        s.set_file_flags = set_flags;
        s.read_map = read_map;
        CHECK(three_mm_chunk_owner_options(&h.owner, &s, 1 | 4 | 8 | 16, 31) ==
              (fail ? THREE_MM_CHUNK_IO : THREE_MM_CHUNK_OK));
        CHECK(h.owner.marked == !fail && h.owner.add_to_extra == !fail && h.owner.read_from_extra == !fail);
        CHECK(h.owner.files.free_map_unread == fail && !strcmp(h.log, fail ? "S" : "SL"));
        dispose(&h);
    }
    init(&h, 0);
    h.owner.marked = h.owner.read_from_extra = 1;
    h.owner.map_offset = 100;
    h.owner.map_bytes = 32;
    h.owner.files.main_file = &h.main;
    h.main.refs = 1;
    h.owner.files.index = &h.index;
    h.index.refs = 1;
    CHECK(three_mm_free_map_create(NULL, 1, &map) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_set(&h.owner.runtime, 2, 3, 99) == THREE_MM_CHUNK_OK);
    saved = three_mm_chunk_owner_save_state(&h.owner);
    CHECK(saved.main_file == &h.main && !saved.extra_file && !saved.main_map && !saved.add_to_extra);
    saved.main_map = map;
    saved.main_end = 400;
    saved.invalid_main = saved.add_to_extra = saved.free_map_unread = 7;
    three_mm_chunk_owner_publish_save(&h.owner, &saved);
    CHECK(h.owner.files.main_map == map && h.owner.files.main_end == 400 && h.owner.files.index == &h.index);
    CHECK(h.owner.files.invalid_main == 1 && h.owner.files.free_map_unread == 1 && h.owner.add_to_extra == 1);
    CHECK(h.owner.marked && h.owner.read_from_extra && h.owner.map_offset == 100 && h.owner.map_bytes == 32);
    CHECK(three_mm_chunk_runtime_get(&h.owner.runtime, 2, 3) == 99);
    storage = three_mm_chunk_owner_storage_policy(&h.owner, 1234);
    CHECK(storage.max_bytes == 1234 && storage.add_to_extra && storage.read_from_extra && storage.free_map_unread);
    h.owner.runtime.storage.buffers[0].locks = 1;
    {
        ThreeMMChunkDisposal d;
        d.context = &h;
        d.release_file = release_file;
        d.release_index = release_index;
        d.publish = publish;
        CHECK(three_mm_chunk_owner_dispose(&h.owner, &d) == THREE_MM_CHUNK_BAD_FILE && !h.publishes &&
              h.main.refs == 1);
    }
    h.owner.runtime.storage.buffers[0].locks = 0;
    dispose(&h);
}
static void error_cases(void)
{
    Host h;
    ThreeMMChunkErrorServices s;
    int a, b, level;
    s.get = get_error;
    s.set = set_error;
    init(&h, 0);
    h.owner.files.main_file = &h.main;
    h.main.refs = 1;
    h.owner.files.extra_file = &h.extra;
    h.extra.refs = 1;
    for (a = 0; a < 10; ++a)
        for (b = 0; b < 10; ++b)
            for (level = 0; level < 10; ++level)
            {
                h.main.error = a;
                h.extra.error = b;
                log_used = 0;
                CHECK(three_mm_chunk_owner_error(&h.owner, &s) == (a > b ? a : b) && !strcmp(log_buffer, "ME"));
                log_used = 0;
                three_mm_chunk_owner_reset_error(&h.owner, &s, level);
                CHECK(h.main.error == (a > level ? level : a) && h.extra.error == (b > level ? level : b));
                CHECK(!strcmp(log_buffer, a > level ? (b > level ? "MmEe" : "MmE") : b > level ? "MEe" : "ME"));
            }
    h.owner.files.extra_file = NULL;
    h.extra.refs = 0;
    h.main.error = 4;
    log_used = 0;
    CHECK(three_mm_chunk_owner_error(&h.owner, &s) == 4 && !strcmp(log_buffer, "M"));
    log_used = 0;
    three_mm_chunk_owner_reset_error(&h.owner, &s, 3);
    CHECK(h.main.error == 3 && !strcmp(log_buffer, "Mm"));
    dispose(&h);
}
int main(void)
{
    reopen_cases();
    policy_cases();
    error_cases();
    puts("chunk owner C89 checks passed");
    return 0;
}
