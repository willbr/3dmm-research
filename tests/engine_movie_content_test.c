/* Movie content transaction ownership, failure prefixes and caption selection. */
#include "mm/movie_content.h"
#include "mm/material_resource.h"
#include "mm/string_wire.h"
#include "template_writer_fixture.h"
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
    MMResourceCache cache;
    Test *test;
} Resource;
typedef struct Actor
{
    Test *test;
    int refs;
    MMActorOwner owner;
} Actor;
typedef struct Caption
{
    Test *test;
    int refs, visible, font;
} Caption;
struct Test
{
    MMMovieContentServices s;
    MMTagServices tags;
    MMMovieState data;
    MMMovieScenes scenes;
    Resource before, after;
    MMStringValue name;
    unsigned char name_buffer[MM_STRING_MAX + 2];
    TemplateWriterFixture archive;
    MMTemplateOwnerHost text_host;
    MMStorageAllocator text_allocator;
    Actor actor, copy, inserted;
    Caption captions[3], *selected;
    int fail, events[128], n, refs, opened, closed, textRefs, writes;
    int dirty, invalidates, bars, releases, restored, removeActor, nuked, cacheBegin, cacheEnd;
    int captionCount, attached, missing, hidden, removed, queries, query[8];
    void *writtenResources, *openedResources;
};
static void note(Test *t, int e)
{
    t->events[t->n++] = e;
}
static int ensure(void *p, MMResourceCache **out)
{
    Test *t = p;
    note(t, ENSURE);
    *out = &t->before.cache;
    return t->fail != FAIL_ENSURE;
}
static int valid(void *p)
{
    Test *t = p;
    note(t, VALID);
    return t->fail != FAIL_VALID;
}
static int temp(void *p)
{
    Test *t = p;
    note(t, TEMP);
    t->data.resources = &t->after;
    return t->fail != FAIL_TEMP;
}
static int material_extent(void *p, MMU32 bytes, MMChunkLocation *location)
{
    Test *t = ((ChunkIndexFixture *)p)->observer;
    assert(bytes == 20);
    location->file = &t->archive.archive;
    location->offset = 128;
    location->bytes = bytes;
    location->flags = 0;
    return 1;
}
static int material_write(void *p, void *file, MMU32 offset, const void *data, size_t size)
{
    const ChunkWriterFixture *fixture = p;
    Test *t = fixture->context;
    assert(file == p && offset == 128 && size == 20);
    if (t->fail == FAIL_WRITE)
        return 0;
    memcpy(t->archive.descriptor, data, size);
    return 1;
}
static MMArchiveWriter material_writer(void *p)
{
    Resource *resource = p;
    Test *t = resource->test;
    note(t, WRITE);
    ++t->writes;
    t->writtenResources = &resource->cache;
    return chunk_writer_fixture(&t->archive.archive, t, material_extent, material_write);
}
static void describe_material(void *p, void *handle, MMMaterialDescription *description)
{
    (void)p;
    (void)handle;
    memset(description, 0, sizeof(*description));
    description->colour = 0x123456;
    description->index_base = 32;
    description->index_range = 7;
}
static void invalidate_number(void *p, void *number, size_t bytes)
{
    (void)p;
    memset(number, 0xff, bytes);
}
static MMArchiveWriter text_writer(void *p)
{
    Resource *resource = p;
    Test *t = resource->test;
    MMArchiveWriter writer = writer_fixture(&t->archive, 0, MM_TEXT_WINDOWS);
    note(t, WRITE);
    ++t->writes;
    t->writtenResources = &resource->cache;
    fixture_chunk_add(&t->archive.archive.index, 0x4d564945UL, 0);
    writer.names.max_bytes = sizeof(t->archive.archive.name);
    if (t->fail == FAIL_WRITE)
        t->archive.fail = 6;
    return writer;
}
static void *allocate_text(void *p, void *value, MMU32 old_size, MMU32 bytes, int priority)
{
    Test *t = p;
    (void)value;
    (void)old_size;
    (void)priority;
    note(t, NEW_TEXT);
    if (t->fail == FAIL_TEXT)
        return NULL;
    ++t->textRefs;
    return malloc(bytes);
}
static void release_text(void *p, void *value, MMU32 bytes)
{
    Test *t = p;
    (void)bytes;
    note(t, TEXT_RELEASE);
    --t->textRefs;
    free(value);
}
static MMTextGenerated *text_state(void *context, void *owner)
{
    (void)context;
    return mm_template_owner_generated(owner);
}
static MMI32 text_length(void *context, void *owner)
{
    const MMStringValue name = mm_template_owner_name(owner);
    (void)context;
    return *(const unsigned char *)name.buffer;
}
static MMTemplateState *template_state(void *context, void *owner)
{
    (void)context;
    return mm_template_owner_state(owner);
}
static void *text_material(void *context)
{
    static const MMMaterialOwnerHost host = {0};
    (void)context;
    return mm_material_owner_create(&host, MM_MATERIAL);
}
static void retain_tag_cache(void *p)
{
    Resource *r = p;
    Test *t = r->test;
    note(t, OPEN);
    t->openedResources = p;
    ++t->refs;
    ++t->opened;
}
static void release_tag_cache(void *p)
{
    const Resource *r = p;
    Test *t = r->test;
    note(t, CLOSE);
    --t->refs;
    ++t->closed;
}
static MMActorOwner *actor_owner(void *p, void *actor)
{
    (void)p;
    return &((Actor *)actor)->owner;
}
static void *new_snapshot(void *p)
{
    Test *t = p;
    note(t, DUP);
    if (t->fail == FAIL_DUP)
        return 0;
    t->copy.test = t;
    mm_actor_owner_init(&t->copy.owner);
    t->copy.refs = 1;
    return &t->copy;
}
static void retain_actor_resource(void *p, void *resource)
{
    (void)p;
    (void)resource;
}
static void release_actor(void *p, void *actor)
{
    Actor *a = actor;
    (void)p;
    note(a->test, ACTOR_RELEASE);
    if (!--a->refs)
    {
        mm_actor_track_dispose(&a->owner.track, NULL);
        mm_actor_sounds_dispose(&a->owner.sounds);
    }
    ++a->test->releases;
}
static void restored_actor_views(void *p, void *scene)
{
    Test *t = p;
    (void)scene;
    note(t, RESTORE);
    ++t->restored;
}
static void restore_template_edit(void *p, void *snapshot)
{
    Actor *a = p;
    mm_actor_owner_restore(a, snapshot, &a->test->s.edit.actors);
}
static int change(void *p, const MMTag *tag)
{
    Actor *a = p;
    note(a->test, TEMPLATE);
    a->owner.runtime.state.cel = 99;
    if (a->test->fail == FAIL_TEMPLATE)
        return 0;
    a->owner.runtime.template_tag = *tag;
    return 1;
}
static MMI32 actor_id(void *p)
{
    return ((Actor *)p)->owner.runtime.actor;
}
static void set_id(void *p, MMI32 id)
{
    Actor *a = p;
    note(a->test, SET_ID);
    a->owner.runtime.actor = id;
}
static void actor_tag(void *p, MMTag *tag)
{
    *tag = ((Actor *)p)->owner.runtime.template_tag;
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
    return p && ((Test *)p)->fail == FAIL_APPEND ? NULL : realloc(data, size);
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
    t->inserted.owner.runtime.template_tag = *tag;
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
    t->before.cache.file = &t->before;
    t->before.cache.services.live_writer = text_writer;
    t->name.buffer = t->name_buffer;
    t->name.unit = 1;
    mm_string_value_set(&t->name, " A", 2);
    t->actor.test = t;
    mm_actor_owner_init(&t->actor.owner);
    t->actor.owner.runtime.actor = 7;
    t->actor.owner.runtime.state.cel = 11;
    t->actor.refs = 1;
    t->inserted.test = t;
    mm_actor_owner_init(&t->inserted.owner);
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
    t->tags.retain = retain_tag_cache;
    t->tags.release = release_tag_cache;
    e->rollcall.tags = &t->tags;
    e->rollcall.changed = roll_changed;
    e->actors.context = t;
    e->actors.owner = actor_owner;
    e->actors.create = new_snapshot;
    e->actors.retain = retain_actor_resource;
    e->actors.release = release_actor;
    e->actors.release_template = release_actor;
    e->actors.retain_template = retain_actor_resource;
    e->actors.invalidate_views = restored_actor_views;
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
    t->text_allocator.context = t;
    t->text_allocator.resize = allocate_text;
    t->text_allocator.release = release_text;
    t->text_host.allocator = &t->text_allocator;
    t->text_host.name_unit = 1;
    s->text_host = &t->text_host;
    s->text_generation.state = text_state;
    s->text_generation.length = text_length;
    s->text_generation.template_state = template_state;
    s->text_generation.table_allocator.resize = resize;
    s->text_generation.table_allocator.release = release;
    s->text_generation.material_new = text_material;
    s->tags = &t->tags;
    s->change_template = change;
    s->restore_template_edit = restore_template_edit;
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
    MMMaterialServices services = {0};
    MMMaterialOwnerHost host = {0};
    MMMaterialOwner *owner;
    MMMaterialDescription stored;
    int written, fault;
    services.system = services.windows_system = MM_TEXT_WINDOWS;
    services.describe = describe_material;
    services.invalidate = invalidate_number;
    host.services = &services;
    owner = mm_material_owner_create(&host, MM_MATERIAL);
    CHECK(owner);
    for (fault = 0; fault < 3; ++fault)
    {
        init(&t);
        t.before.cache.services.live_writer = material_writer;
        t.fail = fault == 1 ? FAIL_ENSURE : fault == 2 ? FAIL_WRITE : 0;
        memset(&output, 0, sizeof(output));
        output.number = 999;
        CHECK(mm_movie_insert_material(owner, &output, &written, &t.s) == !fault);
        CHECK(written == !fault);
        CHECK(t.writes == (fault != 1) && t.archive.archive.index.count == (unsigned)!fault && !t.dirty && !t.invalidates);
        if (written)
        {
            CHECK(output.sid == 0 && output.type == MM_MATERIAL_CHUNK && output.number == 0);
            CHECK(mm_material_header_read(t.archive.descriptor, 0, MM_TEXT_WINDOWS, &stored));
            CHECK(stored.colour == 0x123456 && stored.index_base == 32 && stored.index_range == 7);
        }
        else
            CHECK(output.number == 999);
        if (!fault)
        {
            CHECK(t.refs == 1 && output.cache == &t.before);
            mm_tag_close(&output, &t.tags);
        }
        CHECK(!t.refs);
    }
    mm_material_owner_release(owner);
    return 0;
}
static int inserting(void)
{
    Test t;
    MMTag font;
    int fault;
    const int faults[] = {0, FAIL_TEXT, FAIL_VALID, FAIL_TEMP, FAIL_WRITE, FAIL_CACHE, FAIL_ACTOR, FAIL_ADD};
    memset(&font, 0, sizeof(font));
    font.sid = 123;
    for (fault = 0; fault < (int)(sizeof(faults) / sizeof(*faults)); ++fault)
    {
        init(&t);
        t.fail = faults[fault];
        CHECK(mm_movie_insert_text(&t.name, 4, &font, &t.s) == !fault);
        CHECK(!t.textRefs && !t.refs && t.opened == t.closed && t.dirty == !fault && t.invalidates == !fault);
        CHECK(t.inserted.refs == !fault && t.cacheBegin == t.cacheEnd);
        if (t.fail != FAIL_TEXT && t.fail != FAIL_VALID && t.fail != FAIL_TEMP)
            CHECK(t.writtenResources == &t.before);
        if (!fault || t.fail == FAIL_CACHE || t.fail == FAIL_ACTOR || t.fail == FAIL_ADD)
            CHECK(t.openedResources == &t.after);
        if (!fault)
        {
            const int prefix[] = {NEW_TEXT, VALID, TEMP, WRITE, TEXT_RELEASE, OPEN, ACTOR_RELEASE, DIRTY, CLOSE};
            MMTextDescriptor saved;
            CHECK(t.n == sizeof(prefix) / sizeof(prefix[0]) && !memcmp(t.events, prefix, sizeof(prefix)) && t.bars);
            CHECK(t.archive.archive.index.count == 3 && !t.archive.archive.file_refs &&
                  !memcmp(t.archive.archive.name, "\003\003\002 A", 6));
            CHECK(mm_text_decode_descriptor(t.archive.descriptor, 0, &saved) && saved.shape == 4 && saved.font.sid == 123);
            release_actor(&t, &t.inserted);
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
    const int faults[] = {0, FAIL_TEXT, FAIL_VALID, FAIL_TEMP, FAIL_WRITE, FAIL_DUP, FAIL_TEMPLATE, FAIL_APPEND};
    memset(&font, 0, sizeof(font));
    memset(long_name, 'X', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    font.sid = 123;
    for (fault = 0; fault < (int)(sizeof(faults) / sizeof(*faults)); ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        CHECK(mm_actor_track_create(&t.actor.owner.track, NULL, 0, NULL));
        t.fail = faults[fault];
        if (t.fail == FAIL_APPEND)
            mm_string_value_set(&t.name, long_name, sizeof(long_name) - 1);
        CHECK(mm_movie_change_text(&t.actor, &t.name, 4, &font, &t.s) == !fault);
        CHECK(!t.copy.refs && !t.textRefs && t.refs == !fault && t.dirty == !fault && !t.invalidates);
        CHECK(t.restored == (t.fail == FAIL_APPEND) && t.data.actors.count == (MMU32)(1 + !fault) &&
              t.data.actorIdLimit == (!fault || t.fail == FAIL_APPEND ? 11 : 10));
        if (!fault)
        {
            const int suffix[] = {SET_ID, OPEN, ROLL_CHANGED, ACTOR_RELEASE, CLOSE, DIRTY};
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row));
            CHECK(t.actor.owner.runtime.actor == 10 && t.actor.owner.runtime.state.cel == 99 &&
                  row.data.reference_count == 1);
            CHECK(t.n >= 6 && !memcmp(t.events + t.n - 6, suffix, sizeof(suffix)));
            CHECK(mm_rollcall_table_name(&t.data.actors, 1, &name, &length) && length == 2 && !memcmp(name, " A", length));
            CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
            font = mm_movie_rollcall_tag(&row);
            mm_tag_close(&font, &t.tags);
            font.sid = 123;
        }
        if (t.fail == FAIL_APPEND)
        {
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row));
            CHECK(t.actor.owner.runtime.actor == 7 && t.actor.owner.runtime.state.cel == 11 &&
                  row.data.reference_count == 2);
        }
        if (t.fail == FAIL_TEMPLATE)
            CHECK(t.actor.owner.runtime.state.cel == 99 && t.actor.owner.runtime.actor == 7 &&
                  t.events[t.n - 2] == CLOSE &&
                  t.events[t.n - 1] == ACTOR_RELEASE);
        mm_actor_track_dispose(&t.actor.owner.track, NULL);
        CHECK(!t.refs && mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    init(&t);
    mm_string_value_set(&t.name, "   ", 3);
    CHECK(mm_movie_change_text(&t.actor, &t.name, 4, 0, &t.s) && t.removeActor == 1 && t.nuked == 1);
    CHECK(!t.writes && t.dirty == 1 && t.bars);
    init(&t);
    mm_string_value_clear(&t.name);
    t.fail = FAIL_ADD;
    CHECK(!mm_movie_change_text(&t.actor, &t.name, 4, 0, &t.s) && !t.dirty && !t.nuked && !t.writes);
    init(&t);
    mm_string_value_set(&t.name, "\t", 1);
    CHECK(seed_rollcall(&t));
    CHECK(mm_movie_change_text(&t.actor, &t.name, 4, &font, &t.s) && !t.removeActor && t.writes == 1);
    CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
    font = mm_movie_rollcall_tag(&row);
    mm_tag_close(&font, &t.tags);
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
