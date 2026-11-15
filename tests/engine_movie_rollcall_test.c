/* Roll-call ID publication, tag ownership, failure and notification checks. */
#include "three_mm/movie_rollcall.h"
#include <stdio.h>
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
    OPEN,
    APPEND,
    CLOSE,
    DUPLICATE,
    PUT,
    ERASE,
    CHANGED,
    PROP,
    TEXT
};
typedef struct Test Test;
typedef struct Cache
{
    int refs;
} Cache;
typedef struct Actor
{
    Test *test;
    ThreeMMI32 id;
    int prop, text;
    ThreeMMTag tag;
} Actor;
struct Test
{
    ThreeMMMovieState data;
    ThreeMMMovieRollCallServices s;
    ThreeMMRollCallActor rows[8];
    int names[8], count, changed, reads, nameReads, renameFail, appendFail, issue, diagnostics;
    int events[64], eventCount, textCalls, idPublishedLimit;
    Cache source, destination;
    Actor actor;
};
static void event(Test *t, int value)
{
    t->events[t->eventCount++] = value;
}
static ThreeMMI32 count(void *p)
{
    return ((Test *)p)->count;
}
static void row(void *p, ThreeMMI32 i, ThreeMMRollCallActor *out)
{
    Test *t = p;
    ++t->reads;
    *out = t->rows[i];
}
static void put(void *p, ThreeMMI32 i, const ThreeMMRollCallActor *value)
{
    Test *t = p;
    event(t, PUT);
    t->rows[i] = *value;
}
static void name(void *p, ThreeMMI32 i, void *out)
{
    Test *t = p;
    ++t->nameReads;
    if (out)
        *(int *)out = t->names[i];
}
static int rename_row(void *p, ThreeMMI32 i, const void *name_value)
{
    Test *t = p;
    if (t->renameFail)
        return 0;
    t->names[i] = *(const int *)name_value;
    return 1;
}
static int append(void *p, const void *name_value, const ThreeMMRollCallActor *value)
{
    Test *t = p;
    event(t, APPEND);
    if (t->appendFail)
        return 0;
    t->names[t->count] = *(const int *)name_value;
    t->rows[t->count++] = *value;
    return 1;
}
static void erase(void *p, ThreeMMI32 i)
{
    Test *t = p;
    event(t, ERASE);
    memmove(t->rows + i, t->rows + i + 1, (t->count - i - 1) * sizeof(*t->rows));
    memmove(t->names + i, t->names + i + 1, (t->count - i - 1) * sizeof(*t->names));
    --t->count;
}
static ThreeMMI32 actor_id(void *p)
{
    return ((Actor *)p)->id;
}
static void set_id(void *p, ThreeMMI32 id)
{
    Actor *a = p;
    a->id = id;
    a->test->idPublishedLimit = a->test->data.actorIdLimit;
    event(a->test, SET_ID);
}
static void actor_tag(void *p, ThreeMMTag *out)
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
static int same(const ThreeMMTag *a, const ThreeMMTag *b)
{
    return a->sid == b->sid && a->cache == b->cache && a->type == b->type && a->number == b->number;
}
static void open_tag(void *p, ThreeMMTag *tag, void *cache)
{
    Test *t = p;
    event(t, OPEN);
    if (!tag->sid)
    {
        tag->cache = cache;
        ++((Cache *)cache)->refs;
    }
}
static void duplicate(const ThreeMMTag *tag)
{
    if (!tag->sid)
        ++((Cache *)tag->cache)->refs;
}
static Test *active;
static void duplicate_tag(const ThreeMMTag *tag)
{
    event(active, DUPLICATE);
    duplicate(tag);
}
static void close_tag(ThreeMMTag *tag)
{
    event(active, CLOSE);
    if (!tag->sid)
    {
        --((Cache *)tag->cache)->refs;
        tag->cache = 0;
    }
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
    ThreeMMMovieRollCallServices *s;
    memset(t, 0, sizeof(*t));
    active = t;
    t->source.refs = 10;
    t->destination.refs = 20;
    t->data.actors = t;
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
    s->count = count;
    s->row = row;
    s->put = put;
    s->name = name;
    s->rename = rename_row;
    s->append = append;
    s->erase = erase;
    s->actor_id = actor_id;
    s->set_actor_id = set_id;
    s->actor_tag = actor_tag;
    s->is_prop = prop;
    s->is_text = text;
    s->same_tag = same;
    s->open_tag = open_tag;
    s->duplicate_tag = duplicate_tag;
    s->close_tag = close_tag;
    s->changed = changed;
    s->diagnostic = diagnostic;
}
static int clean(Test *t)
{
    int i;
    ThreeMMTag tag;
    for (i = 0; i < t->count; ++i)
    {
        tag = three_mm_movie_rollcall_tag(&t->rows[i]);
        close_tag(&tag);
    }
    return t->source.refs == 10 && t->destination.refs == 20;
}
static int insertion(void)
{
    Test t;
    int flags, name_value = 123;
    for (flags = 0; flags < 4; ++flags)
    {
        init(&t);
        t.actor.prop = flags & 1;
        t.actor.text = (flags & 2) != 0;
        CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
        CHECK(t.actor.id == 7 && t.data.actorIdLimit == 8 && t.idPublishedLimit == 8 && t.events[0] == SET_ID);
        CHECK(t.count == 1 && t.names[0] == 123 && t.rows[0].data.reference_count == 1 && t.rows[0].data.actor_id == 7);
        CHECK(t.rows[0].data.browser_flags == ((flags & 1) ? (flags & 2) ? 3U : 1U : 0U));
        CHECK(t.textCalls == !!(flags & 1) && t.changed == 1 && t.destination.refs == 21 && t.source.refs == 10);
        CHECK(three_mm_movie_rollcall_has(0, THREE_MM_MOVIE_PROP, &t.s) == !!(flags & 1));
        CHECK(three_mm_movie_rollcall_has(0, THREE_MM_MOVIE_TEXT, &t.s) == (flags == 3));
        CHECK(clean(&t));
    }
    init(&t);
    t.appendFail = 1;
    CHECK(!three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
    CHECK(t.actor.id == 7 && t.data.actorIdLimit == 8 && !t.count && !t.changed && t.destination.refs == 20);
    CHECK(t.events[t.eventCount - 2] == APPEND && t.events[t.eventCount - 1] == CLOSE && clean(&t));
    init(&t);
    t.actor.id = 99;
    CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s) && t.data.actorIdLimit == 7 && t.actor.id == 99 &&
          clean(&t));
    return 0;
}
static int refresh(void)
{
    Test t;
    int name_value = 11;
    init(&t);
    CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
    t.eventCount = 0;
    CHECK(three_mm_movie_rollcall_add(&t.actor, 0, &t.s));
    CHECK(t.rows[0].data.reference_count == 2 && t.changed == 2 && t.destination.refs == 20 && t.source.refs == 11);
    CHECK(t.eventCount == 4 && t.events[0] == CLOSE && t.events[1] == DUPLICATE && t.events[2] == PUT &&
          t.events[3] == CHANGED);
    t.eventCount = 0;
    CHECK(three_mm_movie_rollcall_add(&t.actor, 0, &t.s));
    CHECK(t.rows[0].data.reference_count == 3 && t.changed == 3 && t.source.refs == 11 && t.events[0] == PUT &&
          t.events[1] == CHANGED);
    CHECK(clean(&t));
    return 0;
}
static int queries(void)
{
    Test t;
    ThreeMMTag tag;
    ThreeMMI32 id = 101, refs = 202;
    int name_value = 11, output = 303;
    init(&t);
    memset(&tag, 0, sizeof(tag));
    tag.number = 404;
    CHECK(!three_mm_movie_rollcall_get(-1, &id, &output, &refs, &tag, &t.s));
    CHECK(!three_mm_movie_rollcall_get(0, &id, &output, &refs, &tag, &t.s));
    CHECK(id == 101 && refs == 202 && output == 303 && tag.number == 404 && !t.reads && !t.nameReads);
    CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
    CHECK(three_mm_movie_rollcall_get(0, &id, &output, &refs, &tag, &t.s));
    CHECK(id == 7 && refs == 1 && output == 11 && tag.cache == &t.destination && t.destination.refs == 21);
    output = 999;
    CHECK(!three_mm_movie_rollcall_name(9, &output, &t.s) && output == 999);
    CHECK(three_mm_movie_rollcall_name(7, &output, &t.s) && output == 11);
    name_value = 22;
    t.renameFail = 1;
    CHECK(!three_mm_movie_rollcall_rename(7, &name_value, &t.s) && t.changed == 1 && t.names[0] == 11);
    t.renameFail = 0;
    CHECK(three_mm_movie_rollcall_rename(7, &name_value, &t.s) && t.changed == 2 && t.names[0] == 22);
    t.eventCount = 0;
    tag.number = 88;
    three_mm_movie_rollcall_change_tag(7, &tag, &t.s);
    CHECK(t.rows[0].data.template_number == 88 && t.eventCount == 1 && t.events[0] == PUT && t.destination.refs == 21);
    three_mm_movie_rollcall_change_tag(8, &tag, &t.s);
    CHECK(t.diagnostics == 1 && t.issue == THREE_MM_ROLLCALL_NO_ACTOR && clean(&t));
    return 0;
}
static int removal(void)
{
    Test t;
    int name_value = 11, erase_last;
    for (erase_last = 0; erase_last <= 1; ++erase_last)
    {
        init(&t);
        CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
        CHECK(three_mm_movie_rollcall_add(&t.actor, 0, &t.s));
        t.eventCount = 0;
        three_mm_movie_rollcall_remove(&t.actor, erase_last, &t.s);
        CHECK(t.rows[0].data.reference_count == 1 && t.changed == 2 && t.eventCount == 1 && t.events[0] == PUT);
        t.eventCount = 0;
        three_mm_movie_rollcall_remove(&t.actor, erase_last, &t.s);
        CHECK(t.changed == 3 && t.count == !erase_last);
        if (erase_last)
            CHECK(t.eventCount == 3 && t.events[0] == CLOSE && t.events[1] == ERASE && t.events[2] == CHANGED);
        else
            CHECK(t.rows[0].data.reference_count == 0 && t.eventCount == 2 && t.events[0] == PUT &&
                  t.events[1] == CHANGED);
        CHECK(clean(&t));
    }
    init(&t);
    three_mm_movie_rollcall_remove(&t.actor, 1, &t.s);
    CHECK(t.issue == THREE_MM_ROLLCALL_BAD_REMOVE && t.diagnostics == 1 && !t.count);
    init(&t);
    CHECK(three_mm_movie_rollcall_add(&t.actor, &name_value, &t.s));
    t.rows[0].data.reference_count = 0;
    three_mm_movie_rollcall_remove(&t.actor, 1, &t.s);
    CHECK(t.issue == THREE_MM_ROLLCALL_UNDERFLOW && t.rows[0].data.reference_count == -1 && t.changed == 1 &&
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
