/* Roll-call ID publication, tag ownership, failure and notification checks. */
#include "mm/movie_rollcall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
enum
{
    SET_ID = 1,
    RETAIN_DOCUMENT,
    CLOSE,
    RETAIN_SOURCE,
    CHANGED,
    PROP,
    TEXT
};
typedef struct Test Test;
typedef struct Cache
{
    Test *test;
    int refs;
} Cache;
typedef struct Actor
{
    Test *test;
    MMI32 id;
    int prop, text;
    MMTag tag;
} Actor;
struct Test
{
    MMMovieState data;
    MMMovieRollCallServices s;
    MMTagServices tags;
    int changed, fail_allocation, issue, diagnostics;
    int events[64], eventCount, textCalls, idPublishedLimit;
    Cache source, destination;
    Actor actor;
};
static void event(Test *t, int value)
{
    t->events[t->eventCount++] = value;
}
static void *resize(void *p, void *data, MMU32 old_size, MMU32 size, int priority)
{
    (void)old_size;
    (void)priority;
    return ((Test *)p)->fail_allocation ? NULL : realloc(data, size);
}
static void release(void *p, void *data, MMU32 size)
{
    (void)p;
    (void)size;
    free(data);
}
static MMRollCallActor row(Test *t, MMU32 i)
{
    MMRollCallActor value;
    if (!mm_rollcall_table_extra(&t->data.actors, i, &value))
        abort();
    return value;
}
static int named(Test *t, MMU32 i, const void *text, MMU32 count)
{
    const void *actual;
    MMU32 length;
    return mm_rollcall_table_name(&t->data.actors, i, &actual, &length) && length == count && !memcmp(actual, text, count);
}
static MMI32 actor_id(void *p)
{
    return ((Actor *)p)->id;
}
static void set_id(void *p, MMI32 id)
{
    Actor *a = p;
    a->id = id;
    a->test->idPublishedLimit = a->test->data.actorIdLimit;
    event(a->test, SET_ID);
}
static void actor_tag(void *p, MMTag *out)
{
    *out = ((Actor *)p)->tag;
}
static int prop(void *p)
{
    Actor *a = p;
    event(a->test, PROP);
    return a->prop;
}
static int text(void *p)
{
    Actor *a = p;
    ++a->test->textCalls;
    event(a->test, TEXT);
    return a->text;
}
static int compare_cache(void *a, void *b)
{
    const Cache *cache = a;
    return a == b ? 0 : cache == &cache->test->source ? -1 : 1;
}
static void retain_cache(void *p)
{
    Cache *cache = p;
    Test *t = cache->test;
    event(t, cache == &t->destination ? RETAIN_DOCUMENT : RETAIN_SOURCE);
    ++cache->refs;
}
static void release_cache(void *p)
{
    Cache *cache = p;
    event(cache->test, CLOSE);
    --cache->refs;
}
static void changed(void *p)
{
    Test *t = p;
    ++t->changed;
    event(t, CHANGED);
}
static void diagnostic(void *p, int issue)
{
    Test *t = p;
    t->issue = issue;
    ++t->diagnostics;
}
static void init(Test *t)
{
    MMMovieRollCallServices *s;
    MMStorageAllocator allocator = {NULL, resize, release};
    memset(t, 0, sizeof(*t));
    allocator.context = t;
    t->source.test = t->destination.test = t;
    t->source.refs = 10;
    t->destination.refs = 20;
    if (mm_rollcall_table_init(&t->data.actors, sizeof(MMRollCallActor), 1, 0x0303, 0, &allocator) != MM_STORAGE_OK)
        abort();
    t->data.resources = &t->destination;
    t->data.actorIdLimit = 7;
    t->actor.test = t;
    t->actor.id = -1;
    t->actor.tag.cache = &t->source;
    t->actor.tag.type = 100;
    t->actor.tag.number = 3;
    s = &t->s;
    s->context = t;
    s->data = &t->data;
    s->actor_id = actor_id;
    s->set_actor_id = set_id;
    s->actor_tag = actor_tag;
    s->is_prop = prop;
    s->is_text = text;
    t->tags.compare_cache = compare_cache;
    t->tags.retain = retain_cache;
    t->tags.release = release_cache;
    s->tags = &t->tags;
    s->changed = changed;
    s->diagnostic = diagnostic;
}
static int clean(Test *t)
{
    MMU32 i;
    MMRollCallActor value;
    MMTag tag;
    for (i = 0; i < t->data.actors.count; ++i)
    {
        value = row(t, i);
        tag = mm_movie_rollcall_tag(&value);
        mm_tag_close(&tag, &t->tags);
    }
    return mm_rollcall_table_dispose(&t->data.actors) == MM_STORAGE_OK && t->source.refs == 10 && t->destination.refs == 20;
}
static int insertion(void)
{
    Test t;
    int flags;
    for (flags = 0; flags < 4; ++flags)
    {
        init(&t);
        t.actor.prop = flags & 1;
        t.actor.text = (flags & 2) != 0;
        CHECK(mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
        CHECK(t.actor.id == 7 && t.data.actorIdLimit == 8 && t.idPublishedLimit == 8 && t.events[0] == SET_ID);
        CHECK(t.data.actors.count == 1 && named(&t, 0, "Actor", 5) && row(&t, 0).data.reference_count == 1 &&
              row(&t, 0).data.actor_id == 7);
        CHECK(row(&t, 0).data.browser_flags == ((flags & 1) ? (flags & 2) ? 3U : 1U : 0U));
        CHECK(t.textCalls == !!(flags & 1) && t.changed == 1 && t.destination.refs == 21 && t.source.refs == 10);
        CHECK(mm_movie_rollcall_has(&t.data.actors, 0, MM_MOVIE_PROP) == !!(flags & 1));
        CHECK(mm_movie_rollcall_has(&t.data.actors, 0, MM_MOVIE_TEXT) == (flags == 3));
        CHECK(clean(&t));
    }
    init(&t);
    t.fail_allocation = 1;
    CHECK(!mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
    CHECK(t.actor.id == 7 && t.data.actorIdLimit == 8 && !t.data.actors.count && !t.changed && t.destination.refs == 20);
    CHECK(t.events[t.eventCount - 2] == RETAIN_DOCUMENT && t.events[t.eventCount - 1] == CLOSE && clean(&t));
    init(&t);
    t.actor.id = 99;
    CHECK(mm_movie_rollcall_add(&t.actor, "", 0, &t.s) && t.data.actorIdLimit == 7 && t.actor.id == 99 &&
          named(&t, 0, "", 0) &&
          clean(&t));
    return 0;
}
static int refresh(void)
{
    Test t;
    init(&t);
    CHECK(mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
    t.eventCount = 0;
    CHECK(mm_movie_rollcall_add(&t.actor, NULL, 0, &t.s));
    CHECK(row(&t, 0).data.reference_count == 2 && t.changed == 2 && t.destination.refs == 20 && t.source.refs == 11);
    CHECK(t.eventCount == 3 && t.events[0] == CLOSE && t.events[1] == RETAIN_SOURCE && t.events[2] == CHANGED);
    t.eventCount = 0;
    CHECK(mm_movie_rollcall_add(&t.actor, NULL, 0, &t.s));
    CHECK(row(&t, 0).data.reference_count == 3 && t.changed == 3 && t.source.refs == 11 &&
          t.eventCount == 1 && t.events[0] == CHANGED && named(&t, 0, "Actor", 5));
    t.actor.tag.sid = 3;
    t.actor.tag.cache = NULL;
    t.eventCount = 0;
    CHECK(mm_movie_rollcall_add(&t.actor, NULL, 0, &t.s));
    CHECK(row(&t, 0).data.source_id == 3 && !row(&t, 0).cache && t.source.refs == 10 &&
          t.eventCount == 2 && t.events[0] == CLOSE && t.events[1] == CHANGED);
    CHECK(clean(&t));
    return 0;
}
static int queries(void)
{
    Test t;
    MMTag tag;
    MMI32 id = 101, refs = 202;
    const void *name = &id;
    MMU32 count = 303;
    char large[200];
    const char new_name[] = {'N', '\0', 'M'};
    init(&t);
    memset(large, 'X', sizeof(large));
    memset(&tag, 0, sizeof(tag));
    tag.number = 404;
    CHECK(!mm_movie_rollcall_get(&t.data.actors, -1, &id, &name, &count, &refs, &tag));
    CHECK(!mm_movie_rollcall_get(&t.data.actors, 0, &id, &name, &count, &refs, &tag));
    CHECK(id == 101 && refs == 202 && count == 303 && name == &id && tag.number == 404);
    CHECK(mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
    CHECK(mm_movie_rollcall_get(&t.data.actors, 0, &id, &name, &count, &refs, &tag));
    CHECK(id == 7 && refs == 1 && count == 5 && !memcmp(name, "Actor", count) &&
          tag.cache == &t.destination && t.destination.refs == 21);
    name = &id;
    count = 999;
    CHECK(!mm_movie_rollcall_name(&t.data.actors, 9, &name, &count) && name == &id && count == 999);
    CHECK(mm_movie_rollcall_name(&t.data.actors, 7, &name, &count) && count == 5 && !memcmp(name, "Actor", count));
    t.fail_allocation = 1;
    CHECK(!mm_movie_rollcall_rename(7, large, sizeof(large), &t.s) && t.changed == 1 && named(&t, 0, "Actor", 5));
    t.fail_allocation = 0;
    CHECK(mm_movie_rollcall_rename(7, new_name, sizeof(new_name), &t.s) && t.changed == 2 &&
          named(&t, 0, new_name, sizeof(new_name)));
    t.eventCount = 0;
    tag.number = 88;
    mm_movie_rollcall_change_tag(7, &tag, &t.s);
    CHECK(row(&t, 0).data.template_number == 88 && !t.eventCount && t.destination.refs == 21);
    mm_movie_rollcall_change_tag(8, &tag, &t.s);
    CHECK(t.diagnostics == 1 && t.issue == MM_ROLLCALL_NO_ACTOR && clean(&t));
    return 0;
}
static int removal(void)
{
    Test t;
    int erase_last;
    MMRollCallActor value;
    for (erase_last = 0; erase_last <= 1; ++erase_last)
    {
        init(&t);
        CHECK(mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
        CHECK(mm_movie_rollcall_add(&t.actor, NULL, 0, &t.s));
        t.eventCount = 0;
        mm_movie_rollcall_remove(&t.actor, erase_last, &t.s);
        CHECK(row(&t, 0).data.reference_count == 1 && t.changed == 2 && !t.eventCount);
        t.eventCount = 0;
        mm_movie_rollcall_remove(&t.actor, erase_last, &t.s);
        CHECK(t.changed == 3 && t.data.actors.count == (MMU32)!erase_last);
        if (erase_last)
            CHECK(t.eventCount == 2 && t.events[0] == CLOSE && t.events[1] == CHANGED);
        else
            CHECK(row(&t, 0).data.reference_count == 0 && t.eventCount == 1 && t.events[0] == CHANGED);
        CHECK(clean(&t));
    }
    init(&t);
    mm_movie_rollcall_remove(&t.actor, 1, &t.s);
    CHECK(t.issue == MM_ROLLCALL_BAD_REMOVE && t.diagnostics == 1 && !t.data.actors.count && clean(&t));
    init(&t);
    CHECK(mm_movie_rollcall_add(&t.actor, "Actor", 5, &t.s));
    value = row(&t, 0);
    value.data.reference_count = 0;
    CHECK(mm_rollcall_table_set_extra(&t.data.actors, 0, &value));
    mm_movie_rollcall_remove(&t.actor, 1, &t.s);
    CHECK(t.issue == MM_ROLLCALL_UNDERFLOW && row(&t, 0).data.reference_count == -1 && t.changed == 1 &&
          clean(&t));
    return 0;
}
int main(void)
{
    if (insertion() || refresh() || queries() || removal())
        return 1;
    puts("Movie roll-call ownership, publication, queries and failure cleanup passed");
    return 0;
}
