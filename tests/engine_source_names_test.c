#include "three_mm/source_names.h"
#include "three_mm/source_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "source names failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Memory
{
    int fail, live;
} Memory;
static void *resize(void *c, void *p, ThreeMMU32 old, ThreeMMU32 bytes, int priority)
{
    Memory *m = (Memory *)c;
    void *result;
    (void)old;
    (void)priority;
    if (m->fail)
        return NULL;
    result = realloc(p, bytes);
    if (result && !p)
        ++m->live;
    return result;
}
static void release(void *c, void *p, ThreeMMU32 bytes)
{
    (void)bytes;
    --((Memory *)c)->live;
    free(p);
}
static int equal(void *c, const void *a, ThreeMMU32 ac, const void *b, ThreeMMU32 bc)
{
    ThreeMMU32 i;
    (void)c;
    if (ac != bc)
        return 0;
    for (i = 0; i < ac; ++i)
        if (toupper(((const unsigned char *)a)[i]) != toupper(((const unsigned char *)b)[i]))
            return 0;
    return 1;
}
typedef struct Output
{
    unsigned char bytes[4096];
    int calls, fail;
} Output;
static int write_at(void *c, ThreeMMU32 offset, const void *data, size_t size)
{
    Output *o = (Output *)c;
    ++o->calls;
    if (o->calls == o->fail)
        return 0;
    CHECK(offset + size <= sizeof(o->bytes));
    memcpy(o->bytes + offset, data, size);
    return 1;
}
static int provider(void *c, const void *long_name, ThreeMMU32 lc, const void *short_name, ThreeMMU32 sc, void *path)
{
    ThreeMMSourceNames *n = (ThreeMMSourceNames *)c;
    int i;
    char large[255];
    CHECK(lc == 4 && sc == 5 && path == n);
    memset(large, 'x', sizeof(large));
    for (i = 10; i < 100; ++i)
        CHECK(three_mm_source_names_add(n, i, large, 255));
    CHECK(memcmp(long_name, "Long", 4) == 0 && memcmp(short_name, "SHORT", 5) == 0);
    return THREE_MM_SOURCE_AVAILABLE;
}
typedef struct Input
{
    Memory *memory;
    int calls, diagnostics, swapped;
} Input;
static void get(void *c, ThreeMMU32 i, const void **text, ThreeMMU32 *count, ThreeMMI32 *sid)
{
    static const char large[255] = {0};
    Input *in = (Input *)c;
    ++in->calls;
    *text = "Merged";
    *count = 6;
    *sid = in->swapped ? 0x03000000 : (ThreeMMI32)(20 + i);
    if (i == 1 && in->memory)
    {
        in->memory->fail = 1;
        *count = 255;
        *text = large;
    }
}
static void diagnostic(void *c, ThreeMMI32 sid)
{
    CHECK(sid < 0);
    ++((Input *)c)->diagnostics;
}
static void names(void)
{
    ThreeMMSourceNames n, wide;
    Memory m = {0, 0};
    ThreeMMStorageAllocator allocator = {NULL, resize, release};
    ThreeMMU32 count, position;
    ThreeMMI32 sid;
    const void *text;
    ThreeMMSourceNameParts p;
    Output output;
    ThreeMMStorageWriter writer;
    unsigned char before[64];
    unsigned short word[] = {'A', '/', 'B', 0, 'C'};
    Input input;
    ThreeMMSourceNameInput merge;
    allocator.context = &m;
    CHECK(three_mm_source_names_init(&n, 1, 1, 1, &allocator) == THREE_MM_STORAGE_OK);
    text = (void *)1;
    count = 99;
    CHECK(!three_mm_source_names_merged(&n, 1, &text, &count) && text == (void *)1 && count == 99);
    CHECK(three_mm_source_names_add(&n, 1, "Long/SHORT", 10));
    CHECK(three_mm_source_names_add(&n, 2, "Other/SHORT", 11));
    CHECK(three_mm_source_names_add(&n, 1, "ignored", 7) && n.count == 2);
    sid = 99;
    CHECK(three_mm_source_names_find_name(&n, "short", 5, equal, NULL, &sid) && sid == 1);
    CHECK(three_mm_source_names_find_name(&n, "OTHER/SHORT", 11, equal, NULL, &sid) && sid == 2);
    sid = 99;
    CHECK(!three_mm_source_names_find_name(&n, "missing", 7, equal, NULL, &sid) && sid == 99);
    position = 99;
    CHECK(!three_mm_source_names_find_id(&n, 99, &position) && position == 99);
    CHECK(!three_mm_source_names_merged(&n, 99, &text, &count) && count == 11 && memcmp(text, "Other/SHORT", 11) == 0);
    p = three_mm_source_name_split("A\0/B", 4, 1);
    CHECK(p.long_count == 4 && p.short_offset == 0 && p.short_count == 4);
    p = three_mm_source_name_split("A/B\0C", 5, 1);
    CHECK(p.long_count == 1 && p.short_offset == 2 && p.short_count == 1);
    p = three_mm_source_name_split("/Tail", 5, 1);
    CHECK(p.long_count == 0 && p.short_count == 4);
    p = three_mm_source_name_split("Head/", 5, 1);
    CHECK(p.long_count == 4 && p.short_count == 0);
    CHECK(three_mm_source_names_init(&wide, 2, 1, 0, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_source_names_add(&wide, 8, word, 5));
    p = three_mm_source_name_split(word, 5, 2);
    CHECK(p.long_count == 1 && p.short_count == 1 && p.short_offset == 2);
    CHECK(three_mm_source_names_at(&wide, 0, &text, &count, &sid) && sid == 8 && count == 5 && !memcmp(text, word, 10));
    CHECK(three_mm_source_names_dispose(&wide) == THREE_MM_STORAGE_OK);
    memset(&output, 0, sizeof(output));
    writer.context = &output;
    writer.length = three_mm_source_names_wire_size(&n);
    writer.write_at = write_at;
    CHECK(n.count * 8 <= sizeof(before));
    memcpy(before, n.storage.buffers[1].data, n.count * 8);
    CHECK(three_mm_source_names_write(&n, &writer, 1) == THREE_MM_STORAGE_OK);
    CHECK(!memcmp(before, n.storage.buffers[1].data, n.count * 8));
    output.calls = 0;
    output.fail = 3;
    CHECK(three_mm_source_names_write(&n, &writer, 1) == THREE_MM_STORAGE_IO);
    CHECK(!memcmp(before, n.storage.buffers[1].data, n.count * 8) && n.storage.buffers[1].locks == 0);
    --writer.length;
    CHECK(three_mm_source_names_write(&n, &writer, 0) == THREE_MM_STORAGE_ARGUMENT);
    memset(&input, 0, sizeof(input));
    input.swapped = 1;
    merge.context = &input;
    merge.count = 1;
    merge.get = get;
    merge.invalid_sid = diagnostic;
    CHECK(three_mm_source_names_merge(&n, &merge, 1) && three_mm_source_names_find_id(&n, 3, NULL));
    CHECK(three_mm_source_names_resolve(&n, 99, NULL, provider, &n) == THREE_MM_SOURCE_LEGACY);
    CHECK(three_mm_source_names_resolve(&n, 1, &n, provider, &n) == THREE_MM_SOURCE_AVAILABLE);
    CHECK(three_mm_source_names_dispose(&n) == THREE_MM_STORAGE_OK && m.live == 0);
    CHECK(three_mm_source_names_init(&n, 1, 1, 0, &allocator) == THREE_MM_STORAGE_OK);
    input.swapped = 0;
    input.memory = &m;
    merge.count = 2;
    /* Force the second row to need a fresh payload allocation. Earlier merge rows remain. */
    CHECK(!three_mm_source_names_merge(&n, &merge, 0) && n.count == 1 && three_mm_source_names_find_id(&n, 20, NULL));
    m.fail = 0;
    CHECK(three_mm_source_names_dispose(&n) == THREE_MM_STORAGE_OK && m.live == 0);
}
typedef struct Discovery
{
    char log[128];
    int used, access, names, known, ensure, begin, drives, drive, floppy, long_ok, short_ok, candidate, output;
} Discovery;
static void event(Discovery *h, char c)
{
    CHECK(h->used < 127);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int access_source(void *c, ThreeMMI32 sid)
{
    Discovery *h = (Discovery *)c;
    CHECK(sid == 1);
    event(h, 'a');
    return h->access;
}
static int source_names(void *c, ThreeMMI32 sid)
{
    Discovery *h = (Discovery *)c;
    CHECK(sid == 1);
    event(h, 'n');
    return h->names;
}
static void root(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'r');
    h->candidate = 10;
}
static int descend(void *c, int short_name)
{
    Discovery *h = (Discovery *)c;
    event(h, short_name ? 's' : 'l');
    ++h->candidate;
    return short_name ? h->short_ok : h->long_ok;
}
static void publish(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'o');
    h->output = h->candidate;
}
static int known(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'k');
    return h->known;
}
static int ensure(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'e');
    return h->ensure;
}
static int begin(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'b');
    return h->begin;
}
static int next(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'd');
    return ++h->drive <= h->drives;
}
static int floppy(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'f');
    return h->drive == h->floppy;
}
static void candidate(void *c)
{
    Discovery *h = (Discovery *)c;
    event(h, 'c');
    h->candidate = 20 * h->drive;
}
static void discovery(void)
{
    Discovery h;
    ThreeMMSourceDiscovery s = {NULL,  access_source, source_names, root, descend, publish,
                                known, ensure,        begin,        next, floppy,  candidate};
    int value;
    s.context = &h;
    memset(&h, 0, sizeof(h));
    h.names = h.begin = 1;
    h.output = 99;
    h.short_ok = 1;
    CHECK(three_mm_source_discover_local(1, &s, &value) && value && !strcmp(h.log, "anrlso") && h.output == 12);
    memset(&h, 0, sizeof(h));
    h.names = 1;
    h.output = 99;
    CHECK(three_mm_source_discover_local(1, &s, &value) && !value && !strcmp(h.log, "anrls") && h.output == 99);
    memset(&h, 0, sizeof(h));
    h.access = THREE_MM_SOURCE_DISABLED;
    CHECK(three_mm_source_discover_local(1, &s, &value) && !value && !strcmp(h.log, "a"));
    CHECK(!three_mm_source_discover_media(1, &s, &value) && !value && !strcmp(h.log, "aa"));
    memset(&h, 0, sizeof(h));
    h.access = THREE_MM_SOURCE_AVAILABLE;
    CHECK(three_mm_source_discover_media(1, &s, &value) && value && !strcmp(h.log, "a"));
    memset(&h, 0, sizeof(h));
    h.known = 1;
    CHECK(!three_mm_source_discover_media(1, &s, &value) && !value && !strcmp(h.log, "ake"));
    h.used = 0;
    h.ensure = 1;
    CHECK(three_mm_source_discover_media(1, &s, &value) && !value && !strcmp(h.log, "ake"));
    memset(&h, 0, sizeof(h));
    h.names = h.begin = 1;
    h.drives = 2;
    h.floppy = 1;
    h.long_ok = 1;
    CHECK(three_mm_source_discover_media(1, &s, &value) && value && !strcmp(h.log, "aknbdfdfcslo") && h.output == 42);
    memset(&h, 0, sizeof(h));
    h.names = h.begin = 1;
    h.drives = 1;
    h.output = 99;
    CHECK(!three_mm_source_discover_media(1, &s, &value) && !value && !strcmp(h.log, "aknbdfcsld") && h.output == 99);
    memset(&h, 0, sizeof(h));
    CHECK(!three_mm_source_discover_local(1, &s, &value) && !value && !strcmp(h.log, "an"));
    memset(&h, 0, sizeof(h));
    h.names = 1;
    CHECK(!three_mm_source_discover_media(1, &s, &value) && !value && !strcmp(h.log, "aknb"));
}
int main(void)
{
    names();
    discovery();
    puts("source names/discovery passed");
    return 0;
}
