#ifndef SCENE_SOUND_FIXTURE_H
#define SCENE_SOUND_FIXTURE_H
#include "mm/scene_sound.h"
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
    MMU32 child;
    MMTag tag;
} Record;
typedef struct Fixture
{
    MMSceneSoundPacketServices s;
    MMSceneSound *packet;
    Record input[4];
    int refs[8], live, allocs, fail, retains, clears, lookups, relocate;
    unsigned char *group, *stale;
    MMI32 group_bytes;
    int ops[64], used, resolve_fail, fetch_fail, enqueue_fail, resources, plays, mutate, shrink;
    int loop_tools[4], queues[4], loops[4], volumes[4];
    MMU32 starts[4];
} Fixture;
static Record *at(MMSceneSound *p, int i)
{
    return (Record *)((unsigned char *)p + 16) + i;
}
static void log_op(Fixture *f, int op)
{
    CHECK(f->used < 64);
    f->ops[f->used++] = op;
}
static void *allocate(void *p, MMI32 bytes)
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
static MMI32 sid(void *p, const void *r)
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
static void read_tag(void *p, const void *r, MMU32 *child, MMTag *tag)
{
    (void)p;
    *child = ((const Record *)r)->child;
    *tag = ((const Record *)r)->tag;
}
static void write_tag(void *p, void *r, MMU32 child, const MMTag *tag)
{
    (void)p;
    ((Record *)r)->child = child;
    ((Record *)r)->tag = *tag;
}
static void *group_data(void *p, void *g, MMI32 index)
{
    Fixture *f = (Fixture *)p;
    CHECK(g == f && index == 0);
    ++f->lookups;
    return f->group;
}
static MMI32 group_size(void *p, void *g, MMI32 index)
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
static MMI32 priority(void *p, void *sound, int tool)
{
    Fixture *f = (Fixture *)p;
    Record *tag = (Record *)sound;
    CHECK(f->resources == 1);
    log_op(f, 30 + (int)tag->child);
    f->loop_tools[tag->child] = tool;
    return (MMI32)(tag->child * 10 + tool);
}
static void enqueue(void *p, void *sound, int loop, int queue, MMI32 volume, MMI32 priority, MMU32 start)
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
    MMSceneSoundPacketServices *s;
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
        f->input[i].child = (MMU32)i + 1;
        f->input[i].tag.sid = i % 2 ? 3 : 0;
        f->input[i].tag.cache = f;
        f->input[i].tag.type = 0x4d534e44U;
        f->input[i].tag.number = (MMU32)i + 1;
    }
}
#ifndef SCENE_SOUND_FIXTURE_NO_PACKET_HELPERS
static MMSceneSound *make(Fixture *f, int count)
{
    MMSceneSound *p = mm_scene_sound_make(80, 4, 1, count, f->input, &f->s);
    CHECK(p && p->vlm == 80 && p->sty == 4 && p->fLoop == 1 && p->ctagc == count);
    return p;
}
static void drop(Fixture *f, MMSceneSound *p)
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
#endif
static void clean(Fixture *f)
{
    int i;
    CHECK(!f->live && !f->resources);
    for (i = 0; i < 8; ++i)
        CHECK(!f->refs[i]);
    free(f->group);
    free(f->stale);
}
#endif
