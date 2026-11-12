#include "three_mm/actor_track.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor track failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Table
{
    int route, locked;
    ThreeMMU32 count, growth;
    unsigned char fixed[13][20], payload[13][65];
} Table;
typedef struct Host
{
    int allocations, fail, live, refs, retains, releases;
    char log[512];
    size_t logged;
} Host;
static Host h;
static void log_call(char c)
{
    CHECK(h.logged + 1 < sizeof(h.log));
    h.log[h.logged++] = c;
    h.log[h.logged] = 0;
}
static void reset_log(void)
{
    h.logged = 0;
    h.log[0] = 0;
    h.allocations = 0;
    h.fail = 0;
}
static Table *allocate(int route)
{
    Table *t;
    ++h.allocations;
    if (h.fail == h.allocations)
        return NULL;
    t = (Table *)calloc(1, sizeof(*t));
    CHECK(t != NULL);
    t->route = route;
    ++h.live;
    return t;
}
static void *new_events(void *c, ThreeMMU32 fixed, ThreeMMU32 entries, ThreeMMU32 variable)
{
    CHECK(c == &h && fixed == 20 && entries == 10 && variable == 20);
    log_call('E');
    return allocate(0);
}
static void *new_route(void *c, ThreeMMU32 width, ThreeMMU32 entries)
{
    CHECK(c == &h && width == 16 && entries == 32);
    log_call('R');
    return allocate(1);
}
static void growth(void *p, ThreeMMU32 n)
{
    Table *t = (Table *)p;
    CHECK(t->route && n == 32);
    t->growth = n;
    log_call('g');
}
static void *clone_table(void *p, int route)
{
    Table *t = (Table *)p, *out;
    CHECK(t->route == route && !t->locked);
    log_call(route ? 'r' : 'e');
    out = allocate(route);
    if (out)
        memcpy(out, t, sizeof(*out));
    return out;
}
static void *clone_events(void *p)
{
    return clone_table(p, 0);
}
static void *clone_route(void *p)
{
    return clone_table(p, 1);
}
static void release_events(void *p)
{
    Table *t = (Table *)p;
    CHECK(!t->route && !t->locked);
    log_call('X');
    --h.live;
    free(t);
}
static void release_route(void *p)
{
    Table *t = (Table *)p;
    CHECK(t->route && !t->locked);
    log_call('Y');
    --h.live;
    free(t);
}
static void lock_table(void *p)
{
    Table *t = (Table *)p;
    CHECK(!t->locked);
    t->locked = 1;
    log_call('L');
}
static void unlock_table(void *p)
{
    Table *t = (Table *)p;
    CHECK(t->locked);
    t->locked = 0;
    log_call('U');
}
static ThreeMMU32 count(void *p)
{
    return ((Table *)p)->count;
}
static void *fixed(void *p, ThreeMMU32 i)
{
    Table *t = (Table *)p;
    CHECK(i < t->count);
    return t->fixed[i];
}
/* Deliberately unaligned: native GG rows only promise byte addresses. */
static void *data(void *p, ThreeMMU32 i)
{
    Table *t = (Table *)p;
    CHECK(i < t->count);
    return t->payload[i] + 1;
}
static void retain(void *cache)
{
    CHECK(cache == &h);
    ++h.refs;
    ++h.retains;
    log_call('+');
}
static void release(void *cache)
{
    CHECK(cache == &h && h.refs > 0);
    --h.refs;
    ++h.releases;
    log_call('-');
}
static ThreeMMActorTrackServices services = {&h,          new_events,     new_route,     growth,     clone_events,
                                             clone_route, release_events, release_route, lock_table, unlock_table,
                                             count,       fixed,          data,          retain,     release};
static void row(Table *t, ThreeMMU32 i, ThreeMMI32 type, ThreeMMI32 custom, ThreeMMI32 sid, void *cache)
{
    ThreeMMActorCostume costume;
    ThreeMMActorSound sound;
    if (i >= t->count)
        t->count = i + 1;
    memcpy(t->fixed[i], &type, sizeof(type));
    memset(&costume, 0, sizeof(costume));
    memset(&sound, 0, sizeof(sound));
    if (type == THREE_MM_ACTOR_COSTUME)
    {
        costume.custom = custom;
        costume.tag.sid = sid;
        costume.tag.cache = cache;
        costume.tag.type = 123;
        costume.tag.number = 456;
        memcpy(data(t, i), &costume, sizeof(costume));
    }
    else if (type == THREE_MM_ACTOR_SOUND)
    {
        sound.tag.sid = sid;
        sound.tag.cache = cache;
        sound.tag.type = 789;
        sound.tag.number = 987;
        memcpy(data(t, i), &sound, sizeof(sound));
    }
}
static void populate(ThreeMMActorTrack *t)
{
    Table *e = (Table *)t->events;
    ThreeMMU32 i;
    for (i = 0; i < 13; ++i)
        row(e, i, (ThreeMMI32)i, 0, 0, &h);
    h.refs += 2;
    CHECK(three_mm_actor_event_tag(e, THREE_MM_ACTOR_COSTUME, &services) ==
          (unsigned char *)data(e, THREE_MM_ACTOR_COSTUME) + offsetof(ThreeMMActorCostume, tag));
    CHECK(three_mm_actor_event_tag(e, THREE_MM_ACTOR_SOUND, &services) != NULL);
    for (i = 0; i < 13; ++i)
        if (i != THREE_MM_ACTOR_COSTUME && i != THREE_MM_ACTOR_SOUND)
            CHECK(three_mm_actor_event_tag(e, i, &services) == NULL);
}
static void transactions(void)
{
    ThreeMMActorTrack source, target, old;
    Table *e;
    int n;
    three_mm_actor_track_init(&source);
    three_mm_actor_track_init(&target);
    CHECK(three_mm_actor_track_create(&source, &services));
    populate(&source);
    CHECK(three_mm_actor_track_create(&target, &services));
    populate(&target);
    CHECK(h.refs == 4 && h.live == 4);
    for (n = 1; n <= 2; ++n)
    {
        old = target;
        reset_log();
        h.fail = n;
        CHECK(!three_mm_actor_track_create(&target, &services));
        CHECK(target.events == old.events && target.route == old.route && h.refs == 4 && h.live == 4);
        CHECK(strcmp(h.log, n == 1 ? "E" : "ERLUX") == 0);
        reset_log();
        h.fail = n;
        CHECK(!three_mm_actor_track_clone(&target, &source, &services));
        CHECK(target.events == old.events && target.route == old.route && h.refs == 4 && h.live == 4);
        CHECK(strcmp(h.log, n == 1 ? "r" : "reY") == 0);
    }
    reset_log();
    CHECK(three_mm_actor_track_clone(&target, &source, &services));
    CHECK(strcmp(h.log, "reL++UL--UXY") == 0);
    CHECK(target.events != source.events && target.route != source.route && h.refs == 4 && h.live == 4);
    CHECK(memcmp(target.events, source.events, sizeof(Table)) == 0);
    reset_log();
    CHECK(three_mm_actor_track_clone(&source, &source, &services));
    CHECK(h.logged == 0);
    three_mm_actor_track_swap(&source, &source);
    CHECK(h.refs == 4);
    old = source;
    three_mm_actor_track_swap(&target, &source);
    CHECK(target.events == old.events && target.route == old.route);
    reset_log();
    three_mm_actor_track_close_tags(&target, &services);
    CHECK(strcmp(h.log, "L--U") == 0 && h.refs == 2);
    e = (Table *)target.events;
    {
        ThreeMMTag tag;
        memcpy(&tag, three_mm_actor_event_tag(e, 2, &services), sizeof(tag));
        CHECK(!tag.cache && tag.sid == 0 && tag.type == 123 && tag.number == 456);
    }
    reset_log();
    three_mm_actor_track_close_tags(&target, &services);
    CHECK(strcmp(h.log, "LU") == 0 && h.refs == 2);
    reset_log();
    three_mm_actor_track_dispose(&source, &services);
    CHECK(strcmp(h.log, "L--UXY") == 0);
    three_mm_actor_track_dispose(&source, &services);
    CHECK(!source.events && !source.route && h.refs == 0);
    CHECK(three_mm_actor_track_clone(&target, &source, &services));
    CHECK(!target.events && !target.route && !h.live);
}
static void tag_acceptance(void)
{
    ThreeMMActorTrack t, clone;
    Table *e;
    three_mm_actor_track_init(&t);
    three_mm_actor_track_init(&clone);
    reset_log();
    CHECK(three_mm_actor_track_create(&t, &services));
    e = (Table *)t.events;
    row(e, 0, THREE_MM_ACTOR_COSTUME, -1, 0, &h);
    row(e, 1, THREE_MM_ACTOR_COSTUME, 2, 0, &h);
    row(e, 2, THREE_MM_ACTOR_COSTUME, 0, 17, (void *)1);
    row(e, 3, THREE_MM_ACTOR_SOUND, 0, -1, (void *)1);
    row(e, 4, THREE_MM_ACTOR_SOUND, 0, 0, NULL);
    CHECK(!three_mm_actor_event_tag(e, 0, &services) && !three_mm_actor_event_tag(e, 1, &services));
    reset_log();
    CHECK(three_mm_actor_track_clone(&clone, &t, &services));
    CHECK(strcmp(h.log, "reLU") == 0);
    three_mm_actor_track_dispose(&t, &services);
    three_mm_actor_track_dispose(&clone, &services);
    CHECK(h.refs == 0 && h.live == 0);
}
static void adoption(void)
{
    ThreeMMActorTrack t, empty, copy;
    void *route, *events;
    three_mm_actor_track_init(&t);
    three_mm_actor_track_init(&empty);
    three_mm_actor_track_init(&copy);
    CHECK(three_mm_actor_track_create(&t, &services));
    populate(&t);
    reset_log();
    three_mm_actor_track_adopt_events(&t, t.events, &services);
    three_mm_actor_track_adopt_route(&t, t.route, &services);
    CHECK(!h.logged && h.refs == 2);
    events = allocate(0);
    route = allocate(1);
    CHECK(events != NULL && route != NULL);
    reset_log();
    three_mm_actor_track_adopt_events(&t, events, &services);
    CHECK(strcmp(h.log, "L--UX") == 0 && !h.refs);
    reset_log();
    three_mm_actor_track_adopt_route(&t, route, &services);
    CHECK(strcmp(h.log, "Y") == 0);
    three_mm_actor_track_adopt_events(&t, NULL, &services);
    CHECK(three_mm_actor_track_clone(&copy, &t, &services));
    CHECK(!copy.events && copy.route);
    three_mm_actor_track_dispose(&copy, &services);
    three_mm_actor_track_adopt_route(&t, NULL, &services);
    CHECK(!h.live);
    t.events = allocate(0);
    CHECK(t.events != NULL);
    CHECK(three_mm_actor_track_clone(&copy, &t, &services));
    CHECK(copy.events && !copy.route);
    CHECK(three_mm_actor_track_create(&t, &services));
    CHECK(((Table *)t.route)->growth == 32);
    three_mm_actor_track_dispose(&copy, &services);
    three_mm_actor_track_dispose(&t, &services);
    CHECK(three_mm_actor_track_clone(&copy, &empty, &services));
    CHECK(!h.live && !h.refs);
}
int main(void)
{
    transactions();
    tag_acceptance();
    adoption();
    CHECK(h.retains == 2 && h.releases == 8);
    puts("actor track passed");
    return 0;
}
