#include "three_mm/scene_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene sound line %d\n", __LINE__);                                                        \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Record
{
    ThreeMMU32 child;
    ThreeMMTag tag;
} Record;
typedef struct Fixture
{
    ThreeMMSceneSoundPacketServices s;
    ThreeMMSceneSound *packet;
    Record input[4];
    int refs[8], live, allocs, fail, retains, clears, lookups, relocate;
    unsigned char *group, *stale;
    ThreeMMI32 group_bytes;
    int ops[64], used, resolve_fail, fetch_fail, enqueue_fail, resources, plays, mutate, shrink;
    int loop_tools[4], queues[4], loops[4], volumes[4];
    ThreeMMU32 starts[4];
} Fixture;
static Record *at(ThreeMMSceneSound *p, int i)
{
    return (Record *)((unsigned char *)p + 16) + i;
}
static void log_op(Fixture *f, int op)
{
    CHECK(f->used < 64);
    f->ops[f->used++] = op;
}
static void *allocate(void *p, ThreeMMI32 bytes)
{
    Fixture *f = (Fixture *)p;
    void *out;
    ++f->allocs;
    CHECK(bytes >= 16 && bytes < 100000);
    if (f->fail)
        return NULL;
    if (f->relocate)
    {
        f->relocate = 0;
        CHECK(f->group && !f->stale);
        f->stale = f->group;
        f->group = (unsigned char *)malloc((size_t)f->group_bytes);
        CHECK(f->group);
        memcpy(f->group, f->stale, (size_t)f->group_bytes);
        memset(f->stale, 0xdd, (size_t)f->group_bytes);
    }
    out = malloc((size_t)bytes);
    CHECK(out);
    memset(out, 0xa5, (size_t)bytes);
    ++f->live;
    return out;
}
static void raw_free(Fixture *f, void *p)
{
    if (p)
    {
        CHECK(f->live > 0);
        --f->live;
        free(p);
    }
}
static void copy_record(void *p, void *to, const void *from)
{
    (void)p;
    memcpy(to, from, sizeof(Record));
}
static ThreeMMI32 sid(void *p, const void *r)
{
    (void)p;
    return ((const Record *)r)->tag.sid;
}
static void retain(void *p, void *r)
{
    Fixture *f = (Fixture *)p;
    Record *tag = (Record *)r;
    ++f->retains;
    if (!tag->tag.sid)
    {
        CHECK(tag->tag.cache == f && tag->tag.number < 8);
        ++f->refs[tag->tag.number];
    }
}
static void clear_cache(void *p, void *r)
{
    ++((Fixture *)p)->clears;
    ((Record *)r)->tag.cache = NULL;
}
static void read_tag(void *p, const void *r, ThreeMMU32 *child, ThreeMMTag *tag)
{
    (void)p;
    *child = ((const Record *)r)->child;
    *tag = ((const Record *)r)->tag;
}
static void write_tag(void *p, void *r, ThreeMMU32 child, const ThreeMMTag *tag)
{
    (void)p;
    ((Record *)r)->child = child;
    ((Record *)r)->tag = *tag;
}
static void *group_data(void *p, void *g, ThreeMMI32 index)
{
    Fixture *f = (Fixture *)p;
    CHECK(g == f && index == 0);
    ++f->lookups;
    return f->group;
}
static ThreeMMI32 group_size(void *p, void *g, ThreeMMI32 index)
{
    Fixture *f = (Fixture *)p;
    CHECK(g == f && index == 0);
    return f->group_bytes;
}
static int resolve(void *p, void *r)
{
    Fixture *f = (Fixture *)p;
    Record *tag = (Record *)r;
    log_op(f, 10 + (int)tag->child);
    if (f->mutate && tag->child == 3)
    {
        f->packet->fLoop = 7;
        f->packet->vlm = 33;
    }
    return !(f->resolve_fail & (1 << tag->child));
}
static void *fetch(void *p, void *r)
{
    Fixture *f = (Fixture *)p;
    Record *tag = (Record *)r;
    log_op(f, 20 + (int)tag->child);
    if (f->fetch_fail == (int)tag->child)
        return NULL;
    ++f->resources;
    return tag;
}
static ThreeMMI32 priority(void *p, void *sound, int tool)
{
    Fixture *f = (Fixture *)p;
    Record *tag = (Record *)sound;
    CHECK(f->resources == 1);
    log_op(f, 30 + (int)tag->child);
    f->loop_tools[tag->child] = tool;
    return (ThreeMMI32)(tag->child * 10 + tool);
}
static void enqueue(void *p, void *sound, int loop, int queue, ThreeMMI32 volume, ThreeMMI32 priority, ThreeMMU32 start)
{
    Fixture *f = (Fixture *)p;
    int id = (int)((Record *)sound)->child;
    CHECK(priority == id * 10 + f->loop_tools[id]);
    log_op(f, 40 + id);
    ++f->plays;
    f->queues[id] = queue;
    f->loops[id] = loop;
    f->volumes[id] = volume;
    f->starts[id] = start;
    if (f->enqueue_fail == id)
        return; /* Native queue failure has no dispatcher result. */
}
static void release(void *p, void *sound)
{
    Fixture *f = (Fixture *)p;
    int id = (int)((Record *)sound)->child;
    CHECK(f->resources == 1);
    --f->resources;
    log_op(f, 50 + id);
    if (f->shrink)
        f->packet->ctagc = 1;
}
static void init(Fixture *f)
{
    int i;
    ThreeMMSceneSoundPacketServices *s;
    memset(f, 0, sizeof(*f));
    s = &f->s;
    s->context = f;
    s->tag_bytes = sizeof(Record);
    s->allocate = allocate;
    s->copy_record = copy_record;
    s->sid = sid;
    s->retain = retain;
    s->clear_cache = clear_cache;
    s->read_tag = read_tag;
    s->write_tag = write_tag;
    s->group_data = group_data;
    s->group_size = group_size;
    s->resolve = resolve;
    s->fetch = fetch;
    s->priority = priority;
    s->enqueue = enqueue;
    s->release = release;
    for (i = 0; i < 4; ++i)
    {
        f->input[i].child = (ThreeMMU32)i + 1;
        f->input[i].tag.sid = i % 2 ? 3 : 0;
        f->input[i].tag.cache = f;
        f->input[i].tag.type = 0x4d534e44U;
        f->input[i].tag.number = (ThreeMMU32)i + 1;
    }
}
static ThreeMMSceneSound *make(Fixture *f, int count)
{
    ThreeMMSceneSound *p = three_mm_scene_sound_make(80, 4, 1, count, f->input, &f->s);
    CHECK(p && p->vlm == 80 && p->sty == 4 && p->fLoop == 1 && p->ctagc == count);
    return p;
}
static void drop(Fixture *f, ThreeMMSceneSound *p)
{
    int i;
    Record *tag;
    if (!p)
        return;
    for (i = 0; i < p->ctagc; ++i)
    {
        tag = at(p, i);
        if (!tag->tag.sid && tag->tag.cache)
        {
            CHECK(f->refs[tag->tag.number] > 0);
            --f->refs[tag->tag.number];
        }
    }
    raw_free(f, p);
}
static void clean(Fixture *f)
{
    int i;
    CHECK(!f->live && !f->resources);
    for (i = 0; i < 8; ++i)
        CHECK(!f->refs[i]);
    free(f->group);
    free(f->stale);
}
static void factories(void)
{
    Fixture f;
    ThreeMMSceneSound *p, *copy, *added;
    int before;
    init(&f);
    CHECK(three_mm_scene_sound_size(0, 32) == 16 && !three_mm_scene_sound_size(-1, 32));
    CHECK(!three_mm_scene_sound_size(1, 0) && !three_mm_scene_sound_size(1, (size_t)-1));
    CHECK(three_mm_scene_sound_size(67108863, 32) == 2147483632 && !three_mm_scene_sound_size(67108864, 32));
    CHECK(!three_mm_scene_sound_size(2147483647, 32));
    p = three_mm_scene_sound_allocate(2, &f.s);
    CHECK(p && ((unsigned char *)p)[0] == 0xa5);
    raw_free(&f, p);
    f.fail = 1;
    CHECK(!three_mm_scene_sound_make(1, 2, 0, 3, f.input, &f.s) && !f.retains);
    f.fail = 0;
    p = make(&f, 3);
    CHECK(f.retains == 3 && f.refs[1] == 1 && f.refs[3] == 1 && at(p, 1)->tag.cache == &f);
    before = f.retains;
    f.fail = 1;
    CHECK(!three_mm_scene_sound_duplicate(p, &f.s) && f.retains == before);
    CHECK(!three_mm_scene_sound_append(p, f.input + 3, &f.s) && f.retains == before);
    f.fail = 0;
    p->fLoop = 0x01020304;
    copy = three_mm_scene_sound_duplicate(p, &f.s);
    CHECK(copy && f.retains == before + 3 && f.refs[1] == 2 && copy->fLoop == p->fLoop);
    CHECK(!memcmp(p, copy, (size_t)three_mm_scene_sound_size(3, sizeof(Record))));
    drop(&f, copy);
    f.input[3].tag.sid = 0;
    before = f.retains;
    added = three_mm_scene_sound_append(p, f.input + 3, &f.s);
    CHECK(added && added->ctagc == 4 && f.retains == before + 1);
    CHECK(f.refs[1] == 1 && f.refs[3] == 1 && f.refs[4] == 1 && added->fLoop == p->fLoop);
    raw_free(&f, p);
    drop(&f, added);
    clean(&f);
    init(&f);
    p = make(&f, 0);
    CHECK(p->ctagc == 0 && !f.retains);
    copy = three_mm_scene_sound_duplicate(p, &f.s);
    CHECK(copy);
    drop(&f, copy);
    drop(&f, p);
    clean(&f);
}
static void groups(void)
{
    Fixture f;
    ThreeMMSceneSound *p, *copy;
    int mode, previous;
    for (mode = 0; mode < 2; ++mode)
    {
        init(&f);
        p = make(&f, 3);
        f.group_bytes = three_mm_scene_sound_size(3, sizeof(Record));
        f.group = (unsigned char *)malloc((size_t)f.group_bytes);
        CHECK(f.group);
        memcpy(f.group, p, (size_t)f.group_bytes);
        f.fail = 1;
        previous = f.retains;
        CHECK(!three_mm_scene_sound_group(&f, 0, mode, &f.s));
        CHECK(f.lookups == 1 && f.retains == previous);
        f.fail = 0;
        f.relocate = 1;
        f.lookups = 0;
        copy = three_mm_scene_sound_group(&f, 0, mode, &f.s);
        CHECK(copy && f.lookups == 2 && f.stale);
        CHECK(f.retains == previous + (mode ? 2 : 0) && f.clears == (mode ? 0 : 3));
        CHECK(at(copy, 1)->tag.cache == (mode ? (void *)&f : NULL));
        drop(&f, copy);
        f.group_bytes = 15;
        f.lookups = 0;
        previous = f.allocs;
        CHECK(!three_mm_scene_sound_group(&f, 0, 1, &f.s) && !f.lookups && f.allocs == previous);
        f.group_bytes = 16;
        CHECK(!three_mm_scene_sound_group(&f, 0, 1, &f.s) && f.allocs == previous);
        f.group_bytes = three_mm_scene_sound_size(3, sizeof(Record));
        ((ThreeMMSceneSound *)f.group)->ctagc = -1;
        CHECK(!three_mm_scene_sound_group(&f, 0, 1, &f.s));
        drop(&f, p);
        clean(&f);
    }
}
static ThreeMMU32 load(const unsigned char *p)
{
    ThreeMMU32 n;
    memcpy(&n, p, 4);
    return n;
}
static void store(unsigned char *p, ThreeMMU32 n)
{
    memcpy(p, &n, 4);
}
static void swap_words(unsigned char *p, int bytes)
{
    int i;
    ThreeMMU32 n;
    for (i = 0; i < bytes; i += 4)
    {
        n = load(p + i);
        store(p + i, ((n & 255) << 24) | ((n & 65280) << 8) | ((n >> 8) & 65280) | (n >> 24));
    }
}
static void codecs(void)
{
    Fixture f;
    ThreeMMSceneSound *p, *copy;
    unsigned char *wire, *buffer, *encoded;
    unsigned char canonical[56], saved[61];
    ThreeMMI32 bytes, output;
    int swapped, length, i, before;
    init(&f);
    p = three_mm_scene_sound_allocate(2, &f.s);
    CHECK(p);
    p->vlm = -123;
    p->sty = 2147483647;
    p->fLoop = 0x01020304;
    p->ctagc = 2;
    memcpy(at(p, 0), f.input, 2 * sizeof(Record));
    at(p, 0)->tag.sid = -1;
    at(p, 0)->tag.type = 0xfedcba98U;
    at(p, 0)->tag.number = 0x89abcdefU;
    at(p, 0)->child = 0x80000000U;
    f.fail = 1;
    bytes = 99;
    CHECK(!three_mm_scene_sound_encode(p, &bytes, &f.s) && !bytes);
    f.fail = 0;
    wire = (unsigned char *)three_mm_scene_sound_encode(p, &bytes, &f.s);
    CHECK(wire && bytes == 56);
    CHECK(load(wire + 16) == 0x80000000U && load(wire + 20) == 0xffffffffU && !load(wire + 24));
    CHECK(load(wire + 28) == 0xfedcba98U && load(wire + 32) == 0x89abcdefU && !load(wire + 44));
    memcpy(canonical, wire, 56);
    store(canonical + 24, 0x12345678U);
    for (swapped = 0; swapped < 2; ++swapped)
    {
        buffer = (unsigned char *)malloc(63);
        CHECK(buffer);
        memset(buffer, 0xee, 63);
        memcpy(buffer + 1, canonical, 56);
        if (swapped)
            swap_words(buffer + 1, 56);
        memcpy(saved, buffer + 1, 61);
        before = f.allocs;
        for (length = 0; length < 56; ++length)
        {
            CHECK(!three_mm_scene_sound_decode(buffer + 1, length, swapped, &f.s));
            CHECK(!memcmp(saved, buffer + 1, 61));
        }
        CHECK(f.allocs == before);
        f.fail = 1;
        CHECK(!three_mm_scene_sound_decode(buffer + 1, 61, swapped, &f.s));
        f.fail = 0;
        CHECK(!memcmp(buffer + 1, canonical, 56) && buffer[0] == 0xee && buffer[62] == 0xee);
        if (swapped)
            swap_words(buffer + 1, 56);
        copy = three_mm_scene_sound_decode(buffer + 1, 61, swapped, &f.s);
        CHECK(copy && !memcmp(copy, p, 16));
        for (i = 0; i < 2; ++i)
            CHECK(!at(copy, i)->tag.cache && at(copy, i)->tag.sid == at(p, i)->tag.sid &&
                  at(copy, i)->child == at(p, i)->child);
        encoded = (unsigned char *)three_mm_scene_sound_encode(copy, &output, &f.s);
        CHECK(encoded && output == 56 && !memcmp(encoded, wire, 56));
        raw_free(&f, encoded);
        raw_free(&f, copy);
        free(buffer);
    }
    for (i = 0; i < 3; ++i)
    {
        memcpy(saved, wire, 56);
        store(saved + 12, i == 0 ? 0 : i == 1 ? 0xffffffffU : 0x7fffffffU);
        before = f.allocs;
        CHECK(!three_mm_scene_sound_decode(saved, 56, 0, &f.s) && f.allocs == before);
    }
    CHECK(!three_mm_scene_sound_decode(NULL, 56, 0, &f.s));
    p->ctagc = -1;
    output = 99;
    CHECK(!three_mm_scene_sound_encode(p, &output, &f.s) && !output);
    CHECK(!three_mm_scene_sound_duplicate(p, &f.s) && !three_mm_scene_sound_append(p, f.input, &f.s));
    raw_free(&f, wire);
    raw_free(&f, p);
    clean(&f);
}
static void playback(void)
{
    Fixture f;
    ThreeMMSceneSound *p;
    int i;
    static const int normal[] = {11, 21, 31, 41, 51, 22, 32, 42, 52, 13, 23, 33, 43, 53};
    static const int skipped[] = {11, 22, 32, 42, 52, 13, 23, 33, 43, 53};
    init(&f);
    p = f.packet = make(&f, 3);
    f.enqueue_fail = 1;
    three_mm_scene_sound_play(p, 0xffffffffU, &f.s);
    CHECK(f.used == (int)(sizeof(normal) / sizeof(*normal)) && !memcmp(f.ops, normal, sizeof(normal)) && f.plays == 3 &&
          !f.resources);
    CHECK(f.starts[1] == 0xffffffffU && !f.starts[2] && !f.starts[3] && !f.queues[1] && f.queues[2] && f.queues[3]);
    for (i = 1; i < 4; ++i)
        CHECK(f.loop_tools[i] == 1 && f.loops[i] == 1 && f.volumes[i] == 80);
    f.used = f.plays = 0;
    p->fLoop = 0;
    f.resolve_fail = 1 << 1;
    f.mutate = 1;
    three_mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.used == (int)(sizeof(skipped) / sizeof(*skipped)) && !memcmp(f.ops, skipped, sizeof(skipped)) &&
          f.plays == 2);
    CHECK(f.queues[2] && !f.starts[2] && f.loops[2] == 0 && f.loops[3] == 1 && f.volumes[3] == 33 &&
          f.loop_tools[3] == 0);
    f.used = f.plays = 0;
    f.resolve_fail = 0;
    f.fetch_fail = 2;
    f.mutate = 0;
    three_mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && !f.resources && f.ops[f.used - 1] == 22);
    f.used = f.plays = 0;
    f.fetch_fail = 0;
    f.resolve_fail = (1 << 1) | (1 << 3);
    three_mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && f.queues[2] && !f.starts[2]);
    f.used = f.plays = 0;
    f.resolve_fail = 0;
    f.shrink = 1;
    three_mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && p->ctagc == 1);
    p->ctagc = 0;
    f.used = 0;
    three_mm_scene_sound_play(p, 99, &f.s);
    CHECK(!f.used);
    p->ctagc = 3;
    drop(&f, p);
    clean(&f);
}
int main(void)
{
    factories();
    groups();
    codecs();
    playback();
    puts("C89 Scene sound packets: ownership, relocation/faults, wire codecs and indexed playback passed.");
    return 0;
}
