#include "mm/actor_track.h"
#include "mm/actor_animation.h"
#include "tag_copy_fixture.h"
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

typedef struct Host
{
    int attempts, fail_at, live, refs, copies, fail_copy;
    char log[64];
    size_t logged;
    MMU32 published[8];
    int publish_count;
} Host;
static Host h;
static TagCopyFile source_file, destination_file;

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
    h.attempts = h.fail_at = 0;
}

static void *resize(void *context, void *data, MMU32 old_bytes, MMU32 bytes, int priority)
{
    void *out;
    CHECK(context == &h && (!!data == !!old_bytes));
    (void)priority;
    ++h.attempts;
    if (h.fail_at && h.attempts >= h.fail_at)
        return NULL;
    out = realloc(data, bytes);
    CHECK(out != NULL);
    if (!old_bytes)
        ++h.live;
    return out;
}

static void free_bytes(void *context, void *data, MMU32 bytes)
{
    CHECK(context == &h && data && bytes && h.live > 0);
    --h.live;
    log_call('x');
    free(data);
}

static void retain(void *cache)
{
    CHECK(cache == &h);
    ++h.refs;
    log_call('+');
}

static void release(void *cache)
{
    CHECK(cache == &h && h.refs > 0);
    --h.refs;
    log_call('-');
}

static MMChunkEndpoint bind_target(void *cache, MMChunkFileCopy *binding)
{
    const MMChunkEndpoint unavailable = {0};
    CHECK(cache == &h);
    log_call('c');
    if (++h.copies == h.fail_copy)
        return unavailable;
    return tag_copy_file_endpoint(&destination_file, binding);
}

static void publish(void *context, const MMTag *tag)
{
    CHECK(context == &h && h.publish_count < 8);
    h.published[h.publish_count++] = tag->number;
}

static const MMStorageAllocator allocator = {&h, resize, free_bytes};
static const MMTagServices tags = {
    NULL, retain, release, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void row(MMActorEventTable *events, MMI32 type, MMI32 custom, MMI32 sid, void *cache, MMU32 number)
{
    MMActorCostume costume = {0};
    MMActorSound sound = {0};
    MMActorEvent event = {0};
    const MMU32 sizes[] = {20, 8, 0, 48, 12, 4, 0, 12, 4, 12, 4, 0, 48};
    const unsigned char payload[48] = {0};
    const void *data = payload;
    MMU32 bytes = sizes[type];
    event.aet = type;
    if (type == MM_ACTOR_COSTUME)
    {
        costume.custom = custom;
        costume.tag.sid = sid;
        costume.tag.cache = cache;
        costume.tag.type = 123;
        costume.tag.number = number;
        data = &costume;
        bytes = sizeof(costume);
    }
    else if (type == MM_ACTOR_SOUND)
    {
        sound.tag.sid = sid;
        sound.tag.cache = cache;
        sound.tag.type = 123;
        sound.tag.number = number;
        data = &sound;
        bytes = sizeof(sound);
    }
    CHECK(mm_actor_events_insert(events, events->rows.count, bytes, data, &event));
}

static void populate(MMActorTrack *t)
{
    MMActorRoutePoint point = {{3, 8, 15}, 0};
    MMTag tag;
    int i;
    CHECK(mm_actor_path_insert(t->route, 0, &point));
    for (i = 0; i < 13; ++i)
    {
        row(t->events, i, 0, 0, &h, 456);
        if (i == MM_ACTOR_COSTUME || i == MM_ACTOR_SOUND)
        {
            CHECK(mm_actor_track_get_tag(t->events, i, &tag));
            CHECK(tag.cache == &h && tag.sid == 0 && tag.type == 123 && tag.number == 456);
            ++h.refs;
        }
        else
            CHECK(!mm_actor_track_get_tag(t->events, i, &tag));
    }
}

static void transactions(void)
{
    MMActorTrack source, target, old, probe;
    MMActorRoutePoint point;
    int n, create_attempts, clone_attempts, live;
    mm_actor_track_init(&source);
    mm_actor_track_init(&target);
    mm_actor_track_init(&probe);
    reset_log();
    CHECK(mm_actor_track_create(&source, &allocator, 1, &tags));
    create_attempts = h.attempts;
    CHECK(source.route->storage.buffers[0].capacity >= 32 * MM_ACTOR_ROUTE_BYTES);
    CHECK(source.route->storage.min_grow[0] == 32 * MM_ACTOR_ROUTE_BYTES);
    CHECK(source.events->storage.buffers[1].capacity >= 10 * sizeof(MMGroupLocation));
    populate(&source);
    CHECK(mm_actor_track_create(&target, &allocator, 1, &tags));
    populate(&target);
    CHECK(h.refs == 4);
    reset_log();
    CHECK(mm_actor_track_clone(&probe, &source, &tags));
    clone_attempts = h.attempts;
    mm_actor_track_dispose(&probe, &tags);
    live = h.live;
    old = target;
    for (n = 1; n <= create_attempts; ++n)
    {
        reset_log();
        h.fail_at = n;
        CHECK(!mm_actor_track_create(&target, &allocator, 1, &tags));
        CHECK(target.events == old.events && target.route == old.route && h.refs == 4 && h.live == live);
    }
    for (n = 1; n <= clone_attempts; ++n)
    {
        reset_log();
        h.fail_at = n;
        CHECK(!mm_actor_track_clone(&target, &source, &tags));
        CHECK(target.events == old.events && target.route == old.route && h.refs == 4 && h.live == live);
    }
    reset_log();
    CHECK(mm_actor_track_clone(&target, &source, &tags));
    CHECK(strcmp(h.log, "++--xxxxx") == 0);
    CHECK(target.events != source.events && target.route != source.route && h.refs == 4 && h.live == live);
    CHECK(mm_actor_path_get(target.route, 0, &point) && point.xyz.dxr == 3);
    point.xyz.dxr = 25;
    CHECK(mm_actor_path_put(target.route, 0, &point));
    CHECK(mm_actor_path_get(source.route, 0, &point) && point.xyz.dxr == 3);
    reset_log();
    CHECK(mm_actor_track_clone(&source, &source, &tags));
    mm_actor_track_swap(&source, &source);
    CHECK(!h.logged && !h.attempts);
    old = source;
    mm_actor_track_swap(&target, &source);
    CHECK(target.events == old.events && target.route == old.route);
    reset_log();
    mm_actor_track_close_tags(&target, &tags);
    CHECK(strcmp(h.log, "--") == 0 && h.refs == 2);
    reset_log();
    mm_actor_track_close_tags(&target, &tags);
    CHECK(!h.logged && h.refs == 2);
    mm_actor_track_dispose(&source, &tags);
    CHECK(strcmp(h.log, "--xxxxx") == 0);
    mm_actor_track_dispose(&source, &tags);
    CHECK(mm_actor_track_clone(&target, &source, &tags));
    CHECK(!target.events && !target.route && !h.live && !h.refs);
}

static void tag_acceptance(void)
{
    MMActorTrack t, clone;
    MMActorEvent short_event = {MM_ACTOR_SOUND, 0, {0, 0, 0}};
    MMI32 short_payload = 0;
    MMTag tag;
    mm_actor_track_init(&t);
    mm_actor_track_init(&clone);
    reset_log();
    CHECK(mm_actor_track_create(&t, &allocator, 1, &tags));
    row(t.events, MM_ACTOR_COSTUME, -1, 0, &h, 0);
    row(t.events, MM_ACTOR_COSTUME, 2, 0, &h, 0);
    row(t.events, MM_ACTOR_COSTUME, 0, 17, (void *)1, 10);
    row(t.events, MM_ACTOR_SOUND, 0, -1, (void *)1, 20);
    row(t.events, MM_ACTOR_SOUND, 0, 0, NULL, 30);
    CHECK(mm_actor_events_insert(t.events, 5, sizeof(short_payload), &short_payload, &short_event));
    CHECK(!mm_actor_track_get_tag(t.events, 0, &tag) && !mm_actor_track_get_tag(t.events, 1, &tag));
    CHECK(!mm_actor_track_get_tag(t.events, 5, &tag) && !mm_actor_track_get_tag(t.events, 6, &tag));
    reset_log();
    mm_actor_track_retain_tag(t.events, 4, &tags);
    mm_actor_track_close_tag(t.events, 4, &tags);
    CHECK(mm_actor_track_clone(&clone, &t, &tags) && !h.logged && !h.refs);
    mm_actor_track_dispose(&t, &tags);
    mm_actor_track_dispose(&clone, &tags);
    CHECK(!h.refs && !h.live);
}

static void open_rows(void)
{
    MMActorTrack t;
    MMTag tag;
    MMTagServices services = tags;
    MMChunkFileCopy source_binding;
    MMChunkEndpoint source;
    const MMU32 order[] = {0, 1, 30, 1, 0};
    services.context = &h;
    services.bind_target = bind_target;
    services.publish = publish;
    tag_copy_file_init(&source_file);
    tag_copy_file_init(&destination_file);
    fixture_chunk_add(&source_file.index, 123, 10);
    fixture_chunk_add(&source_file.index, 123, 20);
    fixture_chunk_add(&source_file.index, 123, 30);
    source = tag_copy_file_endpoint(&source_file, &source_binding);
    fixture_chunk_add(&destination_file.index, 123, 10);
    fixture_chunk_add(&destination_file.index, 123, 20);
    reset_log();
    mm_actor_track_init(&t);
    CHECK(mm_actor_track_create(&t, &allocator, 1, &tags));
    row(t.events, MM_ACTOR_COSTUME, 0, 0, NULL, 10);
    row(t.events, MM_ACTOR_COSTUME, 0, 7, NULL, 50);
    row(t.events, MM_ACTOR_SOUND, 0, 0, NULL, 20);
    row(t.events, MM_ACTOR_COSTUME, 0, 0, NULL, 30);
    h.copies = h.publish_count = 0;
    h.fail_copy = 3;
    CHECK(!mm_actor_track_open_tags(&t, &h, &source, &services));
    CHECK(strcmp(h.log, "c+c+c--") == 0 && !h.refs);
    CHECK(h.publish_count == 5 && !memcmp(h.published, order, sizeof(order)));
    CHECK(!t.events->storage.buffers[0].locks);
    CHECK(mm_actor_track_get_tag(t.events, 2, &tag) && !tag.cache && tag.number == 1);
    reset_log();
    CHECK(mm_actor_track_open_tags(&t, &h, NULL, &tags));
    CHECK(strcmp(h.log, "+++") == 0 && h.refs == 3);
    CHECK(mm_actor_track_get_tag(t.events, 1, &tag) && !tag.cache && tag.number == 50);
    mm_actor_track_retain_tag(t.events, 0, &tags);
    CHECK(h.refs == 4);
    release(&h);
    mm_actor_track_close_tag(t.events, 0, &tags);
    mm_actor_track_close_tag(t.events, 0, &tags);
    CHECK(h.refs == 2);
    mm_actor_track_dispose(&t, &tags);
    CHECK(!h.refs && !h.live);
}

static void adoption(void)
{
    MMActorTrack t, clone;
    MMActorEventTable *events;
    MMActorPath *route;
    mm_actor_track_init(&t);
    mm_actor_track_init(&clone);
    reset_log();
    CHECK(mm_actor_track_create(&t, &allocator, 1, &tags));
    populate(&t);
    reset_log();
    mm_actor_track_adopt_events(&t, t.events, &tags);
    mm_actor_track_adopt_route(&t, t.route);
    CHECK(!h.logged && h.refs == 2);
    events = mm_actor_events_create(NULL, 0, 0);
    route = mm_actor_path_create(&allocator, 1, 1);
    CHECK(events && route);
    mm_actor_track_adopt_events(&t, events, &tags);
    CHECK(!h.refs && t.events == events);
    mm_actor_track_adopt_route(&t, route);
    mm_actor_track_adopt_events(&t, NULL, &tags);
    CHECK(mm_actor_track_clone(&clone, &t, &tags));
    CHECK(!clone.events && clone.route && clone.route != route);
    mm_actor_track_dispose(&clone, &tags);
    mm_actor_track_adopt_route(&t, NULL);
    CHECK(!h.live);
    t.events = mm_actor_events_create(NULL, 0, 0);
    CHECK(t.events && mm_actor_track_clone(&clone, &t, &tags));
    CHECK(clone.events && !clone.route);
    mm_actor_track_dispose(&clone, &tags);
    mm_actor_track_dispose(&t, &tags);
    CHECK(!h.live && !h.refs);
}

static void reject_invalid_path(void)
{
    MMPreviewActor actor = {0};
    MMActorRecording recording = {0};
    MMActorRecordServices host = {0};
    MMActorAnimationPoint point = {3, 0, 0};
    MMActorAnimation edit = {MM_ACTOR_ANIMATION_TWEAK, 0, 0, 0, 0, NULL, 1};
    MMActorAnimationRange range = {80, 90};
    edit.points = &point;
    actor.tmpl = &actor;
    actor.state.onStage = 1;
    actor.state.position.dxr = 32698L * 65536L;
    CHECK(!mm_actor_animation_edit(&actor, &recording, &edit, 0, &host, &range));
    CHECK(actor.state.position.dxr == 32698L * 65536L && range.actor_last == 80 && range.edited_last == 90);
    actor.state.position.dxr = 0;
    edit.kind = MM_ACTOR_ANIMATION_RECORD;
    actor.state.frame = 2147483647L - 4095;
    CHECK(!mm_actor_animation_edit(&actor, &recording, &edit, 0, &host, &range));
    CHECK(!recording.recording && range.actor_last == 80 && range.edited_last == 90);
}

int main(void)
{
    transactions();
    tag_acceptance();
    adoption();
    open_rows();
    reject_invalid_path();
    puts("actor track passed");
    return 0;
}
