#include "scene_sound_fixture.h"
#include "mm/scene_sound_edit.h"
#include "chunk_seed_fixture.h"
typedef struct Undo
{
    MMSceneSoundUndo state;
    int refs;
} Undo;
typedef struct List
{
    int count;
    MMTag tags[8];
} List;
typedef struct Editor
{
    Fixture base; /* Packet fixture context is the initial member. */
    MMSceneOwner owner;
    MMSceneSoundEditServices s;
    MMSceneSoundUndoServices us;
    MMSceneTrack *frames;
    ChunkIndexFixture archive;
    MMTagServices movie_tags;
    int dirty, indicator, indicators, actor_count, actor_calls, actor_values[3];
    MMTag tags[3];
    Undo *active;
    int alloc_calls, fail_at, grow_attempts, fail_grow, grow_failures, relocate_at, relocations;
    int fail_child, fail_list, fail_resize, fail_new_undo, fail_commit;
    int list_live, undo_live, commits, clears, issues[5], event_plays, publications, published_metadata,
        child_changes_frame;
    int input_resolve_fail, input_fetch_fail, input_mutate, packet_fetch_fail, closes, releases;
    int input_graph_changed, input_refs;
    int check_query_release, released_query_checked;
    int fail_switch, fail_seek, fail_get, fail_restore, flushes, queue_plays, switched, seeks;
    MMI32 published_volume;
    void *published_list;
} Editor;
static void *edit_allocate(void *p, MMI32 bytes)
{
    Editor *e = (Editor *)p;
    void *out;
    e->base.fail = ++e->alloc_calls == e->fail_at;
    out = allocate(p, bytes);
    e->base.fail = 0;
    if (out && e->alloc_calls == e->relocate_at)
    {
        MMCollectionStorage *storage = &e->frames->storage;
        CHECK(storage->buffers[0].data);
        CHECK(mm_storage_resize_exact(storage, 0, storage->buffers[0].capacity + 32, MM_STORAGE_NORMAL) ==
              MM_STORAGE_OK);
        ++e->relocations;
    }
    return out;
}
static void close_tag(void *p, MMSceneSoundTag *r)
{
    Editor *e = (Editor *)p;
    ++e->closes;
    if (!r->tag.sid && r->tag.cache)
    {
        CHECK(e->base.refs[r->tag.number] > 0);
        --e->base.refs[r->tag.number];
        r->tag.cache = NULL;
    }
}
static void release_packet(void *p, MMSceneSound *packet)
{
    Editor *e = (Editor *)p;
    int i;
    CHECK(packet);
    if (e->check_query_release)
    {
        CHECK(e->published_metadata && e->published_list && e->published_volume == packet->vlm);
        CHECK(((List *)e->published_list)->count == packet->ctagc);
        ++e->released_query_checked;
    }
    ++e->releases;
    for (i = 0; i < packet->ctagc; ++i)
        close_tag(e, at(packet, i));
    raw_free(&e->base, packet);
}
static void *resource(Editor *e, MMU32 number)
{
    CHECK(number > 0 && number <= 3);
    ++e->base.resources;
    return e->base.input + number - 1;
}
static int packet_resolve(void *p, MMSceneSoundTag *record)
{
    (void)p;
    (void)record;
    return 1;
}
static void *packet_fetch(void *p, const MMSceneSoundTag *r)
{
    Editor *e = (Editor *)p;
    return e->packet_fetch_fail == (int)r->tag.number ? NULL : resource(e, r->tag.number);
}
static void *resize_track(void *p, void *data, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Editor *e = (Editor *)p;
    void *copy;
    (void)priority;
    if (++e->grow_attempts == e->fail_grow)
    {
        ++e->grow_failures;
        return NULL;
    }
    copy = malloc((size_t)bytes);
    CHECK(copy);
    if (data)
        memcpy(copy, data, old_bytes < bytes ? old_bytes : bytes);
    free(data);
    return copy;
}
static void release_track(void *p, void *data, MMU32 bytes)
{
    (void)p;
    (void)bytes;
    free(data);
}
static void *allocate_records(void *p, MMI32 bytes)
{
    void *out = edit_allocate(p, bytes);
    if (out)
        memset(out, 0, (size_t)bytes);
    return out;
}
static void free_raw(void *p, void *value)
{
    raw_free(&((Editor *)p)->base, value);
}
static void read_movie(void *p, MMMovieSoundState *state)
{
    Editor *e = p;
    if (e->child_changes_frame)
        e->owner.data.currentFrame = 7;
    /* Exercise a changed or removed link after packet installation. */
    if (e->dirty && !e->input_graph_changed && (e->input_resolve_fail || e->input_mutate))
    {
        const MMChunkEdit edit = fixture_chunk_edit(&e->archive);
        const MMChunkChild link = {MM_SOUND_CHUNK, 1, 1};
        CHECK(mm_chunk_unlink(&edit, MM_SOUND_SCENE, 20, &link) == MM_CHUNK_OK);
        if (e->input_mutate)
            fixture_chunk_seed_link(&e->archive, MM_SOUND_SCENE, 20, MM_SOUND_CHUNK, 3, 1);
        e->input_graph_changed = 1;
    }
    state->cache = &e->base;
    state->movie = 10;
    state->scene = e->fail_child ? 99 : 0;
}
static MMChunkEdit edit_archive(void *p)
{
    return fixture_chunk_edit(&((Editor *)p)->archive);
}
static void retain_input(void *p) { ++((Editor *)p)->input_refs; }
static void release_input(void *p) { CHECK(--((Editor *)p)->input_refs >= 0); }
static MMMovieSoundServices movie_sound(void *p)
{
    Editor *e = p;
    MMMovieSoundServices s = {0};
    s.context = e;
    s.read = read_movie;
    s.tags = &e->movie_tags;
    s.edit = edit_archive;
    return s;
}
static MMU32 unused_child(void *p)
{
    (void)p;
    return 0xccccccccU;
}
static void *fetch_input(void *p, const MMTag *tag)
{
    Editor *e = (Editor *)p;
    int index = (int)(tag - e->tags);
    return e->input_fetch_fail == index + 1 ? NULL : resource(e, tag->number);
}
static void *new_list(void *p, MMI32 count)
{
    Editor *e = (Editor *)p;
    void *out;
    CHECK(count < 8);
    if (e->fail_list)
        return NULL;
    out = calloc(1, sizeof(List));
    CHECK(out);
    ++e->list_live;
    return out;
}
static int resize_list(void *p, void *list, MMI32 count)
{
    Editor *e = (Editor *)p;
    if (e->fail_resize)
        return 0;
    ((List *)list)->count = count;
    return 1;
}
static void put_tag(void *p, void *list, MMI32 index, const MMTag *tag)
{
    (void)p;
    CHECK(index >= 0 && index < ((List *)list)->count);
    ((List *)list)->tags[index] = *tag;
}
static void release_list(void *p, void *list)
{
    Editor *e = (Editor *)p;
    CHECK(e->list_live > 0);
    --e->list_live;
    free(list);
}
static void publish_query(void *p, void *list, int metadata, MMI32 volume, int loop)
{
    Editor *e = (Editor *)p;
    ++e->publications;
    e->published_list = list;
    if (metadata)
    {
        e->published_metadata = 1;
        e->published_volume = volume;
        CHECK(loop == 1);
    }
}
static MMI32 actors(void *p)
{
    return ((Editor *)p)->actor_count;
}
static int actor_sound(void *p, MMI32 index)
{
    Editor *e = (Editor *)p;
    ++e->actor_calls;
    CHECK(index < 3);
    return e->actor_values[index];
}
static void dirty(void *p)
{
    ++((Editor *)p)->dirty;
}
static void indicator(void *p, int value)
{
    Editor *e = (Editor *)p;
    e->indicator = value;
    ++e->indicators;
}
static void diagnostic(void *p, int issue)
{
    Editor *e = (Editor *)p;
    CHECK(issue >= 0 && issue < 5);
    ++e->issues[issue];
}
static void undo_release(void *p, void *value)
{
    Editor *e = (Editor *)p;
    Undo *u = (Undo *)value;
    CHECK(u->refs > 0);
    if (!--u->refs)
    {
        mm_scene_sound_undo_dispose(&u->state, &e->s.values);
        --e->undo_live;
        free(u);
    }
}
static void *new_undo(void *p)
{
    Editor *e = (Editor *)p;
    Undo *u;
    if (e->fail_new_undo)
        return NULL;
    u = (Undo *)calloc(1, sizeof(*u));
    CHECK(u);
    u->refs = 1;
    ++e->undo_live;
    return u;
}
static MMSceneSoundUndo *undo_state(void *p, void *u)
{
    (void)p;
    return &((Undo *)u)->state;
}
static int commit_undo(void *p, void *u)
{
    Editor *e = (Editor *)p;
    ++e->commits;
    if (e->fail_commit)
        return 0;
    if (e->active)
        undo_release(e, e->active);
    e->active = (Undo *)u;
    ++e->active->refs;
    return 1;
}
static void clear_undo(void *p)
{
    Editor *e = (Editor *)p;
    Undo *u = e->active;
    ++e->clears;
    e->active = NULL;
    if (u)
        undo_release(e, u);
}
static void packet_free(void *p, void *value)
{
    free_raw(p, value);
}
static void *event_duplicate(void *p, void *value)
{
    Editor *e = (Editor *)p;
    return mm_scene_sound_duplicate((MMSceneSound *)value, &e->base.s);
}
static void event_play(void *p, void *value)
{
    Editor *e = (Editor *)p;
    ++e->event_plays;
    e->base.packet = (MMSceneSound *)value;
    mm_scene_sound_play(e->base.packet, 0, &e->base.s);
}
static int switch_scene(void *p)
{
    Editor *e = (Editor *)p;
    ++e->switched;
    return !e->fail_switch;
}
static int seek_frame(void *p)
{
    Editor *e = (Editor *)p;
    ++e->seeks;
    return !e->fail_seek;
}
static void flush(void *p)
{
    ++((Editor *)p)->flushes;
}
static void play_queue(void *p)
{
    ++((Editor *)p)->queue_plays;
}
static int undo_get(void *p, MMI32 style, int *found, MMSceneSound **out)
{
    Editor *e = (Editor *)p;
    if (e->fail_get)
        return 0;
    return mm_scene_sound_get(style, found, out, &e->s);
}
static void undo_remove(void *p, MMI32 style)
{
    Editor *e = (Editor *)p;
    mm_scene_sound_remove(style, &e->s);
}
static int undo_restore(void *p, const MMSceneSound *value)
{
    Editor *e = (Editor *)p;
    if (e->fail_restore)
        return 0;
    return mm_scene_sound_restore(value->fLoop != 0, 0, value->vlm, value->sty, value->ctagc,
                                  (const MMSceneSoundTag *)(value + 1), &e->s);
}
static void editor_init(Editor *e)
{
    int i;
    MMSceneSoundEditServices *s;
    MMSceneEventServices *v;
    MMStorageAllocator allocator = {NULL, resize_track, release_track};
    memset(e, 0, sizeof(*e));
    init(&e->base);
    e->base.s.allocate = edit_allocate;
    e->base.s.resolve = packet_resolve;
    e->base.s.fetch = packet_fetch;
    allocator.context = e;
    CHECK(mm_scene_owner_init(&e->owner, NULL, 1, &allocator));
    e->frames = &e->owner.tracks[MM_SCENE_FRAMES];
    e->frames->storage.min_grow[0] = e->frames->storage.min_grow[1] = 0;
    e->owner.data.firstFrame = e->owner.data.currentFrame = 1;
    e->owner.data.lastFrame = 10;
    for (i = 0; i < 3; ++i)
        e->tags[i] = e->base.input[i].tag;
    e->input_refs = 2;
    e->movie_tags.retain = retain_input;
    e->movie_tags.release = release_input;
    fixture_chunk_seed_link(&e->archive, MM_SOUND_MOVIE, 10, MM_SOUND_SCENE, 20, 0);
    fixture_chunk_seed_link(&e->archive, MM_SOUND_SCENE, 20, MM_SOUND_CHUNK, 7, 0);
    fixture_chunk_seed_link(&e->archive, MM_SOUND_SCENE, 20, MM_SOUND_CHUNK, 8, 2);
    fixture_chunk_add(&e->archive, MM_SOUND_CHUNK, 1);
    fixture_chunk_add(&e->archive, MM_SOUND_CHUNK, 3);
    s = &e->s;
    s->context = e;
    s->values.context = e;
    s->values.packets = e->base.s;
    s->values.release = release_packet;
    s->allocate_records = allocate_records;
    s->free_raw = free_raw;
    s->movie_sound = movie_sound;
    s->unused_child = unused_child;
    s->fetch_input = fetch_input;
    s->create_tag_list = new_list;
    s->resize_tag_list = resize_list;
    s->put_list_tag = put_tag;
    s->release_list = release_list;
    s->actor_count = actors;
    s->actor_sound = actor_sound;
    s->dirty = dirty;
    s->sound_frame = indicator;
    s->new_undo = new_undo;
    s->undo_state = undo_state;
    s->commit_undo = commit_undo;
    s->release_undo = undo_release;
    s->clear_undo = clear_undo;
    s->diagnostic = diagnostic;
    v = &s->events;
    v->context = e;
    v->owner = &e->owner;
    v->duplicate_sound = event_duplicate;
    v->play_sound = event_play;
    v->owners.context = e;
    v->owners.sound.context = e;
    v->owners.sound.close = close_tag;
    v->owners.sound.free = packet_free;
    e->us.context = e;
    e->us.values = s->values;
    e->us.switch_scene = switch_scene;
    e->us.seek_frame = seek_frame;
    e->us.flush = flush;
    e->us.play = play_queue;
    e->us.clear = clear_undo;
    e->us.get = undo_get;
    e->us.remove = undo_remove;
    e->us.restore = undo_restore;
    e->us.diagnostic = diagnostic;
}
static MMSceneTrackEvent frame_row(const Editor *e, int index)
{
    MMSceneTrackEvent row;
    CHECK(mm_scene_track_get(e->frames, index, &row));
    return row;
}
static MMSceneSound *frame_packet(Editor *e, int index)
{
    CHECK(frame_row(e, index).kind == MM_SCENE_PLAY_SOUND);
    return (MMSceneSound *)mm_scene_track_payload(e->frames, index, NULL);
}
static void seed(Editor *e, int style)
{
    const MMSceneTrackEvent event = {1, MM_SCENE_PLAY_SOUND};
    MMSceneSound *p = make(&e->base, 1);
    p->sty = style;
    p->fLoop = 0;
    ++e->owner.runtime.next_event;
    CHECK(mm_scene_track_insert(e->frames, e->frames->rows.count, mm_scene_sound_size(1), p, &event));
    if (style == 4)
    {
        e->owner.owned[MM_SCENE_BACKGROUND_SOUND] = mm_scene_sound_duplicate(p, &e->base.s);
        e->owner.runtime.background_frame = 1;
    }
    raw_free(&e->base, p);
    e->alloc_calls = e->base.used = e->base.plays = 0;
}
static void editor_clean(Editor *e)
{
    clear_undo(e);
    mm_scene_owner_drop(&e->owner, MM_SCENE_BACKGROUND_SOUND, &e->s.events.owners);
    while (e->frames->rows.count)
        CHECK(mm_scene_owner_erase_frame(&e->owner, e->frames->rows.count - 1, &e->s.events.owners));
    mm_scene_track_dispose(e->frames);
    CHECK(!e->undo_live && !e->list_live);
    clean(&e->base);
}
static void additions(void)
{
    Editor e;
    MMSceneSound *p;
    MMSceneSoundTag tag;
    int failure;
    editor_init(&e);
    CHECK(mm_scene_sound_add(0, 0, 83, 2, 3, e.tags, &e.s));
    p = frame_packet(&e, 0);
    CHECK(p->ctagc == 3 && p->vlm == 83 && e.base.plays == 3 && e.dirty == 2 && e.owner.runtime.next_event == 1);
    CHECK(e.base.refs[1] == 1 && e.base.refs[3] == 1 && !e.base.starts[1] && !e.base.queues[1] && e.base.queues[2]);
    e.relocate_at = e.alloc_calls + 1;
    CHECK(mm_scene_sound_add(0, 1, 29, 2, 1, e.tags, &e.s));
    CHECK(e.relocations == 1);
    p = frame_packet(&e, 0);
    CHECK(p->ctagc == 4 && p->vlm == 83 && e.base.volumes[1] == 29 && e.base.refs[1] == 2 && e.owner.runtime.next_event == 1);
    CHECK(mm_scene_sound_add(1, 0, 56, 2, 1, e.tags, &e.s));
    p = frame_packet(&e, 0);
    CHECK(p->ctagc == 1 && p->fLoop == 1 && e.base.refs[1] == 1 && !e.base.refs[3]);
    editor_clean(&e);
    for (failure = 1; failure <= 2; ++failure)
    {
        editor_init(&e);
        seed(&e, 2);
        e.fail_at = failure;
        CHECK(!mm_scene_sound_add(0, 0, 70, 2, 1, e.tags, &e.s));
        CHECK(e.frames->rows.count == 1 && e.base.refs[1] == 1 && !e.dirty);
        editor_clean(&e);
        editor_init(&e);
        seed(&e, 2);
        e.fail_at = failure;
        CHECK(!mm_scene_sound_add(0, 1, 70, 2, 1, e.tags, &e.s));
        CHECK(e.frames->rows.count == 1 && e.base.refs[1] == 1 && !e.dirty);
        editor_clean(&e);
    }
    editor_init(&e);
    seed(&e, 2);
    e.fail_grow = e.grow_attempts + 1;
    CHECK(!mm_scene_sound_add(0, 1, 70, 2, 1, e.tags, &e.s));
    CHECK(e.grow_failures && e.base.refs[1] == 1 && e.base.live == 0 && frame_packet(&e, 0)->ctagc == 1);
    editor_clean(&e);
    for (failure = 1; failure <= 2; ++failure)
    {
        editor_init(&e);
        seed(&e, 2);
        e.fail_grow = e.grow_attempts + failure;
        CHECK(!mm_scene_sound_add(0, 0, 70, 2, 1, e.tags, &e.s));
        CHECK(e.grow_failures == 1 && e.dirty == 1 && e.base.refs[1] == 1 && e.owner.runtime.next_event == 1);
        CHECK(e.frames->rows.count == 1 && frame_packet(&e, 0)->vlm == 80);
        editor_clean(&e);
    }
    editor_init(&e);
    seed(&e, 2);
    e.fail_child = 1;
    CHECK(!mm_scene_sound_add(0, 0, 70, 2, 1, e.tags, &e.s));
    CHECK(!e.dirty && e.base.refs[1] == 1);
    CHECK(!mm_scene_sound_add(0, 1, 70, 2, 1, e.tags, &e.s));
    CHECK(e.base.refs[1] == 1 && !e.base.live);
    editor_clean(&e);
    editor_init(&e);
    e.input_resolve_fail = 1;
    e.input_fetch_fail = 2;
    CHECK(mm_scene_sound_add(0, 0, 30, 2, 3, e.tags, &e.s));
    CHECK(e.base.plays == 1 && e.base.queues[3] && !e.base.starts[3]);
    editor_clean(&e);
    editor_init(&e);
    e.input_mutate = 1;
    CHECK(mm_scene_sound_add(0, 0, 30, 2, 1, e.tags, &e.s));
    mm_scene_sound_get_tag(frame_packet(&e, 0), 0, &tag);
    CHECK(e.tags[0].number == 3 && tag.tag.number == 1 && tag.child == 1 && e.base.volumes[3] == 30 &&
          e.input_refs == 2);
    editor_clean(&e);
    editor_init(&e);
    e.child_changes_frame = 1;
    CHECK(mm_scene_sound_add(0, 0, 30, 4, 1, e.tags, &e.s));
    CHECK(frame_row(&e, 0).frame == 1 && e.owner.runtime.background_frame == 1 && e.owner.data.currentFrame == 7);
    editor_clean(&e);
    editor_init(&e);
    e.fail_at = 3; /* MIDI cache duplicate fails after array+packet allocation. */
    CHECK(mm_scene_sound_add(0, 0, 30, 4, 1, e.tags, &e.s));
    CHECK(e.frames->rows.count == 1 && !e.owner.owned[MM_SCENE_BACKGROUND_SOUND] && e.indicator);
    editor_clean(&e);
    editor_init(&e);
    CHECK(!mm_scene_sound_add(0, 1, 30, 2, 3, e.tags, &e.s) && !e.alloc_calls);
    editor_clean(&e);
}
static void queries(void)
{
    Editor e;
    MMSceneSound *p;
    void *list;
    MMI32 volume;
    int found, loop;
    editor_init(&e);
    p = (MMSceneSound *)&e;
    found = 1;
    CHECK(mm_scene_sound_get(2, &found, &p, &e.s) && !found && p == (MMSceneSound *)&e);
    volume = -17;
    loop = 7;
    list = &e;
    CHECK(mm_scene_sound_query(2, &list, &volume, &loop, &e.s) && !list && volume == -17 && loop == 7);
    seed(&e, 2);
    frame_packet(&e, 0)->fLoop = 0x01000000;
    e.s.query_context = &e;
    e.s.publish_query = publish_query;
    e.check_query_release = 1;
    CHECK(mm_scene_sound_query(2, &list, &volume, &loop, &e.s));
    CHECK(list && volume == 80 && loop == 1 && e.base.refs[1] == 1);
    CHECK(e.publications == 3 && e.published_metadata && e.published_volume == 80 && e.published_list == list);
    CHECK(e.released_query_checked == 1);
    e.check_query_release = 0;
    CHECK(((List *)list)->tags[0].cache == &e.base);
    release_list(&e, list);
    e.fail_at = e.alloc_calls + 1;
    p = (MMSceneSound *)&e;
    CHECK(!mm_scene_sound_get(2, &found, &p, &e.s) && !found && !p);
    e.fail_at = 0;
    e.fail_list = 1;
    volume = -17;
    loop = 7;
    CHECK(!mm_scene_sound_query(2, &list, &volume, &loop, &e.s) && !list && volume == -17 && loop == 7 &&
          e.base.refs[1] == 1);
    e.fail_list = 0;
    e.fail_resize = 1;
    CHECK(!mm_scene_sound_query(2, &list, &volume, &loop, &e.s) && !list && !e.list_live && e.base.refs[1] == 1);
    CHECK(e.issues[MM_SCENE_SOUND_LIST_RESIZE] == 1);
    editor_clean(&e);
}
static void indicators_and_restore(void)
{
    Editor e;
    int missing;
    editor_init(&e);
    e.actor_count = 3;
    e.actor_values[1] = 1;
    e.owner.runtime.disabled = 65535;
    mm_scene_sound_update_indicator(&e.s);
    CHECK(e.indicator && e.actor_calls == 2);
    seed(&e, 4);
    e.actor_calls = 0;
    mm_scene_sound_update_indicator(&e.s);
    CHECK(e.indicator && !e.actor_calls);
    mm_scene_sound_volume(4, 45, &e.s);
    CHECK(frame_packet(&e, 0)->vlm == 45 &&
          ((MMSceneSound *)e.owner.owned[MM_SCENE_BACKGROUND_SOUND])->vlm == 80);
    mm_scene_sound_remove(4, &e.s);
    CHECK(!e.frames->rows.count && !e.owner.owned[MM_SCENE_BACKGROUND_SOUND] && e.indicator &&
          e.owner.runtime.background_frame == 1);
    e.actor_values[1] = 0;
    mm_scene_sound_update_indicator(&e.s);
    CHECK(!e.indicator);
    editor_clean(&e);
    editor_init(&e);
    seed(&e, 2);
    e.owner.data.currentFrame = 2;
    missing = e.issues[MM_SCENE_SOUND_MISSING];
    mm_scene_sound_volume(2, 20, &e.s);
    CHECK(e.issues[MM_SCENE_SOUND_MISSING] == missing + 2 && frame_packet(&e, 0)->vlm == 80);
    mm_scene_sound_remove(2, &e.s);
    CHECK(e.frames->rows.count == 1 && e.issues[MM_SCENE_SOUND_STYLE_MISSING] == 1);
    editor_clean(&e);
    editor_init(&e);
    e.packet_fetch_fail = 2;
    CHECK(mm_scene_sound_restore(0, 0, 66, 2, 3, e.base.input, &e.s));
    CHECK(e.event_plays == 1 && e.base.plays == 1 && e.frames->rows.count == 1);
    CHECK(!mm_scene_sound_restore(0, 1, 66, 2, 1, e.base.input, &e.s) &&
          e.issues[MM_SCENE_SOUND_QUEUE_RESTORE] == 1);
    e.owner.runtime.disabled |= 1;
    CHECK(mm_scene_sound_restore(0, 0, 20, 4, 1, e.base.input, &e.s));
    CHECK(e.owner.owned[MM_SCENE_BACKGROUND_SOUND]);
    editor_clean(&e);
}
static void transactions(void)
{
    Editor e;
    int failure;
    for (failure = 1; failure <= 4; ++failure)
    {
        editor_init(&e);
        seed(&e, 2);
        e.fail_at = failure;
        CHECK(!mm_scene_sound_add_undo(0, 0, 60, 2, e.tags, &e.s));
        CHECK(e.base.refs[1] == 1 && !e.undo_live && e.frames->rows.count == 1);
        CHECK(e.clears == (failure > 2));
        editor_clean(&e);
    }
    editor_init(&e);
    seed(&e, 2);
    e.fail_new_undo = 1;
    CHECK(!mm_scene_sound_add_undo(0, 0, 60, 2, e.tags, &e.s) && !e.undo_live && !e.commits);
    editor_clean(&e);
    editor_init(&e);
    seed(&e, 2);
    e.fail_commit = 1;
    CHECK(!mm_scene_sound_add_undo(0, 0, 60, 2, e.tags, &e.s) && !e.undo_live && !e.clears &&
          frame_packet(&e, 0)->vlm == 80);
    editor_clean(&e);
    editor_init(&e);
    CHECK(mm_scene_sound_add_undo(0, 0, 60, 2, e.tags, &e.s));
    CHECK(e.active && !e.active->state.sound && e.active->state.style == 2);
    editor_clean(&e);
    editor_init(&e);
    seed(&e, 2);
    e.owner.data.currentFrame = 2;
    CHECK(mm_scene_sound_add(0, 0, 60, 2, 1, e.tags, &e.s));
    CHECK(e.frames->rows.count == 2);
    CHECK(mm_scene_sound_remove_undo(2, &e.s) && e.frames->rows.count == 1 && e.active->state.sound && frame_row(&e, 0).frame == 1);
    editor_clean(&e);
    editor_init(&e);
    CHECK(!mm_scene_sound_remove_undo(2, &e.s) && !e.undo_live && !e.commits);
    editor_clean(&e);
    for (failure = 1; failure <= 2; ++failure)
    {
        editor_init(&e);
        seed(&e, 2);
        e.fail_at = failure;
        CHECK(!mm_scene_sound_remove_undo(2, &e.s) && !e.undo_live && e.frames->rows.count == 1 && e.base.refs[1] == 1);
        editor_clean(&e);
    }
}
static void swaps(void)
{
    Editor e;
    Undo *u;
    MMSceneSound *p;
    int i, mode;
    editor_init(&e);
    seed(&e, 2);
    u = (Undo *)new_undo(&e);
    u->state.style = 2;
    e.active = u;
    for (i = 0; i < 4; ++i)
    {
        CHECK(mm_scene_sound_undo_swap(&u->state, &e.us));
        CHECK((e.frames->rows.count == 0) == (u->state.sound != NULL));
    }
    CHECK(e.queue_plays == 4 && e.flushes == 4);
    editor_clean(&e);
    editor_init(&e);
    seed(&e, 2);
    u = (Undo *)new_undo(&e);
    e.active = u;
    CHECK(mm_scene_sound_undo_set(&u->state, frame_packet(&e, 0), &e.s.values));
    frame_packet(&e, 0)->vlm = 31;
    CHECK(mm_scene_sound_undo_swap(&u->state, &e.us));
    CHECK(frame_packet(&e, 0)->vlm == 80 && u->state.sound->vlm == 31);
    CHECK(mm_scene_sound_undo_swap(&u->state, &e.us));
    CHECK(frame_packet(&e, 0)->vlm == 31 && u->state.sound->vlm == 80);
    editor_clean(&e);
    for (mode = 0; mode < 5; ++mode)
    {
        editor_init(&e);
        seed(&e, 2);
        u = (Undo *)new_undo(&e);
        e.active = u;
        u->state.style = 2;
        e.fail_switch = mode == 0;
        e.fail_seek = mode == 1;
        e.fail_get = mode == 2;
        if (mode == 3)
        {
            CHECK(mm_scene_sound_undo_set(&u->state, frame_packet(&e, 0), &e.s.values));
            e.fail_restore = 1;
        }
        if (mode == 4)
        {
            e.alloc_calls = 0;
            e.fail_at = 2;
        } /* current clone succeeds, saved clone fails */
        CHECK(!mm_scene_sound_undo_swap(&u->state,
                                              &e.us)); /* clear destroys u: sanitizer checks no later access. */
        CHECK(!e.active && !e.undo_live && e.clears == 1 && !e.queue_plays);
        CHECK(e.frames->rows.count == (mode == 3 ? 0 : 1));
        editor_clean(&e);
    }
    editor_init(&e);
    u = (Undo *)new_undo(&e);
    e.active = u;
    u->state.style = 2;
    CHECK(!mm_scene_sound_undo_swap(&u->state, &e.us) && e.active && !e.clears);
    editor_clean(&e);
    editor_init(&e);
    seed(&e, 2);
    u = (Undo *)new_undo(&e);
    e.active = u;
    CHECK(mm_scene_sound_undo_set(&u->state, frame_packet(&e, 0), &e.s.values));
    p = u->state.sound;
    e.fail_at = e.alloc_calls + 1;
    CHECK(!mm_scene_sound_undo_set(&u->state, frame_packet(&e, 0), &e.s.values) && u->state.sound == p);
    editor_clean(&e);
}
static void background(void)
{
    Editor e;
    int overflow;
    MMU32 offset;
    editor_init(&e);
    mm_scene_sound_background_play(&e.s);
    CHECK(!e.base.plays);
    seed(&e, 4);
    e.owner.data.currentFrame = 3;
    mm_scene_sound_background_play(&e.s);
    CHECK(e.base.starts[1] == 333);
    CHECK(mm_scene_sound_background_offset(2, 1, &overflow) == 166 && !overflow);
    CHECK(mm_scene_sound_background_offset(1, 2, &overflow) == (MMU32)(0U - 166U) && !overflow);
    offset = mm_scene_sound_background_offset(2147483647, 0, &overflow);
    CHECK(overflow && offset == 1431655598U);
    CHECK(mm_scene_sound_background_offset((-2147483647 - 1), 2147483647, &overflow) == 166 && !overflow);
    e.owner.data.currentFrame = 2147483647;
    e.owner.runtime.background_frame = 0;
    mm_scene_sound_background_play(&e.s);
    CHECK(e.issues[MM_SCENE_SOUND_OFFSET_OVERFLOW] == 1 && e.base.starts[1] == offset);
    editor_clean(&e);
}
int main(void)
{
    Fixture f;
    MMSceneSound *p;
    init(&f);
    p = make(&f, 1);
    drop(&f, p);
    clean(&f);
    additions();
    queries();
    indicators_and_restore();
    transactions();
    swaps();
    background();
    puts("C89 Scene sound editing/undo: event commits, reference rollback, queries, preview, swaps and clocks passed.");
    return 0;
}
