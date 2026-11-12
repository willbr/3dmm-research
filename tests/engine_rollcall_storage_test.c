#include "three_mm/rollcall_storage.h"
#include "three_mm/movie_owner.h"
#include "three_mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "roll-call storage failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Memory
{
    int calls, fail_after, live;
} Memory;
static void *resize(void *c, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int priority)
{
    Memory *m = (Memory *)c;
    void *p;
    (void)priority;
    if (m->fail_after >= 0 && m->calls++ >= m->fail_after)
        return NULL;
    p = malloc(bytes);
    CHECK(p != NULL);
    if (old)
    {
        memcpy(p, old, old_bytes < bytes ? old_bytes : bytes);
        free(old);
    }
    else
        ++m->live;
    return p;
}
static void release(void *c, void *p, ThreeMMU32 bytes)
{
    (void)bytes;
    --((Memory *)c)->live;
    free(p);
}
typedef struct IO
{
    unsigned char bytes[8192];
    int calls, fail;
    ThreeMMU32 size;
} IO;
static int write_at(void *c, ThreeMMU32 offset, const void *bytes, size_t size)
{
    IO *io = (IO *)c;
    if (++io->calls == io->fail)
        return 0;
    CHECK(offset + size <= sizeof(io->bytes));
    memcpy(io->bytes + offset, bytes, size);
    return 1;
}
static int read_at(void *c, ThreeMMU32 offset, void *bytes, size_t size)
{
    IO *io = (IO *)c;
    if (++io->calls == io->fail)
        return 0;
    CHECK(offset + size <= io->size);
    memcpy(bytes, io->bytes + offset, size);
    return 1;
}
static ThreeMMReadStream reader(IO *io)
{
    ThreeMMReadStream s;
    s.context = io;
    s.length = io->size;
    s.read_at = read_at;
    return s;
}
static ThreeMMStorageWriter writer(IO *io, ThreeMMU32 size)
{
    ThreeMMStorageWriter w;
    w.context = io;
    w.length = io->size = size;
    w.write_at = write_at;
    return w;
}
static void mutations(void)
{
    ThreeMMRollCallTable t;
    Memory m = {0, -1, 0};
    ThreeMMStorageAllocator a = {NULL, resize, release};
    ThreeMMRollCallActor actor, out;
    ThreeMMU32 length;
    const void *text;
    char large[255];
    int i, canary = 123;
    a.context = &m;
    memset(&actor, 0, sizeof(actor));
    actor.cache = &canary;
    actor.data.source_id = 0;
    CHECK(three_mm_rollcall_table_init(&t, sizeof(actor), 1, THREE_MM_TEXT_WINDOWS, 1, &a) == THREE_MM_STORAGE_OK);
    t.storage.min_grow[0] = t.storage.min_grow[1] = 0;
    m.fail_after = 1;
    CHECK(!three_mm_rollcall_table_add(&t, "first", 5, &actor) && !t.count && !t.bytes && m.live == 1);
    m.fail_after = -1;
    for (i = 0; i < 50; ++i)
    {
        actor.data.actor_id = i;
        CHECK(three_mm_rollcall_table_add(&t, "name", 4, &actor));
    }
    for (i = 0; i < 50; ++i)
    {
        CHECK(three_mm_rollcall_table_extra(&t, i, &out) && out.data.actor_id == i && out.cache == &canary);
    }
    memset(large, 'L', 255);
    m.fail_after = 0;
    m.calls = 0;
    CHECK(!three_mm_rollcall_table_rename(&t, 3, large, 255) && t.count == 50);
    CHECK(three_mm_rollcall_table_name(&t, 3, &text, &length) && length == 4 && !memcmp(text, "name", 4));
    CHECK(three_mm_rollcall_table_extra(&t, 3, &out) && out.data.actor_id == 3);
    m.fail_after = -1;
    CHECK(three_mm_rollcall_table_rename(&t, 3, large, 255));
    CHECK(three_mm_rollcall_table_rename(&t, 3, "x", 1));
    actor.data.actor_id = -9;
    CHECK(three_mm_rollcall_table_set_extra(&t, 3, &actor));
    CHECK(three_mm_rollcall_table_delete(&t, 0) && three_mm_rollcall_table_delete(&t, 1));
    CHECK(three_mm_rollcall_table_extra(&t, 1, &out) && out.data.actor_id == -9 && t.count == 48);
    CHECK(!three_mm_rollcall_table_delete(&t, 48) && !three_mm_rollcall_table_rename(&t, 48, "", 0));
    CHECK(!three_mm_rollcall_table_extra(&t, 48, &out) && !three_mm_rollcall_table_set_extra(&t, 48, &actor));
    CHECK(three_mm_storage_lock(&t.storage, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_dispose(&t) == THREE_MM_STORAGE_LOCKED && t.count == 48);
    CHECK(three_mm_storage_unlock(&t.storage, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_dispose(&t) == THREE_MM_STORAGE_OK && !m.live && canary == 123);
}
static size_t convert(void *c, const void *input, size_t bytes, int encoding, void *output, size_t capacity)
{
    int *fail = (int *)c;
    const unsigned char *p = (const unsigned char *)input;
    size_t i, n = bytes / 2;
    CHECK(encoding == THREE_MM_TEXT_UTF16_BE && capacity == 255);
    if (*fail)
        return 256;
    for (i = 0; i < n; ++i)
    {
        CHECK(!p[i * 2]);
        ((unsigned char *)output)[i] = p[i * 2 + 1];
    }
    return n;
}
static void wire(void)
{
    ThreeMMRollCallTable t, loaded;
    ThreeMMStorageWriter w;
    ThreeMMReadStream r;
    IO io;
    unsigned char extra[28], got[28], entries[96];
    ThreeMMU32 count;
    const void *text;
    int big, fail, i;
    ThreeMMMovieActor actor;
    ThreeMMMemoryStream memory;
    memset(&io, 0, sizeof(io));
    memset(&actor, 0, sizeof(actor));
    actor.actor_id = 42;
    actor.source_id = 1;
    actor.template_number = 99;
    for (big = 0; big < 2; ++big)
    {
        CHECK(three_mm_rollcall_table_init(&t, 28, 1, THREE_MM_TEXT_WINDOWS, 1, NULL) == THREE_MM_STORAGE_OK);
        three_mm_movie_write_actor(&actor, extra, big);
        CHECK(three_mm_rollcall_table_add(&t, "Alpha", 5, extra) && three_mm_rollcall_table_add(&t, "", 0, extra));
        CHECK(three_mm_rollcall_table_rename(&t, 0, "Zulu", 4));
        memcpy(entries, t.storage.buffers[1].data, t.count * t.stride);
        w = writer(&io, three_mm_rollcall_table_wire_size(&t));
        io.calls = io.fail = 0;
        CHECK(three_mm_rollcall_table_write(&t, &w, big) == THREE_MM_STORAGE_OK &&
              !memcmp(entries, t.storage.buffers[1].data, t.count * t.stride));
        for (i = 1; i <= 3; ++i)
        {
            io.calls = 0;
            io.fail = i;
            CHECK(three_mm_rollcall_table_write(&t, &w, big) == THREE_MM_STORAGE_IO);
            CHECK(!memcmp(entries, t.storage.buffers[1].data, t.count * t.stride) && !t.storage.buffers[0].locks &&
                  !t.storage.buffers[1].locks);
        }
        io.calls = io.fail = 0;
        CHECK(three_mm_rollcall_table_write(&t, &w, big) == THREE_MM_STORAGE_OK);
        CHECK(three_mm_rollcall_table_init(&loaded, 0, 1, THREE_MM_TEXT_WINDOWS, 0, NULL) == THREE_MM_STORAGE_OK);
        r = reader(&io);
        io.calls = 0;
        fail = 9;
        CHECK(three_mm_rollcall_table_read(&loaded, &r, NULL, NULL, &fail) == THREE_MM_STORAGE_OK && fail == big);
        CHECK(loaded.count == 2 && loaded.stride == 32 && three_mm_rollcall_table_name(&loaded, 0, &text, &count) &&
              count == 4 && !memcmp(text, "Zulu", 4));
        CHECK(three_mm_rollcall_table_extra(&loaded, 1, got) && !memcmp(got, extra, 28));
        three_mm_memory_stream_init(&memory, got, 28);
        CHECK(three_mm_movie_read_actor(&memory.stream, big, &actor, NULL) == THREE_MM_MOVIE_OK &&
              actor.actor_id == 42);
        CHECK(three_mm_rollcall_table_dispose(&loaded) == THREE_MM_STORAGE_OK);
        for (i = 1; i <= 3; ++i)
        {
            CHECK(three_mm_rollcall_table_init(&loaded, 0, 1, THREE_MM_TEXT_WINDOWS, 0, NULL) == THREE_MM_STORAGE_OK);
            io.calls = 0;
            io.fail = i;
            fail = 9;
            CHECK(three_mm_rollcall_table_read(&loaded, &r, NULL, NULL, &fail) == THREE_MM_STORAGE_IO && fail == 9);
            CHECK(three_mm_rollcall_table_dispose(&loaded) == THREE_MM_STORAGE_OK);
        }
        io.fail = 0;
        io.bytes[16] = 0;
        io.bytes[17] = io.bytes[18] = io.bytes[19] = 0;
        CHECK(three_mm_rollcall_table_init(&loaded, 0, 1, THREE_MM_TEXT_WINDOWS, 0, NULL) == THREE_MM_STORAGE_OK);
        CHECK(three_mm_rollcall_table_read(&loaded, &r, NULL, NULL, NULL) == THREE_MM_STORAGE_ARGUMENT);
        CHECK(three_mm_rollcall_table_dispose(&loaded) == THREE_MM_STORAGE_OK &&
              three_mm_rollcall_table_dispose(&t) == THREE_MM_STORAGE_OK);
    }
}
static void translation(void)
{
    ThreeMMRollCallTable t, loaded;
    IO io;
    ThreeMMStorageWriter w;
    ThreeMMReadStream r;
    unsigned char text[] = {0, 'A', 0, 'B'};
    ThreeMMU32 extra = 0xabcdef01UL, out, n;
    const void *name;
    int fail = 0;
    /* Big-endian native UTF-16 bytes, including its counted prefix. */
    memset(&io, 0, sizeof(io));
    CHECK(three_mm_rollcall_table_init(&t, 4, 2, THREE_MM_TEXT_UTF16_BE, 0, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_add(&t, text, 2, &extra));
    t.storage.buffers[0].data[0] = 0;
    t.storage.buffers[0].data[1] = 2;
    w = writer(&io, three_mm_rollcall_table_wire_size(&t));
    CHECK(three_mm_rollcall_table_write(&t, &w, 1) == THREE_MM_STORAGE_OK);
    r = reader(&io);
    CHECK(three_mm_rollcall_table_init(&loaded, 0, 1, THREE_MM_TEXT_WINDOWS, 0, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_read(&loaded, &r, convert, &fail, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_name(&loaded, 0, &name, &n) && n == 2 && !memcmp(name, "AB", 2));
    CHECK(three_mm_rollcall_table_extra(&loaded, 0, &out) && out == extra);
    CHECK(three_mm_rollcall_table_dispose(&loaded) == THREE_MM_STORAGE_OK);
    fail = 1;
    CHECK(three_mm_rollcall_table_init(&loaded, 0, 1, THREE_MM_TEXT_WINDOWS, 0, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_rollcall_table_read(&loaded, &r, convert, &fail, NULL) == THREE_MM_STORAGE_ARGUMENT);
    CHECK(three_mm_rollcall_table_dispose(&loaded) == THREE_MM_STORAGE_OK &&
          three_mm_rollcall_table_dispose(&t) == THREE_MM_STORAGE_OK);
}
int main(void)
{
    mutations();
    wire();
    translation();
    puts("roll-call storage passed");
    return 0;
}
