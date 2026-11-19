/* Movie content transaction ownership, failure prefixes and caption selection. */
#include "mm/movie_content.h"
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
    ENSURE = 1,
    NEW_TEXT,
    VALID,
    FILE_GET,
    TEMP,
    WRITE,
    TEXT_RELEASE,
    OPEN,
    CLOSE,
    DUP,
    TEMPLATE,
    SET_ID,
    ROLL_CHANGED,
    ACTOR_RELEASE,
    RESTORE,
    DIRTY,
    NEW_CAPTION,
    DEFAULT_FONT,
    FONT,
    ADD_CAPTION,
    SELECT_CAPTION,
    ATTACH,
    CAPTION_RELEASE
};
enum
{
    FAIL_ENSURE = 1,
    FAIL_TEXT,
    FAIL_VALID,
    FAIL_TEMP,
    FAIL_WRITE,
    FAIL_OPEN,
    FAIL_CACHE,
    FAIL_ACTOR,
    FAIL_ADD,
    FAIL_DUP,
    FAIL_TEMPLATE,
    FAIL_APPEND,
    FAIL_CAPTION,
    FAIL_CAPTION_ADD,
    FAIL_CAPTION_EDIT
};
typedef struct Test Test;
typedef struct Resource
{
    Test *test;
    int id;
} Resource;
typedef struct Name
{
    Test *test;
    const char *text;
} Name;
typedef struct Actor
{
    Test *test;
    int refs, id, value;
    MMTag tag;
} Actor;
typedef struct Caption
{
    Test *test;
    int refs, visible, font;
} Caption;
struct Test
{
    MMMovieContentServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    Resource before, after;
    Name name;
    Actor actor, copy, inserted;
    Caption captions[3], *selected;
    int fail, events[128], n, refs, opened, closed, textRefs, writes, chunks;
    int dirty, invalidates, bars, releases, restored, removeActor, nuked, cacheBegin, cacheEnd;
    int captionCount, attached, missing, hidden, removed, queries, query[8], characterCalls;
    void *writtenFile, *openedResources;
};
static void note(Test *t, int e)
{
    t->events[t->n++] = e;
}
static int ensure(void *p, void **out)
{
    Test *t = p;
    note(t, ENSURE);
    *out = &t->before;
    return t->fail != FAIL_ENSURE;
}
static int valid(void *p)
{
    Test *t = p;
    note(t, VALID);
    return t->fail != FAIL_VALID;
}
static void *file(void *p)
{
    Resource *r = p;
    note(r->test, FILE_GET);
    return r;
}
static int temp(void *p)
{
    Test *t = p;
    note(t, TEMP);
    t->data.resources = &t->after;
    return t->fail != FAIL_TEMP;
}
static int write_asset(void *p, void *f, MMU32 *number)
{
    Test *t = p;
    note(t, WRITE);
    ++t->writes;
    t->writtenFile = f;
    ++t->chunks;
    *number = 27;
    return t->fail != FAIL_WRITE;
}
static void *text(const void *p, MMI32 shape, const void *font_input)
{
    const MMTag *font = font_input;
    const Name *name = p;
    Test *t = name->test;
    note(t, NEW_TEXT);
    if (shape != 4 || font->sid != 123 || t->fail == FAIL_TEXT)
        return 0;
    ++t->textRefs;
    return t;
}
static void release_text(void *p)
{
    Test *t = p;
    note(t, TEXT_RELEASE);
    --t->textRefs;
}
static int open_tag(MMTag *tag, void *p)
{
    Resource *r = p;
    Test *t = r->test;
    note(t, OPEN);
    t->openedResources = p;
    if (t->fail == FAIL_OPEN)
        return 0;
    tag->cache = t;
    ++t->refs;
    ++t->opened;
    return 1;
}
static void close_tag(MMTag *tag)
{
    Test *t = tag->cache;
    note(t, CLOSE);
    --t->refs;
    ++t->closed;
    tag->cache = 0;
}
static const void *name_units(const void *p, MMU32 *count)
{
    const Name *name = p;
    *count = (MMU32)strlen(name->text);
    return name->text;
}
static MMI32 character(const void *p, MMI32 i)
{
    const Name *n = p;
    ++n->test->characterCalls;
    return (unsigned char)n->text[i];
}
static void *duplicate(void *p, int reset)
{
    Actor *a = p;
    Test *t = a->test;
    note(t, DUP);
    if (reset || t->fail == FAIL_DUP)
        return 0;
    t->copy = *a;
    t->copy.refs = 1;
    return &t->copy;
}
static void release_actor(void *p)
{
    Actor *a = p;
    note(a->test, ACTOR_RELEASE);
    --a->refs;
    ++a->test->releases;
}
static void restore(void *p, void *q)
{
    Actor *a = p, *copy = q;
    note(a->test, RESTORE);
    ++a->test->restored;
    a->id = copy->id;
    a->value = copy->value;
    a->tag = copy->tag;
}
static int change(void *p, const MMTag *tag)
{
    Actor *a = p;
    note(a->test, TEMPLATE);
    a->value = 99;
    if (a->test->fail == FAIL_TEMPLATE)
        return 0;
    a->tag = *tag;
    return 1;
}
static MMI32 actor_id(void *p)
{
    return ((Actor *)p)->id;
}
static void set_id(void *p, MMI32 id)
{
    Actor *a = p;
    note(a->test, SET_ID);
    a->id = id;
}
static void actor_tag(void *p, MMTag *tag)
{
    *tag = ((Actor *)p)->tag;
}
static int is_prop(void *p)
{
    (void)p;
    return 1;
}
static void *resize(void *p, void *data, MMU32 old_size, MMU32 size, int priority)
{
    (void)old_size;
    (void)priority;
    return ((Test *)p)->fail == FAIL_APPEND ? NULL : realloc(data, size);
}
static void release(void *p, void *data, MMU32 size)
{
    (void)p;
    (void)size;
    free(data);
}
static int seed_rollcall(Test *t)
{
    MMStorageAllocator allocator = {NULL, resize, release};
    MMRollCallActor row;
    allocator.context = t;
    memset(&row, 0, sizeof(row));
    row.data.actor_id = 7;
    row.data.reference_count = 2;
    return mm_rollcall_table_init(&t->data.actors, sizeof(row), 1, 0x0303, 0, &allocator) == MM_STORAGE_OK &&
           mm_rollcall_table_add(&t->data.actors, "Before", 6, &row);
}
static void open_row(void *p, MMTag *tag, void *r)
{
    (void)p;
    open_tag(tag, r);
}
static void roll_changed(void *p)
{
    note(p, ROLL_CHANGED);
}
static void dirty(void *p)
{
    Test *t = p;
    note(t, DIRTY);
    ++t->dirty;
}
static void invalidate(void *p, int bars)
{
    Test *t = p;
    ++t->invalidates;
    t->bars = bars;
}
static int cache(void *p, const MMTag *tag, int background)
{
    Test *t = p;
    return !background && tag->type == 0x544d504cU && t->fail != FAIL_CACHE;
}
static void long_op(void *p, int begin)
{
    Test *t = p;
    if (begin)
        ++t->cacheBegin;
    else
        ++t->cacheEnd;
}
static void *new_actor(void *p, const MMTag *tag)
{
    Test *t = p;
    if (t->fail == FAIL_ACTOR)
        return 0;
    t->inserted.tag = *tag;
    t->inserted.refs = 1;
    return &t->inserted;
}
static int add_actor(void *p, void *actor)
{
    Test *t = p;
    if (t->fail == FAIL_ADD)
        return 0;
    ++((Actor *)actor)->refs;
    return 1;
}
static void *selected_actor(void *p)
{
    return &((Test *)p)->actor;
}
static int remove_actor(void *p, MMI32 id)
{
    Test *t = p;
    ++t->removeActor;
    return id == 7 && t->fail != FAIL_ADD;
}
static void nuked(void *p)
{
    ++((Test *)p)->nuked;
}
static void *new_caption(void *p, const void *bounds, int story)
{
    Test *t = p;
    note(t, NEW_CAPTION);
    if (bounds != t || story != 1 || t->fail == FAIL_CAPTION)
        return 0;
    t->captions[0].refs = 1;
    return &t->captions[0];
}
static MMI32 default_font(void *p)
{
    note(p, DEFAULT_FONT);
    return 23;
}
static void caption_font(void *p, MMI32 value)
{
    Caption *c = p;
    note(c->test, FONT);
    c->font = value;
}
static int add_caption(void *p, void *q)
{
    Test *t = p;
    note(t, ADD_CAPTION);
    ++t->captionCount;
    if (t->fail == FAIL_CAPTION_ADD)
        return 0;
    ++((Caption *)q)->refs;
    return 1;
}
static void select_caption(void *p, void *q)
{
    Test *t = p;
    note(t, SELECT_CAPTION);
    t->selected = q;
}
static void attach(void *p)
{
    Caption *c = p;
    note(c->test, ATTACH);
    ++c->test->attached;
}
static void release_caption(void *p)
{
    Caption *c = p;
    note(c->test, CAPTION_RELEASE);
    --c->refs;
}
static void *selected_caption(void *p)
{
    return ((Test *)p)->selected;
}
static int remove_caption(void *p, void *q)
{
    Test *t = p;
    (void)q;
    ++t->removed;
    return t->fail != FAIL_CAPTION_EDIT;
}
static int hide_caption(void *p)
{
    Caption *c = p;
    ++c->test->hidden;
    return c->test->fail != FAIL_CAPTION_EDIT;
}
static int visible_caption(void *p)
{
    return ((Caption *)p)->visible;
}
static void *caption_at(void *p, MMI32 index)
{
    Test *t = p;
    t->query[t->queries++] = index;
    return index < 3 ? &t->captions[index] : 0;
}
static void missing(void *p)
{
    ++((Test *)p)->missing;
}
static void init(Test *t)
{
    MMMovieContentServices *s = &t->s;
    MMMovieEditServices *e = &s->edit;
    memset(t, 0, sizeof(*t));
    t->before.test = t;
    t->after.test = t;
    t->before.id = 1;
    t->after.id = 2;
    t->name.test = t;
    t->name.text = " A";
    t->actor.test = t;
    t->actor.id = 7;
    t->actor.value = 11;
    t->actor.refs = 1;
    t->inserted.test = t;
    t->inserted.id = -1;
    t->data.resources = &t->before;
    t->data.actorIdLimit = 10;
    t->scenes.open = t;
    e->context = t;
    e->data = &t->data;
    e->scenes = &t->scenes;
    e->rollcall.context = t;
    e->rollcall.data = &t->data;
    e->rollcall.actor_id = actor_id;
    e->rollcall.set_actor_id = set_id;
    e->rollcall.actor_tag = actor_tag;
    e->rollcall.is_prop = is_prop;
    e->rollcall.is_text = is_prop;
    e->rollcall.open_tag = open_row;
    e->rollcall.close_tag = close_tag;
    e->rollcall.changed = roll_changed;
    e->duplicate_actor = duplicate;
    e->release_actor = release_actor;
    e->restore_actor = restore;
    e->dirty = dirty;
    e->invalidate = invalidate;
    e->cache_tag = cache;
    e->long_op = long_op;
    e->new_actor = new_actor;
    e->add_actor = add_actor;
    e->selected_actor = selected_actor;
    e->remove_actor = remove_actor;
    e->actor_nuked = nuked;
    s->ensure = ensure;
    s->make_valid = valid;
    s->use_temp = temp;
    s->file = file;
    s->write_material = write_asset;
    s->new_text = text;
    s->write_text = write_asset;
    s->release_text = release_text;
    s->open_tag = open_tag;
    s->close_tag = close_tag;
    s->name_units = name_units;
    s->character = character;
    s->change_template = change;
    s->new_caption = new_caption;
    s->default_font = default_font;
    s->caption_font = caption_font;
    s->add_caption = add_caption;
    s->select_caption = select_caption;
    s->attach_caption = attach;
    s->release_caption = release_caption;
    s->selected_caption = selected_caption;
    s->caption_at = caption_at;
    s->hide_caption = hide_caption;
    s->remove_caption = remove_caption;
    s->visible_caption = visible_caption;
    s->missing_caption = missing;
    t->captions[0].test = t;
    t->captions[1].test = t;
    t->captions[2].test = t;
    t->captions[0].visible = t->captions[2].visible = 1;
}
static int material(void)
{
    Test t;
    MMTag output;
    int written, fault;
    for (fault = 0; fault < 4; ++fault)
    {
        init(&t);
        t.fail = fault == 1 ? FAIL_ENSURE : fault == 2 ? FAIL_WRITE : fault == 3 ? FAIL_OPEN : 0;
        memset(&output, 0, sizeof(output));
        output.number = 999;
        CHECK(mm_movie_insert_material(&t, &output, &written, &t.s) == !fault);
        CHECK(written == (fault != 1 && fault != 2));
        CHECK(t.writes == (fault != 1) && t.chunks == (fault != 1) && !t.dirty && !t.invalidates);
        if (written)
            CHECK(output.sid == 0 && output.type == 0x4d54524cU && output.number == 27);
        else
            CHECK(output.number == 999);
        if (!fault)
        {
            CHECK(t.refs == 1 && output.cache == &t);
            close_tag(&output);
        }
        CHECK(!t.refs);
    }
    return 0;
}
static int inserting(void)
{
    Test t;
    MMTag font;
    int fault;
    memset(&font, 0, sizeof(font));
    font.sid = 123;
    for (fault = 0; fault < 9; ++fault)
    {
        const int faults[] = {0,         FAIL_TEXT,  FAIL_VALID, FAIL_TEMP, FAIL_WRITE,
                              FAIL_OPEN, FAIL_CACHE, FAIL_ACTOR, FAIL_ADD};
        init(&t);
        t.fail = faults[fault];
        CHECK(mm_movie_insert_text(&t.name, 4, &font, &t.s) == !fault);
        CHECK(!t.textRefs && !t.refs && t.opened == t.closed && t.dirty == !fault && t.invalidates == !fault);
        CHECK(t.inserted.refs == !fault && t.cacheBegin == t.cacheEnd);
        if (fault != 1 && fault != 2 && fault != 3)
            CHECK(t.writtenFile == &t.before);
        if (!fault || fault >= 5)
            CHECK(t.openedResources == &t.after);
        if (!fault)
        {
            const int prefix[] = {NEW_TEXT,     VALID, FILE_GET,      TEMP,  WRITE,
                                  TEXT_RELEASE, OPEN,  ACTOR_RELEASE, DIRTY, CLOSE};
            CHECK(t.n == 10 && !memcmp(t.events, prefix, sizeof(prefix)) && t.bars && t.chunks == 1);
            release_actor(&t.inserted);
        }
    }
    return 0;
}
static int changing(void)
{
    Test t;
    MMTag font;
    MMRollCallActor row;
    char long_name[161];
    const void *name;
    MMU32 length;
    int fault;
    const int faults[] = {0,         FAIL_TEXT, FAIL_VALID,    FAIL_TEMP,  FAIL_WRITE,
                          FAIL_OPEN, FAIL_DUP,  FAIL_TEMPLATE, FAIL_APPEND};
    memset(&font, 0, sizeof(font));
    memset(long_name, 'X', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    font.sid = 123;
    for (fault = 0; fault < 9; ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        t.fail = faults[fault];
        if (t.fail == FAIL_APPEND)
            t.name.text = long_name;
        CHECK(mm_movie_change_text(&t.actor, &t.name, 4, &font, &t.s) == !fault);
        CHECK(!t.copy.refs && !t.textRefs && t.refs == !fault && t.dirty == !fault && !t.invalidates);
        CHECK(t.restored == (fault == 8) && t.data.actors.count == (MMU32)(1 + !fault) &&
              t.data.actorIdLimit == (fault == 0 || fault == 8 ? 11 : 10));
        if (!fault)
        {
            const int suffix[] = {SET_ID, OPEN, ROLL_CHANGED, ACTOR_RELEASE, CLOSE, DIRTY};
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row));
            CHECK(t.actor.id == 10 && t.actor.value == 99 && row.data.reference_count == 1);
            CHECK(t.n >= 6 && !memcmp(t.events + t.n - 6, suffix, sizeof(suffix)));
            CHECK(mm_rollcall_table_name(&t.data.actors, 1, &name, &length) && length == 2 && !memcmp(name, " A", length));
            CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
            font = mm_movie_rollcall_tag(&row);
            close_tag(&font);
            font.sid = 123;
        }
        if (fault == 8)
        {
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row));
            CHECK(t.actor.id == 7 && t.actor.value == 11 && row.data.reference_count == 2);
        }
        if (fault == 7)
            CHECK(t.actor.value == 99 && t.actor.id == 7 && t.events[t.n - 2] == CLOSE &&
                  t.events[t.n - 1] == ACTOR_RELEASE);
        CHECK(!t.refs && mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    init(&t);
    t.name.text = "   ";
    CHECK(mm_movie_change_text(&t.actor, &t.name, 4, 0, &t.s) && t.removeActor == 1 && t.nuked == 1);
    CHECK(!t.writes && t.dirty == 1 && t.bars && t.characterCalls == 3);
    init(&t);
    t.name.text = "";
    t.fail = FAIL_ADD;
    CHECK(!mm_movie_change_text(&t.actor, &t.name, 4, 0, &t.s) && !t.dirty && !t.nuked && !t.characterCalls);
    init(&t);
    t.name.text = "\t";
    CHECK(seed_rollcall(&t));
    CHECK(mm_movie_change_text(&t.actor, &t.name, 4, &font, &t.s) && !t.removeActor && t.writes == 1);
    CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
    font = mm_movie_rollcall_tag(&row);
    close_tag(&font);
    CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    return 0;
}
static int captions(void)
{
    Test t;
    int fault, hide;
    for (fault = 0; fault < 3; ++fault)
    {
        init(&t);
        t.fail = fault == 1 ? FAIL_CAPTION : fault == 2 ? FAIL_CAPTION_ADD : 0;
        CHECK(mm_movie_insert_caption(&t, 1, &t.s) == !fault);
        CHECK(t.captions[0].refs == !fault && t.attached == !fault && !t.dirty);
        CHECK(t.captionCount == (fault != 1));
        if (!fault)
        {
            const int events[] = {NEW_CAPTION,    DEFAULT_FONT, FONT,           ADD_CAPTION,
                                  SELECT_CAPTION, ATTACH,       CAPTION_RELEASE};
            CHECK(t.captions[0].font == 23 && t.selected == &t.captions[0] && t.n == 7 &&
                  !memcmp(t.events, events, sizeof(events)));
        }
        else
            CHECK(!t.selected);
    }
    for (hide = 0; hide < 2; ++hide)
        for (fault = 0; fault < 3; ++fault)
        {
            init(&t);
            if (fault != 1)
                t.selected = &t.captions[2];
            t.fail = fault == 2 ? FAIL_CAPTION_EDIT : 0;
            CHECK(mm_movie_remove_caption(hide, &t.s) == !fault);
            CHECK(t.missing == (fault == 1) && t.hidden == (hide && fault != 1) && t.removed == (!hide && fault != 1));
        }
    init(&t);
    mm_movie_select_caption(-1, &t.s);
    CHECK(!t.queries && !t.selected);
    mm_movie_select_caption(1, &t.s);
    CHECK(t.queries == 3 && t.query[0] == 0 && t.query[1] == 1 && t.query[2] == 2 && t.selected == &t.captions[2]);
    t.queries = 0;
    mm_movie_select_caption(2, &t.s);
    CHECK(t.queries == 4 && t.query[3] == 3 && t.selected == &t.captions[2]);
    t.queries = 0;
    mm_movie_select_caption(0, &t.s);
    CHECK(t.queries == 1 && t.selected == &t.captions[0]);
    return 0;
}
int main(void)
{
    if (material() || inserting() || changing() || captions())
        return 1;
    puts("Movie material/text/caption transactions and failure cleanup passed");
    return 0;
}
