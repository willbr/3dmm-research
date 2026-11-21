#include "mm/actor_clipboard.h"
#include "mm/actor_effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "clipboard check at %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define ONE 65536L

typedef struct Text
{
    unsigned short chars[MM_STRING_MAX];
    MMI32 count;
} Text;

typedef struct Actor
{
    MMActorOwner owner;
    int refs;
} Actor;

typedef struct Fixture
{
    MMActorClipboard clip;
    MMActorClipboardServices services;
    MMActorCopyServices copy;
    Actor source, copied, duplicate;
    Text name, prefix;
    int movie, template_refs, tag_refs;
    int fail, created_actors, route_result, resets;
    char order[32];
    size_t order_count;
} Fixture;

static void mark(Fixture *f, char action)
{
    CHECK(f->order_count + 1 < sizeof(f->order));
    f->order[f->order_count++] = action;
}

static MMActorOwner *owner(void *context, void *actor)
{
    (void)context;
    return &((Actor *)actor)->owner;
}

static void *create_actor(void *context)
{
    Fixture *f = (Fixture *)context;
    Actor *actor;
    if (f->clip.actor)
    {
        CHECK(f->clip.actor == &f->copied);
        mark(f, 'd');
        if (f->fail == 3)
            return NULL;
        actor = &f->duplicate;
    }
    else
    {
        mark(f, 'c');
        if (f->fail == 2)
            return NULL;
        actor = &f->copied;
    }
    ++f->created_actors;
    CHECK(!actor->refs);
    actor->refs = 1;
    mm_actor_owner_init(&actor->owner);
    return actor;
}

static void *fetch_template(void *context, const MMTag *tag)
{
    Fixture *f = (Fixture *)context;
    CHECK(tag->sid == 7);
    ++f->template_refs;
    return &f->template_refs;
}

static void retain(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    CHECK(object == &f->template_refs);
    ++f->template_refs;
}

static void release(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    Actor *actor;
    if (object == &f->template_refs)
    {
        CHECK(--f->template_refs >= 0);
        return;
    }
    actor = (Actor *)object;
    CHECK(actor->refs == 1);
    if (actor == &f->copied)
        CHECK(f->clip.actor == actor);
    else
        CHECK(actor == &f->duplicate && f->clip.actor == &f->copied);
    actor->refs = 0;
    mm_actor_owner_dispose(actor, &f->copy.create.owner);
    mark(f, 'R');
}

static void retain_tag(void *cache) { ++((Fixture *)cache)->tag_refs; }
static void release_tag(void *cache) { CHECK(--((Fixture *)cache)->tag_refs >= 0); }

static const MMTagServices tags = {
    NULL, NULL, retain_tag, release_tag, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static int action_flags(void *context, void *object, MMI32 action, MMU32 *flags)
{
    Fixture *f = (Fixture *)context;
    CHECK(object == &f->template_refs && action == 0);
    *flags = 8;
    return 1;
}

static void reset_lifetime(void *context)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->duplicate.owner.runtime.state.frame == 2147483647L);
    ++f->resets;
}

static void default_costume(void *context) { (void)context; }
static void recording(void *context, int enabled) { (void)context; CHECK(!enabled); }

static void route_services(void *context, void *actor, MMActorRouteEditServices *s)
{
    Fixture *f = (Fixture *)context;
    CHECK(actor == &f->copied || actor == &f->duplicate);
    memset(s, 0, sizeof(*s));
    s->edits.context = f;
    s->life_dirty = reset_lifetime;
    s->default_costume = default_costume;
    s->recording = recording;
}

static MMI32 distance(void *context, const MMActorPoint *point)
{
    (void)context;
    CHECK(point->dxr >= 0 && !point->dyr && !point->dzr);
    return point->dxr;
}

static void *create_clipboard(void *context, MMActorClipboard **storage)
{
    Fixture *f = (Fixture *)context;
    mark(f, 'C');
    if (f->fail == 1)
        return NULL;
    *storage = &f->clip;
    return f;
}

static void release_clipboard(void *context, void *clipboard)
{
    Fixture *f = (Fixture *)context;
    CHECK(clipboard == f && !f->clip.actor);
    mark(f, 'D');
    mm_actor_clipboard_dispose(&f->clip, &f->copy.create.owner);
}

static void supply_text(const MMStringValue *out, const Text *input)
{
    unsigned char bytes[MM_STRING_MAX];
    MMI32 i;
    if (out->unit == 2)
        mm_string_value_set(out, input->chars, input->count);
    else
    {
        for (i = 0; i < input->count; ++i)
            bytes[i] = (unsigned char)input->chars[i];
        mm_string_value_set(out, bytes, input->count);
    }
}

static void source_name(void *context, void *source, const MMStringValue *out)
{
    Fixture *f = (Fixture *)context;
    CHECK(source == &f->source && f->copied.refs == 1 && !f->clip.actor);
    mark(f, 'n');
    supply_text(out, &f->name);
}

static void copy_prefix(void *context, void *source, const MMStringValue *out)
{
    Fixture *f = (Fixture *)context;
    CHECK(source == &f->source && f->copied.refs == 1 && !f->clip.actor);
    mark(f, 'p');
    supply_text(out, &f->prefix);
}

static void too_long(void *context) { mark((Fixture *)context, '!'); }

static int paste_route(void *context, void *movie, void *actor)
{
    Fixture *f = (Fixture *)context;
    MMActorRoutePoint point;
    CHECK(movie == &f->movie && actor == &f->copied && f->copied.refs == 1);
    CHECK(mm_actor_path_count(f->copied.owner.track.route) == 2 && !mm_actor_events_count(f->copied.owner.track.events));
    CHECK(mm_actor_path_get(f->copied.owner.track.route, 0, &point) && point.xyz.dxr == 3 * ONE);
    mark(f, 'r');
    return f->route_result;
}

static int paste_actor(void *context, void *movie, void *actor)
{
    Fixture *f = (Fixture *)context;
    MMActorOwner *duplicate = &f->duplicate.owner;
    CHECK(movie == &f->movie && actor == &f->duplicate && f->duplicate.refs == 1);
    CHECK(duplicate->track.events != f->copied.owner.track.events && duplicate->track.route != f->copied.owner.track.route);
    CHECK(mm_actor_events_count(duplicate->track.events) == mm_actor_events_count(f->copied.owner.track.events));
    CHECK(f->tag_refs == 3 && f->resets == 1 && f->template_refs == 3);
    CHECK(duplicate->runtime.state.frame == 2147483647L && !duplicate->runtime.scene);
    duplicate->runtime.actor = 91;
    mark(f, 'P');
    return f->fail != 4;
}

static int rename_actor(void *context, void *movie, MMI32 id, const void *name, MMI32 count)
{
    Fixture *f = (Fixture *)context;
    MMI32 stored_count;
    CHECK(movie == &f->movie && id == 91 && name == mm_actor_clipboard_name(&f->clip, &stored_count));
    CHECK(count == stored_count && count == 12);
    mark(f, 'N');
    f->duplicate.owner.runtime.actor = 92;
    return f->fail == 5 ? 0 : 2;
}

static void remove_actor(void *context, void *movie, MMI32 id)
{
    Fixture *f = (Fixture *)context;
    CHECK(movie == &f->movie && id == 92 && f->duplicate.refs == 1);
    mark(f, 'X');
}

static void set_text(Text *text, const char *value)
{
    MMI32 i;
    text->count = (MMI32)strlen(value);
    for (i = 0; i < text->count; ++i)
        text->chars[i] = (unsigned char)value[i];
}

static void seed_source(Fixture *f)
{
    MMActorOwner *source = &f->source.owner;
    const MMActorEntrance entrance = {0};
    MMActorCostume costume = {0};
    MMActorRoutePoint point = {0};
    MMActorEvent event = {0};
    MMI32 i;

    mm_actor_owner_init(source);
    CHECK(mm_actor_track_create(&source->track, NULL, 0, &tags));
    source->runtime.template_object = &f->template_refs;
    source->runtime.template_tag.sid = 7;
    source->runtime.state.location.dwrOffset = 3 * ONE;
    source->runtime.state.addEvent = 0;
    source->runtime.state.nextEvent = source->runtime.state.firstEventInFrame = 2;
    for (i = 0; i < 4; ++i)
    {
        point.xyz.dxr = i * 4 * ONE;
        point.dwr = (i & 1) ? 0 : 4 * ONE;
        CHECK(mm_actor_path_insert(source->track.route, i, &point));
    }
    event.aet = MM_ACTOR_ADD;
    event.nfrm = 1;
    CHECK(mm_actor_events_insert(source->track.events, 0, sizeof(entrance), &entrance, &event));
    costume.tag.cache = f;
    event.aet = MM_ACTOR_COSTUME;
    CHECK(mm_actor_events_insert(source->track.events, 1, sizeof(costume), &costume, &event));
    event.aet = MM_ACTOR_ADD;
    event.nfrm = 8;
    event.rtel.irpt = 2;
    CHECK(mm_actor_events_insert(source->track.events, 2, sizeof(entrance), &entrance, &event));
    f->template_refs = f->tag_refs = 1;
}

static void init(Fixture *f, unsigned unit)
{
    MMCollectionStorage storage;
    MMActorOwnerServices *o;
    MMActorClipboardServices *s;

    memset(f, 0, sizeof(*f));
    mm_actor_clipboard_init(&f->clip, unit);
    o = &f->copy.create.owner;
    o->context = f;
    o->tags = &tags;
    o->owner = owner;
    o->create = create_actor;
    o->retain = retain;
    o->release = release;
    o->retain_body = retain;
    o->release_body = release;
    o->route_services = route_services;
    CHECK(mm_storage_init(&storage, NULL) == MM_STORAGE_OK);
    f->copy.create.track_allocator = storage.allocator;
    f->copy.create.fetch_template = fetch_template;
    f->copy.create.action_flags = action_flags;
    f->copy.distance = distance;

    s = &f->services;
    s->context = f;
    s->create = create_clipboard;
    s->release_clipboard = release_clipboard;
    s->source_name = source_name;
    s->copy_prefix = copy_prefix;
    s->name_too_long = too_long;
    s->paste_route = paste_route;
    s->paste_actor = paste_actor;
    s->rename = rename_actor;
    s->remove = remove_actor;
    set_text(&f->name, "Alex");
    set_text(&f->prefix, "Copy of");
    seed_source(f);
}

static void dispose(Fixture *f)
{
    const MMActorOwnerServices *owner = &f->copy.create.owner;
    mm_actor_clipboard_dispose(&f->clip, owner);
    CHECK(!f->clip.actor && !f->duplicate.refs && !f->copied.refs);
    mm_actor_owner_dispose(&f->source, owner);
    CHECK(!f->template_refs && !f->tag_refs);
}

static unsigned character(const Fixture *f, MMI32 index)
{
    MMI32 count;
    const void *text = mm_actor_clipboard_name(&f->clip, &count);
    CHECK(index <= count);
    return f->clip.unit == 1 ? ((const unsigned char *)text)[index] : ((const unsigned short *)text)[index];
}

static void name_is(const Fixture *f, const char *expected)
{
    MMI32 i, count;
    mm_actor_clipboard_name(&f->clip, &count);
    CHECK(count == (MMI32)strlen(expected));
    for (i = 0; i < count; ++i)
        CHECK(character(f, i) == (unsigned char)expected[i]);
    CHECK(!character(f, count));
}

static void *make(Fixture *f, int route, int entire)
{
    return mm_actor_clipboard_create(&f->source, route, entire, 0, &f->copy, &f->services);
}

static int paste(Fixture *f)
{
    return mm_actor_clipboard_paste(&f->clip, &f->movie, &f->copy.create.owner, &f->services);
}

static void test_names(unsigned unit)
{
    Fixture f;
    MMI32 count;
    init(&f, unit);
    CHECK(make(&f, 0, 1) == &f && !strcmp(f.order, "Ccnp"));
    CHECK(f.clip.actor == &f.copied && f.tag_refs == 2);
    CHECK(mm_actor_path_count(f.copied.owner.track.route) == 4 && mm_actor_events_count(f.copied.owner.track.events) == 3);
    name_is(&f, "Copy of Alex");
    dispose(&f);

    init(&f, unit);
    set_text(&f.name, "Copy ofX");
    CHECK(make(&f, 0, 0) == &f);
    name_is(&f, "Copy ofX");
    dispose(&f);

    init(&f, unit);
    set_text(&f.name, "copy of Alex");
    CHECK(make(&f, 0, 0) == &f);
    name_is(&f, "Copy of copy of Alex");
    dispose(&f);

    init(&f, unit);
    set_text(&f.prefix, "");
    CHECK(make(&f, 0, 0) == &f);
    name_is(&f, "Alex");
    dispose(&f);

    init(&f, unit);
    set_text(&f.prefix, "C.X");
    f.prefix.chars[1] = 0;
    set_text(&f.name, "Cat");
    CHECK(make(&f, 0, 0) == &f);
    name_is(&f, "Cat");
    dispose(&f);

    init(&f, unit);
    set_text(&f.prefix, "C.X");
    f.prefix.chars[1] = 0;
    f.name.chars[1] = 0;
    CHECK(make(&f, 0, 0) == &f);
    mm_actor_clipboard_name(&f.clip, &count);
    CHECK(count == 8 && character(&f, 0) == 'C' && !character(&f, 1) && character(&f, 2) == 'X');
    CHECK(character(&f, 3) == ' ' && character(&f, 4) == 'A' && !character(&f, 5) && !character(&f, 8));
    dispose(&f);

    if (unit == 2)
    {
        init(&f, unit);
        set_text(&f.prefix, "P");
        f.prefix.chars[0] = 0x0100;
        CHECK(make(&f, 0, 0) == &f);
        CHECK(character(&f, 0) == 0x0100 && character(&f, 1) == ' ' && character(&f, 2) == 'A');
        dispose(&f);
    }
}

static void test_creation_failures(unsigned unit)
{
    Fixture f;
    int fail;
    MMI32 i, j, count;
    for (fail = 1; fail <= 2; ++fail)
    {
        init(&f, unit);
        f.fail = fail;
        CHECK(!make(&f, 0, 0) && !f.clip.actor && !f.created_actors);
        CHECK(!strcmp(f.order, fail == 1 ? "C" : "CcD"));
        dispose(&f);
    }
    for (i = 250; i <= 255; i += 5)
    {
        init(&f, unit);
        f.prefix.count = i;
        for (j = 0; j < i; ++j)
            f.prefix.chars[j] = 'P';
        set_text(&f.name, "Alexis");
        CHECK(!make(&f, 0, 0) && !f.clip.actor && !strcmp(f.order, "Ccnp!D"));
        CHECK(f.copied.refs == 1 && f.template_refs == 2 && f.tag_refs == 2);
        mm_actor_clipboard_name(&f.clip, &count);
        CHECK(count == 255 && !character(&f, count));
        CHECK(character(&f, 254) == (i == 250 ? 'x' : 'P'));
        /* Release the reference intentionally left by the legacy overflow path. */
        mm_actor_owner_dispose(&f.copied, &f.copy.create.owner);
        f.copied.refs = 0;
        dispose(&f);
    }
}

static void test_paste(void)
{
    Fixture f;
    int result, fail;
    for (result = 0; result <= 1; ++result)
    {
        init(&f, 2);
        CHECK(make(&f, 1, 0) == &f && f.clip.route_only && !strcmp(f.order, "Cc"));
        name_is(&f, "");
        f.route_result = result;
        CHECK(paste(&f) == result && !strcmp(f.order, "Ccr"));
        CHECK(f.copied.refs == 1 && !f.duplicate.refs && f.tag_refs == 1);
        dispose(&f);
    }
    for (fail = 3; fail <= 6; ++fail)
    {
        const char *orders[] = {"Ccnpd", "CcnpdPR", "CcnpdPNXR", "CcnpdPNR"};
        init(&f, 2);
        CHECK(make(&f, 0, 0) == &f && !f.clip.route_only);
        f.fail = fail;
        CHECK(paste(&f) == (fail == 6) && !strcmp(f.order, orders[fail - 3]));
        CHECK(f.copied.refs == 1 && !f.duplicate.refs && f.tag_refs == 2);
        name_is(&f, "Copy of Alex");
        dispose(&f);
    }
}

int main(void)
{
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
    {
        test_names(unit);
        test_creation_failures(unit);
    }
    test_paste();
    puts("C89 actor clipboard checks passed");
    return 0;
}
