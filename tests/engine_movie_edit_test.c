/* Movie edit sequencing, placement ownership and undo failure contracts. */
#include "mm/movie_edit.h"
#include "mm/actor_math.h"
#include "mm/actor_undo.h"
#include "mm/scene_selection.h"
#include "actor_stage_owner_fixture.h"
#include "movie_scene_session_fixture.h"
#include <stddef.h>
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
    FAIL_CACHE = 1,
    FAIL_NEW_ACTOR,
    FAIL_ADD,
    FAIL_DUP,
    FAIL_STAGE,
    FAIL_APPLY,
    FAIL_COMMIT,
    FAIL_PASTE,
    FAIL_UNDO,
    FAIL_NEW_SCENE,
    FAIL_BACKGROUND,
    FAIL_NEW_UNDO,
    FAIL_SOUND
};
enum
{
    VIEW = 100,
    LOOKUP,
    SELECT,
    DUP,
    RELEASE,
    PLACE,
    TRANSFER,
    APPLY,
    FRAME,
    DIRTY,
    INVALIDATE,
    NUKED,
    RESTORE,
    MAKE_UNDO,
    BEGIN,
    END,
    CACHE,
    TITLE,
    NEW_SCENE,
    REMOVE_SCENE,
    BACKGROUND,
    NEW_UNDO,
    CONFIGURE,
    UNDO_STATE,
    UNDO_VALIDATE,
    COMMIT,
    NOTIFY,
    CLEAR,
    UNDO_RELEASE,
    SOUND_FETCH,
    SOUND_VOLUME,
    SOUND_STYLE,
    SOUND_RELEASE,
    ROLL_CHANGED,
    CURSOR,
    SIZE_SOUND,
    SIZE_SOUND_ALTERNATE,
    WORLD_DIRTY,
    MARK_VIEWS
};
typedef struct Test Test;
typedef struct Actor
{
    Test *test;
    int id, refs, visible;
    MMTag tag;
    StageOwnerFixture stage;
} Actor;
typedef struct Undo
{
    Test *test;
    MMMovieUndoContext base;
    MMActorUndoState actor_state;
    MMScenePauseUndo pause;
    MMSceneBackgroundUndo background;
    MMSceneTransitionUndo transition;
    MMSceneSoundUndo sound;
    int refs, bound, actor;
} Undo;
struct Test
{
    MMMovieEditServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    MovieSceneArchiveFixture archive;
    MMSceneOwner scene_owner;
    Actor actor, fresh, copy;
    Actor *selected, *placement;
    Undo undo;
    int view, inScene, fail, selectedCalls, selectionCalls, frame, actorBindings, sceneBindings;
    int actorSeeks, actorSoundFrames;
    MMI32 actorSeekFrame;
    int sceneDirty, sceneSoundFrames, camera, cameraCloses;
    MMU32 sceneInputNumber;
    int dirty, invalidates, bars, nuked, error, diagnostics, entire, restore, placeCalls, undoCount;
    int lastUndoCalls, notify, commits, clears, rowChanges;
    int begin, end, cacheBackground, defaults, newScene, removeScene, removedIndex;
    int soundEnabled, played, flushed, soundRefs, volumeCalls, styleCalls;
    int switchSelection, undoOnFresh;
    int events[128], eventCount;
};
static void event(Test *t, int e)
{
    t->events[t->eventCount++] = e;
}
static void *selected(void *p)
{
    Test *t = p;
    ++t->selectedCalls;
    event(t, SELECT);
    return t->selected;
}
static void *lookup(void *p, MMI32 id)
{
    Test *t = p;
    event(t, LOOKUP);
    return t->inScene && id == t->actor.id ? &t->actor : 0;
}
static void select_actor(void *p, void *a)
{
    Test *t = p;
    t->selected = a;
    ++t->selectionCalls;
    event(t, SELECT);
}
static void *view(void *p)
{
    Test *t = p;
    event(t, VIEW);
    return t->view ? t : 0;
}
static void *new_actor(void *p, const MMTag *tag)
{
    Test *t = p;
    if (t->fail == FAIL_NEW_ACTOR)
        return 0;
    t->fresh.refs = 1;
    t->fresh.tag = *tag;
    return &t->fresh;
}
static int visible(void *p)
{
    return ((Actor *)p)->visible;
}
static MMActorStageEditServices bind_stage(void *p)
{
    Actor *a = p;
    a->stage.fail_entrance = a->test->fail == FAIL_STAGE;
    a->stage.owner.runtime.actor = a->id;
    return stage_fixture_services(&a->stage);
}
static void release(void *p)
{
    Actor *a = p;
    if (!--a->refs)
        stage_fixture_dispose(&a->stage);
    event(a->test, RELEASE);
}
static int add_actor(void *p, void *a)
{
    Test *t = p;
    if (t->fail == FAIL_ADD)
        return 0;
    ++((Actor *)a)->refs;
    t->inScene = 1;
    return 1;
}
static int remove_actor(void *p, MMI32 id)
{
    Test *t = p;
    if (id != t->actor.id)
        return 0;
    t->selected = 0;
    t->inScene = 0;
    return t->fail != FAIL_APPLY;
}
static Test *actor_test(void *p)
{
    Actor *a = (Actor *)((char *)p - offsetof(Actor, stage));
    return a->test;
}
static MMActorSubrouteEditServices bind_actor_edit(Actor *a)
{
    Test *t = a->test;
    ++t->actorBindings;
    event(t, APPLY);
    return stage_fixture_services(&a->stage).subroute;
}
static int fail_reserve(void *p, MMU32 entries, MMU32 bytes)
{
    (void)p;
    (void)entries;
    (void)bytes;
    return 0;
}
static int edit_scene_frame(void *p, MMI32 *out)
{
    *out = actor_test(p)->frame;
    return 1;
}
static int seek_actor_scene(void *p, MMActorState *state, MMI32 frame)
{
    Test *t = actor_test(p);
    (void)state;
    ++t->actorSeeks;
    t->actorSeekFrame = frame;
    return t->fail != FAIL_APPLY;
}
static int flag_true(void *p) { (void)p; return 1; }
static void lock_edit_events(void *p) { mm_actor_events_lock(p); }
static void unlock_edit_events(void *p) { mm_actor_events_unlock(p); }
static const void *edit_payload(void *p, MMI32 index) { return mm_actor_events_payload(p, index, NULL); }
static MMActorTransformEditServices bind_transform(void *p)
{
    Actor *actor = p;
    MMActorTransformEditServices s = {0};
    s.subroute = bind_actor_edit(actor);
    s.subroute.route.edits.scene_frame = edit_scene_frame;
    if (actor->test->fail == FAIL_APPLY)
        s.subroute.route.edits.tables.reserve_events = fail_reserve;
    s.fill.edits = s.subroute.route.edits;
    s.fill.lock_events = lock_edit_events;
    s.fill.unlock_events = unlock_edit_events;
    s.fill.payload_view = edit_payload;
    s.scene_present = flag_true;
    s.seek_scene = seek_actor_scene;
    s.scrollbars = stage_fixture_effect;
    return s;
}
static int apply_actor_resource(void *p, MMActorState *state, MMI32 index)
{
    Test *t = actor_test(p);
    (void)index;
    if (t->fail != FAIL_APPLY)
        return 1;
    state->cel = 555;
    return 0;
}
static void *current_costume(void *p, MMI32 body_set, int *custom)
{
    (void)p;
    (void)body_set;
    *custom = 0;
    return NULL;
}
static void *fetch_costume(void *p, MMI32 costume) { (void)costume; return p; }
static MMI32 costume_set(void *p, void *resource) { (void)p; (void)resource; return 3; }
static void release_resource(void *p, void *resource) { (void)p; (void)resource; }
static MMActorCostumeEditServices bind_costume(void *p)
{
    MMActorCostumeEditServices s = {0};
    s.context = p;
    s.edits = bind_actor_edit(p).route.edits;
    s.edits.apply_event = apply_actor_resource;
    s.current = current_costume;
    s.fetch_custom = fetch_costume;
    s.body_set = costume_set;
    s.release = release_resource;
    return s;
}
static void *fetch_actor_sound(void *p, const MMTag *tag) { (void)tag; return p; }
static int actor_cel_count(void *p, MMI32 action, MMI32 *count)
{
    (void)p;
    (void)action;
    *count = 4;
    return 1;
}
static void actor_sound_info(void *p, void *sound, MMActorSoundInfo *info)
{
    (void)p;
    (void)sound;
    info->style = 2;
    info->volume = 73;
    info->no_sound = 0;
}
static void set_actor_sound_frame(void *p) { ++((Actor *)p)->test->actorSoundFrames; }
static MMActorSoundEditServices bind_actor_sound(void *p)
{
    MMActorSoundEditServices s = {0};
    s.context = s.runtime.context = p;
    s.runtime.fetch = fetch_actor_sound;
    s.runtime.release = release_resource;
    s.runtime.cel_count = actor_cel_count;
    s.edits = bind_actor_edit(p).route.edits;
    s.edits.apply_event = apply_actor_resource;
    s.sound_info = actor_sound_info;
    s.sounds_suppressed = flag_true;
    s.set_sound_frame = set_actor_sound_frame;
    return s;
}
static MMI32 frame(void *p)
{
    Test *t = p;
    event(t, FRAME);
    return t->frame;
}
static MMActorUndoState *last_actor_undo(void *p)
{
    Test *t = p;
    ++t->lastUndoCalls;
    return t->undoCount && t->undo.actor ? &t->undo.actor_state : NULL;
}
static void place(void *p, int entire)
{
    Test *t = p;
    ++t->placeCalls;
    t->entire = entire;
    event(t, PLACE);
}
static void placement(void *p, void *owned)
{
    Test *t = p;
    t->placement = owned;
    event(t, TRANSFER);
}
static int paste_actor(void *p, void *a)
{
    Test *t = p;
    (void)a;
    return t->fail != FAIL_PASTE;
}
static MMI32 route_paste_distance(void *p, const MMActorPoint *delta)
{
    (void)p;
    return delta->dxr;
}
static void route_paste_diagnostic(void *p, int fault)
{
    (void)p;
    (void)fault;
}
static int route_paste_flags(void *p, MMI32 action, MMU32 *flags)
{
    Actor *a = p;
    (void)action;
    *flags = 8;
    if (a->test->switchSelection)
        a->test->selected = &a->test->fresh;
    return a->test->fail != FAIL_PASTE;
}
static MMActorRoutePasteServices bind_route_paste(void *p)
{
    Actor *a = p;
    MMActorRoutePasteServices s = {0};
    s.subroute = stage_fixture_services(&a->stage).subroute;
    s.legacy_static_mode = 1;
    s.subroute.action.context = a;
    s.subroute.action.action_flags = route_paste_flags;
    s.distance = route_paste_distance;
    s.diagnostic = route_paste_diagnostic;
    return s;
}
static void long_op(void *p, int begin)
{
    Test *t = p;
    if (begin)
    {
        ++t->begin;
        event(t, BEGIN);
    }
    else
    {
        ++t->end;
        event(t, END);
    }
}
static int cache(void *p, const MMTag *tag, int background)
{
    Test *t = p;
    (void)tag;
    t->cacheBackground = background;
    event(t, CACHE);
    return t->fail != FAIL_CACHE;
}
static void title(void *p)
{
    Test *t = p;
    ++t->defaults;
    t->data.title.count = 7;
    event(t, TITLE);
}
static MMSceneOwnerServices scene_owners(Test *t);
static void *create_background_scene(void *p)
{
    Test *t = p;
    ++t->newScene;
    event(t, NEW_SCENE);
    return t->fail == FAIL_NEW_SCENE ? NULL : t;
}
static int open_background_movie(void *p, void *file, MMU32 *number, MMMovieSession **out)
{
    return p == file && fixture_scene_open(&((Test *)p)->archive, number, out);
}
static void *read_background_scene(void *p, void *resources, MMU32 number, const MMMovieSession *session)
{
    Test *t = p;
    const MMSceneOwnerServices owners = scene_owners(t);
    if (p != resources || session != t->data.session || fixture_scene_slot(&t->archive, number) < 0)
        return NULL;
    mm_scene_owner_dispose(&t->scene_owner, &owners);
    if (!mm_scene_owner_init(&t->scene_owner, t, 1, NULL))
        return NULL;
    t->scene_owner.data.transition = 1;
    return t;
}
static void delete_background_scene(void *p, MMU32 movie, const MMChunkChild *child)
{
    Test *t = p;
    if (movie != FIXTURE_MOVIE_ROOT || child->type != MM_SCENE_CHUNK)
        abort();
    ++t->removeScene;
    t->removedIndex = child->slot;
    event(t, REMOVE_SCENE);
}
static void *new_undo(void *p)
{
    Test *t = p;
    event(t, NEW_UNDO);
    if (t->fail == FAIL_NEW_UNDO)
        return 0;
    t->undo.refs = 1;
    return &t->undo;
}
static void configure(void *p, void *u, MMI32 index, const MMTag *tag)
{
    Test *t = p;
    (void)u;
    (void)tag;
    t->removedIndex = index;
    event(t, CONFIGURE);
}
static void release_undo(void *p)
{
    Undo *u = p;
    --u->refs;
    event(u->test, UNDO_RELEASE);
}
static int sounds(void *p)
{
    return ((Test *)p)->soundEnabled;
}
static void play(void *p)
{
    ++((Test *)p)->played;
}
static void flush(void *p)
{
    ++((Test *)p)->flushed;
}
static void *sound(void *p, const MMTag *tag)
{
    Test *t = p;
    (void)tag;
    event(t, SOUND_FETCH);
    if (t->fail == FAIL_SOUND)
        return 0;
    ++t->soundRefs;
    return t;
}
static MMI32 volume(void *p)
{
    Test *t = p;
    ++t->volumeCalls;
    event(t, SOUND_VOLUME);
    return 83;
}
static MMI32 style(void *p)
{
    Test *t = p;
    ++t->styleCalls;
    event(t, SOUND_STYLE);
    return 4;
}
static void release_sound(void *p)
{
    Test *t = p;
    --t->soundRefs;
    event(t, SOUND_RELEASE);
}
static void dirty(void *p)
{
    Test *t = p;
    ++t->dirty;
    event(t, DIRTY);
}
static void invalidate(void *p, int bars)
{
    Test *t = p;
    ++t->invalidates;
    t->bars = bars;
    event(t, INVALIDATE);
}
static void nuked(void *p)
{
    Test *t = p;
    ++t->nuked;
    event(t, NUKED);
}
static void diagnostic(void *p, int issue)
{
    Test *t = p;
    ++t->diagnostics;
    t->error = issue;
}
static MMMovieUndoContext *undo_state(void *p, void *u)
{
    event((Test *)p, UNDO_STATE);
    return &((Undo *)u)->base;
}
static void undo_validate(void *p, void *u)
{
    Undo *undo = u;
    if (undo->base.movie == p) undo->bound = 1;
    event((Test *)p, UNDO_VALIDATE);
}
static MMI32 scene_index(void *p)
{
    return ((Test *)p)->scenes.index;
}
static int commit(void *p, void *u)
{
    Test *t = p;
    ++t->commits;
    event(t, COMMIT);
    if (t->fail == FAIL_COMMIT)
        return 0;
    ++((Undo *)u)->refs;
    return 1;
}
static void clear(void *p)
{
    Test *t = p;
    ++t->clears;
    t->undo.refs = 0;
    event(t, CLEAR);
}
static void *scene_sound_allocate(void *p, MMI32 bytes)
{
    return ((Test *)p)->fail == FAIL_APPLY ? NULL : malloc((size_t)bytes);
}
static void scene_sound_free(void *p, void *value) { (void)p; free(value); }
static void scene_sound_release(void *p, MMSceneSound *value) { scene_sound_free(p, value); }
static void scene_sound_retain(void *p, const MMSceneSoundTag *tag) { (void)p; (void)tag; }
static void scene_sound_close(void *p, MMSceneSoundTag *tag) { (void)p; (void)tag; }
static MMSceneSoundValueServices scene_sound_values(Test *t)
{
    MMSceneSoundValueServices values = {0};
    values.context = values.packets.context = t;
    values.release = scene_sound_release;
    values.packets.allocate = scene_sound_allocate;
    values.packets.retain = scene_sound_retain;
    return values;
}
static void *scene_duplicate_sound(void *p, void *packet)
{
    const MMSceneSoundValueServices values = scene_sound_values(p);
    return mm_scene_sound_duplicate(packet, &values.packets);
}
static int scene_camera_tag(void *p, MMI32 camera, MMTag *tag)
{
    (void)p;
    tag->sid = 2;
    tag->number = camera;
    return 1;
}
static int scene_cache_camera(void *p, const MMTag *tag)
{
    (void)tag;
    return ((Test *)p)->fail != FAIL_CACHE;
}
static void scene_close_camera(void *p, MMTag *tag) { (void)tag; ++((Test *)p)->cameraCloses; }
static int set_scene_camera(void *p, void *background, MMI32 camera)
{
    Test *t = p;
    (void)background;
    t->camera = camera;
    return 1;
}
static void *fetch_background(void *p, MMTag *tag)
{
    Test *t = p;
    event(t, BACKGROUND);
    return t->fail == FAIL_BACKGROUND && tag->number == 5 ? NULL : t;
}
static MMI32 current_camera(void *p) { return ((Test *)p)->camera; }
static int scene_transition(void *p) { return ((Test *)p)->scene_owner.data.transition; }
static int cannot_notify(void *p) { (void)p; return 0; }
static void ignore_change(void *p) { (void)p; }
static int scene_sound_child(void *p, MMU32 number, MMU32 *child)
{
    (void)p;
    *child = number + 7;
    return 1;
}
static MMU32 scene_sound_unused(void *p) { (void)p; return 29; }
static int scene_sound_resolve(void *p, MMTag *tag, MMU32 child)
{
    (void)p;
    tag->number = child;
    return 1;
}
static void *scene_sound_fetch(void *p, const MMTag *tag)
{
    ((Test *)p)->sceneInputNumber = tag->number;
    return NULL;
}
static void scene_edit_dirty(void *p) { ++((Test *)p)->sceneDirty; }
static void scene_sound_frame(void *p, int value) { ((Test *)p)->sceneSoundFrames += value; }
static void *new_scene_edit_undo(void *p, int kind) { (void)kind; return new_undo(p); }
static MMSceneBackgroundUndo *scene_background_undo(void *p, void *u) { (void)p; return &((Undo *)u)->background; }
static MMScenePauseUndo *scene_pause_undo(void *p, void *u) { (void)p; return &((Undo *)u)->pause; }
static MMSceneTransitionUndo *scene_transition_undo(void *p, void *u) { (void)p; return &((Undo *)u)->transition; }
static MMSceneSoundUndo *scene_sound_undo(void *p, void *u) { (void)p; return &((Undo *)u)->sound; }
static void release_scene_edit_undo(void *p, void *object)
{
    Undo *u = object;
    const MMSceneSoundValueServices values = scene_sound_values(p);
    release_undo(u);
    if (!u->refs)
        mm_scene_sound_undo_dispose(&u->sound, &values);
}
static void clear_scene_edit_undo(void *p)
{
    Test *t = p;
    const MMSceneSoundValueServices values = scene_sound_values(t);
    mm_scene_sound_undo_dispose(&t->undo.sound, &values);
    clear(t);
}
static MMSceneOwnerServices scene_owners(Test *t)
{
    MMSceneOwnerServices s = {0};
    s.context = s.sound.context = t;
    s.release = release_resource;
    s.sound.close = scene_sound_close;
    s.sound.free = scene_sound_free;
    return s;
}
static void note_scene_binding(Test *t)
{
    ++t->sceneBindings;
    event(t, APPLY);
}
static MMSceneEditServices bind_scene_edit(void *p)
{
    Test *t = p;
    MMSceneEditServices s = {0};
    note_scene_binding(t);
    s.context = s.sound.context = s.sound.events.context = t;
    s.sound.events.owner = &t->scene_owner;
    s.sound.events.owners = scene_owners(t);
    s.sound.events.owners.lights_off = release_resource;
    s.sound.events.set_camera = set_scene_camera;
    s.sound.events.fetch_background = fetch_background;
    s.sound.events.transition = scene_transition;
    s.sound.events.duplicate_sound = scene_duplicate_sound;
    s.sound.values = scene_sound_values(t);
    s.sound.allocate_records = scene_sound_allocate;
    s.sound.free_raw = scene_sound_free;
    s.sound.child = scene_sound_child;
    s.sound.unused_child = scene_sound_unused;
    s.sound.resolve_input = scene_sound_resolve;
    s.sound.fetch_input = scene_sound_fetch;
    s.sound.dirty = scene_edit_dirty;
    s.sound.sound_frame = scene_sound_frame;
    s.sound.new_undo = new_undo;
    s.sound.undo_state = scene_sound_undo;
    s.sound.commit_undo = commit;
    s.sound.release_undo = release_scene_edit_undo;
    s.sound.clear_undo = clear_scene_edit_undo;
    s.new_undo = new_scene_edit_undo;
    s.background_state = scene_background_undo;
    s.pause_state = scene_pause_undo;
    s.commit_undo = commit;
    s.release_undo = release_scene_edit_undo;
    s.camera_tag = scene_camera_tag;
    s.cache_tag = scene_cache_camera;
    s.close_tag = scene_close_camera;
    s.current_camera = current_camera;
    s.background_name = ignore_change;
    s.scene_change = ignore_change;
    s.can_notify = cannot_notify;
    return s;
}
static MMScenePropertyServices bind_scene_properties(void *p)
{
    Test *t = p;
    MMScenePropertyServices s = {0};
    note_scene_binding(t);
    s.context = t;
    s.data = &t->scene_owner.data;
    s.new_undo = new_scene_edit_undo;
    s.transition = scene_transition_undo;
    s.commit = commit;
    s.release = release_scene_edit_undo;
    return s;
}
static void notify(void *p, int value)
{
    Test *t = p;
    t->notify = value;
    event(t, NOTIFY);
}
static MMActorOwner *actor_owner(void *p, void *object)
{
    (void)p;
    return &((Actor *)object)->stage.owner;
}
static void *new_actor_snapshot(void *p)
{
    Test *t = p;
    event(t, DUP);
    if (t->fail == FAIL_DUP)
        return NULL;
    mm_actor_owner_init(&t->copy.stage.owner);
    t->copy.refs = 1;
    return &t->copy;
}
static void retain_actor_snapshot(void *p, void *object)
{
    (void)p;
    if (object)
        ++((Actor *)object)->refs;
}
static void release_actor_ref(void *p, void *object)
{
    (void)p;
    release(object);
}
static void stage_route(void *p, void *object, MMActorRouteEditServices *out)
{
    Actor *actor = object;
    const MMActorStageEditServices stage = stage_fixture_services(&actor->stage);
    (void)p;
    *out = stage.subroute.route;
}
static void restored_actor_views(void *p, void *scene)
{
    Test *t = p;
    (void)scene;
    ++t->restore;
    event(t, RESTORE);
}
static MMActorUndoState *new_actor_undo(void *p, void **handle)
{
    Test *t = p;
    event(t, MAKE_UNDO);
    if (t->fail == FAIL_UNDO)
        return NULL;
    t->undo.refs = 1;
    *handle = &t->undo;
    return mm_actor_undo_init(&t->undo.actor_state, &t->undo.base, 1) ? &t->undo.actor_state : NULL;
}
static void clear_actor_undo(void *p) { ++((Test *)p)->clears; }
static void release_actor_undo(void *p, void *object)
{
    Undo *u = object;
    if (!--u->refs)
        release_actor_ref(p, u->actor_state.actor);
}
static MMActorUndoCreateServices bind_actor_undo(void *p)
{
    Actor *actor = p;
    Test *t = actor->test;
    MMActorUndoCreateServices services;
    t->undoOnFresh = actor == &t->fresh;
    services.context = t;
    services.create = new_actor_undo;
    services.add = commit;
    services.clear = clear_actor_undo;
    services.release = release_actor_undo;
    return services;
}
static int seed_rollcall(Test *t)
{
    MMRollCallActor row;
    memset(&row, 0, sizeof(row));
    row.data.actor_id = 5;
    row.data.reference_count = 1;
    row.data.source_id = 2;
    row.data.template_type = row.data.template_number = 1;
    return mm_rollcall_table_init(&t->data.actors, sizeof(row), 1, 0x0303, 0, NULL) == MM_STORAGE_OK &&
           mm_rollcall_table_add(&t->data.actors, "Actor", 5, &row);
}
static MMI32 actor_id(void *p)
{
    return ((Actor *)p)->id;
}
static void set_id(void *p, MMI32 id)
{
    ((Actor *)p)->id = id;
}
static void actor_tag(void *p, MMTag *out)
{
    *out = ((Actor *)p)->tag;
}
static int same_tag(const MMTag *a, const MMTag *b)
{
    return a->sid == b->sid && a->cache == b->cache && a->type == b->type && a->number == b->number;
}
static void changed_row(void *p)
{
    Test *t = p;
    ++t->rowChanges;
    event(t, ROLL_CHANGED);
}
static void init(Test *t)
{
    MMMovieEditServices *s;
    memset(t, 0, sizeof(*t));
    t->actor.test = t->fresh.test = t->copy.test = t;
    t->actor.id = 5;
    t->actor.refs = 1;
    t->actor.tag.sid = 2;
    t->actor.tag.type = 1;
    t->actor.tag.number = 1;
    t->undo.test = t;
    t->undo.actor = t->undo.actor_state.sooner_later = 1;
    t->undo.base.frame = 77;
    t->selected = &t->actor;
    t->scenes.open = t;
    t->scenes.index = 2;
    t->data.sceneCount = 3;
    t->data.title.count = 4;
    t->view = t->inScene = 1;
    t->frame = 9;
    s = &t->s;
    s->context = t;
    s->data = &t->data;
    s->scenes = &t->scenes;
    s->rollcall.context = t;
    s->rollcall.data = &t->data;
    s->rollcall.actor_id = actor_id;
    s->rollcall.set_actor_id = set_id;
    s->rollcall.actor_tag = actor_tag;
    s->rollcall.same_tag = same_tag;
    s->rollcall.changed = changed_row;
    s->undo.context = t;
    s->undo.movie = t;
    s->undo.state = undo_state;
    s->undo.validate = undo_validate;
    s->undo.scene_index = scene_index;
    s->undo.frame = frame;
    s->undo.commit = commit;
    s->undo.clear = clear;
    s->undo.notify = notify;
    s->selected_actor = selected;
    s->actor_from_id = lookup;
    s->select_actor = select_actor;
    s->first_view = view;
    s->new_actor = new_actor;
    s->actor_visible = visible;
    s->stage = bind_stage;
    s->actors.context = t;
    s->actors.owner = actor_owner;
    s->actors.create = new_actor_snapshot;
    s->actors.retain = retain_actor_snapshot;
    s->actors.release = release_actor_ref;
    s->actors.route_services = stage_route;
    s->actors.invalidate_views = restored_actor_views;
    s->actor_undo = bind_actor_undo;
    s->transform = bind_transform;
    s->costume = bind_costume;
    s->actor_sound = bind_actor_sound;
    s->sound_external_child = 42;
    s->add_actor = add_actor;
    s->remove_actor = remove_actor;
    s->scene_edit = bind_scene_edit;
    s->scene_properties = bind_scene_properties;
    s->frame = frame;
    s->last_actor_undo = last_actor_undo;
    s->place_actor = place;
    s->placement_undo = placement;
    s->paste_actor = paste_actor;
    s->route_paste = bind_route_paste;
    s->long_op = long_op;
    s->cache_tag = cache;
    s->default_title = title;
    s->new_scene_undo = new_undo;
    s->configure_scene_undo = configure;
    s->release_undo = release_undo;
    s->sounds_enabled = sounds;
    s->play_sound = play;
    s->flush_sound = flush;
    s->fetch_sound = sound;
    s->sound_volume = volume;
    s->sound_style = style;
    s->release_sound = release_sound;
    s->dirty = dirty;
    s->invalidate = invalidate;
    s->actor_nuked = nuked;
    s->diagnostic = diagnostic;
}
static int registration(void)
{
    Test t;
    int current, fail;
    for (current = 0; current <= 1; ++current)
        for (fail = 0; fail <= 1; ++fail)
        {
            init(&t);
            if (!current)
                t.scenes.index = -1;
            if (fail)
                t.fail = FAIL_COMMIT;
            CHECK(mm_movie_undo_add(&t.undo, &t.s.undo) == !fail);
            CHECK(t.undo.bound && t.undo.base.scene == (current ? 2 : -1) && t.undo.base.frame == (current ? 9 : 77));
            CHECK(t.notify == (fail ? MM_MOVIE_UNDO_DISABLED : MM_MOVIE_UNDO_READY));
            CHECK(t.events[0] == UNDO_STATE && t.events[1] == UNDO_VALIDATE && t.events[t.eventCount - 2] == COMMIT &&
                  t.events[t.eventCount - 1] == NOTIFY);
            mm_movie_undo_clear(&t.s.undo);
            CHECK(t.clears == 1 && t.notify == MM_MOVIE_UNDO_DISABLED && !t.undo.refs &&
                  t.events[t.eventCount - 2] == CLEAR);
        }
    return 0;
}
static int choosing(void)
{
    Test t;
    MMRollCallActor row;
    int fault;
    init(&t);
    t.scenes.open = 0;
    CHECK(mm_movie_selected_actor(&t.s) == -1 && !t.selectedCalls);
    init(&t);
    t.selected = 0;
    CHECK(mm_movie_selected_actor(&t.s) == -1);
    init(&t);
    CHECK(mm_movie_selected_actor(&t.s) == 5);
    init(&t);
    t.view = 0;
    CHECK(!mm_movie_choose_actor(5, &t.s) && t.eventCount == 1 && t.events[0] == VIEW);
    init(&t);
    t.actor.visible = 1;
    CHECK(mm_movie_choose_actor(5, &t.s) && t.selectionCalls == 1 && t.invalidates == 1 && !t.placeCalls &&
          !t.dirty);
    for (fault = 0; fault <= 2; ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        CHECK(stage_fixture_init(&t.actor.stage, t.frame));
        t.fail = fault == 1 ? FAIL_DUP : fault == 2 ? FAIL_STAGE : 0;
        CHECK(mm_movie_choose_actor(5, &t.s) == !fault);
        CHECK(!t.dirty);
        if (fault)
            CHECK(!t.copy.refs && !t.selectionCalls && !t.placeCalls && !t.invalidates);
        else
        {
            CHECK(t.copy.refs == 1 && t.placement == &t.copy && t.selectionCalls == 2 && t.placeCalls == 1 &&
                  !t.entire);
            CHECK(t.copy.stage.owner.runtime.state.frame == 2147483647L &&
                  !mm_actor_events_count(t.copy.stage.owner.track.events));
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row) && row.data.reference_count == 2 &&
                  t.rowChanges == 1 && t.invalidates == 1);
            release(t.placement);
            CHECK(!t.copy.refs);
        }
        if (fault == 2)
            CHECK(t.actor.stage.entrances == 1 && mm_actor_events_count(t.actor.stage.owner.track.events) == 1);
        if (!fault)
            CHECK(t.actor.stage.owner.runtime.state.onStage && mm_actor_events_count(t.actor.stage.owner.track.events) == 4);
        stage_fixture_dispose(&t.actor.stage);
        CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    for (fault = 0; fault <= 2; ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        t.inScene = 0;
        t.fail = fault == 1 ? FAIL_NEW_ACTOR : fault == 2 ? FAIL_ADD : 0;
        CHECK(mm_movie_choose_actor(5, &t.s) == !fault);
        CHECK(t.fresh.refs == !fault && !t.dirty && !t.invalidates && t.placeCalls == !fault);
        if (!fault)
        {
            CHECK(t.fresh.id == 5);
            release(&t.fresh);
            CHECK(!t.fresh.refs);
        }
        CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    init(&t);
    CHECK(seed_rollcall(&t));
    t.inScene = 0;
    CHECK(!mm_movie_choose_actor(6, &t.s) && !t.fresh.refs && !t.placeCalls);
    CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    return 0;
}
static int seed_edit_track(Test *t)
{
    MMActorOwner *owner = &t->actor.stage.owner;
    MMActorRoutePoint point = {{0, 0, 0}, 0};
    MMActorEntrance entrance = {0};
    MMActorEvent event = {0};
    if (!stage_fixture_init(&t->actor.stage, t->frame))
        return 0;
    owner->runtime.actor = t->actor.id;
    owner->runtime.first = owner->runtime.last = t->frame;
    owner->runtime.state.cel = 6;
    owner->runtime.state.onStage = 1;
    owner->runtime.state.addEvent = 0;
    owner->runtime.state.firstEventInFrame = owner->runtime.state.nextEvent = 1;
    event.aet = MM_ACTOR_ADD;
    event.nfrm = t->frame;
    return mm_actor_path_insert(owner->track.route, 0, &point) &&
           mm_actor_events_insert(owner->track.events, 0, sizeof(entrance), &entrance, &event);
}
static int actor_edits(void)
{
    Test t;
    MMMovieEditRequest q;
    int kind, fail, type;
    memset(&q, 0, sizeof(q));
    q.factor = 49152;
    q.frame = 17;
    q.costume = 12;
    q.custom = 2;
    q.loop = q.queue = q.cel = 2;
    q.volume = -1;
    for (kind = MM_MOVIE_EDIT_ROTATE; kind <= MM_MOVIE_EDIT_REMOVE; ++kind)
        for (fail = 0; fail <= 1; ++fail)
        {
            MMActorEvent event;
            MMActorOwner *owner = &t.actor.stage.owner;
            const int has_undo = kind == MM_MOVIE_EDIT_COSTUME || kind == MM_MOVIE_EDIT_ACTOR_SOUND;
            init(&t);
            CHECK(seed_edit_track(&t));
            q.tag = &t.actor.tag;
            if (fail)
                t.fail = FAIL_APPLY;
            CHECK(mm_movie_edit_actor(kind, &q, &t.s) == !fail);
            CHECK(t.dirty == (!fail && kind != MM_MOVIE_EDIT_ACTOR_SOUND));
            CHECK(t.invalidates == t.dirty && t.nuked == (!fail && kind == MM_MOVIE_EDIT_REMOVE));
            if (t.invalidates)
                CHECK(t.bars == (kind == MM_MOVIE_EDIT_SCALE || kind == MM_MOVIE_EDIT_REMOVE));
            CHECK(t.restore == (has_undo && fail) && t.commits == (has_undo && !fail));
            CHECK(owner->runtime.state.cel == 6);
            if (kind == MM_MOVIE_EDIT_TIME)
            {
                CHECK(t.events[0] == FRAME && t.events[1] == SELECT && t.lastUndoCalls == !fail);
                CHECK(t.actorSeeks == 1 && t.actorSeekFrame == 17 && owner->runtime.state.frame == 17);
                CHECK(mm_actor_events_get_fixed(owner->track.events, 0, &event) && event.nfrm == 17);
            }
            else if (fail || kind == MM_MOVIE_EDIT_REMOVE)
                CHECK(mm_actor_events_count(owner->track.events) == 1);
            else
            {
                CHECK(mm_actor_events_count(owner->track.events) == 2 &&
                      mm_actor_events_get_fixed(owner->track.events, 1, &event));
                if (kind == MM_MOVIE_EDIT_ROTATE)
                    CHECK(event.aet == MM_ACTOR_ROTATE_HERE);
                if (kind == MM_MOVIE_EDIT_SQUASH)
                {
                    MMActorStretch stretch;
                    CHECK(event.aet == MM_ACTOR_STRETCH &&
                          mm_actor_events_get_payload(owner->track.events, 1, &stretch));
                    CHECK(stretch.rScaleX == 49152 && stretch.rScaleY == 87381 && stretch.rScaleZ == 49152);
                }
                if (kind == MM_MOVIE_EDIT_SCALE)
                    CHECK(event.aet == MM_ACTOR_SIZE && owner->runtime.state.transform.rScaleStep == 49152);
                if (kind == MM_MOVIE_EDIT_COSTUME)
                {
                    MMActorCostume costume;
                    CHECK(event.aet == MM_ACTOR_COSTUME &&
                          mm_actor_events_get_payload(owner->track.events, 1, &costume));
                    CHECK(costume.custom == 2 && costume.costume == 12 && costume.body_set == 3);
                }
                if (kind == MM_MOVIE_EDIT_ACTOR_SOUND)
                {
                    MMActorSound sound;
                    CHECK(event.aet == MM_ACTOR_SOUND &&
                          mm_actor_events_get_payload(owner->track.events, 1, &sound));
                    CHECK(sound.loop == 2 && sound.queue == 2 && sound.cel == 2 && sound.child == 42);
                    CHECK(sound.volume == 73 && sound.style == 2 && t.actorSoundFrames == 1);
                }
            }
            if (has_undo && !fail)
            {
                CHECK(t.undo.actor_state.actor == &t.copy && t.undo.actor_state.actor_id == t.actor.id &&
                      t.undo.actor_state.sound == (kind == MM_MOVIE_EDIT_ACTOR_SOUND));
                CHECK(mm_actor_events_count(t.copy.stage.owner.track.events) == 1);
                release_actor_undo(&t, &t.undo);
            }
            CHECK(!t.undo.refs && !t.copy.refs);
            stage_fixture_dispose(&t.actor.stage);
        }
    init(&t);
    q.factor = 0;
    CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_SQUASH, &q, &t.s) && !t.actorBindings && !t.dirty &&
          t.selectedCalls == 1);
    for (type = 0; type < 8; ++type)
    {
        init(&t);
        t.undoCount = (type & 4) != 0;
        t.undo.actor = type & 1;
        t.undo.actor_state.sooner_later = (type & 2) != 0;
        CHECK(seed_edit_track(&t));
        CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &q, &t.s));
        CHECK(t.lastUndoCalls == 1 && t.undo.actor_state.last_frame == (type == 7 ? 17 : 0));
        stage_fixture_dispose(&t.actor.stage);
    }
    init(&t);
    t.frame = 2147483647;
    q.frame = (-2147483647 - 1);
    CHECK(seed_edit_track(&t));
    CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &q, &t.s) && t.actorSeekFrame == q.frame);
    CHECK(t.actor.stage.owner.runtime.first == q.frame);
    CHECK(mm_movie_step_actor_time(1, &t.s) && t.actorSeekFrame == q.frame);
    CHECK(t.actor.stage.owner.runtime.first == q.frame + 1);
    t.frame = q.frame;
    CHECK(mm_movie_step_actor_time(-1, &t.s) && t.actorSeekFrame == 2147483647);
    CHECK(t.actor.stage.owner.runtime.first == q.frame);
    stage_fixture_dispose(&t.actor.stage);
    init(&t);
    t.selected = NULL;
    CHECK(!mm_movie_step_actor_time(1, &t.s) && t.selectedCalls == 1 && !t.actorBindings && !t.lastUndoCalls && !t.dirty);
    t.scenes.open = NULL;
    CHECK(!mm_movie_step_actor_time(-1, &t.s) && t.eventCount == 2);
    init(&t);
    t.selected = 0;
    CHECK(!mm_movie_onstage(-1, &t.s) && t.error == MM_MOVIE_EDIT_NO_ACTOR && !t.selectionCalls);
    init(&t);
    CHECK(!mm_movie_onstage(99, &t.s) && !t.selected && t.selectionCalls == 1 &&
          t.error == MM_MOVIE_EDIT_NO_ACTOR);
    for (type = 0; type < 3; ++type)
    {
        init(&t);
        CHECK(stage_fixture_init(&t.actor.stage, t.frame));
        t.fail = type == 1 ? FAIL_STAGE : type == 2 ? FAIL_COMMIT : 0;
        CHECK(mm_movie_onstage(5, &t.s) == !type && t.selectionCalls == 1 && t.actor.stage.entrances == 1);
        CHECK(t.dirty == !type && t.invalidates == !type && !t.bars && t.commits == (type != 1));
        CHECK(t.actor.stage.owner.runtime.state.onStage == !type);
        CHECK(mm_actor_events_count(t.actor.stage.owner.track.events) == (type ? 0 : 4));
        if (!type)
        {
            CHECK(t.undo.refs == 1 && t.copy.refs == 1 && t.undo.actor_state.actor == &t.copy);
            CHECK(t.copy.stage.owner.runtime.state.frame == 2147483647L && t.undo.actor_state.actor_id == 5);
            release_actor_undo(&t, &t.undo);
        }
        CHECK(t.clears == (type == 2) && !t.copy.refs && !t.undo.refs);
        stage_fixture_dispose(&t.actor.stage);
    }
    return 0;
}
static void cursor(void *p) { event(p, CURSOR); }
static void size_sound(void *p, int alternate) { event(p, alternate ? SIZE_SOUND_ALTERNATE : SIZE_SOUND); }
static void world_dirty(void *p) { event(p, WORLD_DIRTY); }
static void mark_views(void *p) { event(p, MARK_VIEWS); }
static void drag_release(void *p, void *handle) { (void)p; release_undo(handle); }

static int actor_drag(void)
{
    static const int rotate_steps[] = {SELECT, APPLY, DIRTY, INVALIDATE, COMMIT, UNDO_RELEASE, WORLD_DIRTY, MARK_VIEWS, CURSOR};
    static const int failure_steps[] = {SELECT, APPLY, WORLD_DIRTY, MARK_VIEWS, CURSOR};
    Test t;
    MMActorDragUndoServices undo = {0};
    MMSceneSelectionServices selection = {0};
    MMSceneOwner owner = {0};
    MMMovieActorDrag drag;
    MMMovieActorDragInput input = {0};
    MMMovieActorDragServices services = {0};
    int axis, kind, tick;
    undo.context = &t;
    undo.commit = commit;
    undo.release = drag_release;
    selection.owner = &owner;
    services.selection = &selection;
    services.movie = &t.s;
    services.undo = &undo;
    services.view = &t;
    services.adjust_cursor = cursor;
    services.size_sound = size_sound;
    services.world_dirty = world_dirty;
    services.mark_views = mark_views;
    owner.selected_actor = &t.actor;
    for (axis = 0; axis < 3; ++axis)
    {
        MMActorEvent event;
        MMActorMatrix actual, expected;
        const unsigned short angle = axis == 1 ? 130 : 65406;
        const int axes[] = {MM_MATRIX_X, MM_MATRIX_Y, MM_MATRIX_Z};
        init(&t);
        CHECK(seed_edit_track(&t));
        memset(&drag, 0, sizeof(drag));
        drag.undo.single = &t.undo;
        t.undo.refs = 1;
        drag.undo.group = &t.copy;
        input.tool = MM_MOVIE_DRAG_ROTATE_X + axis;
        input.command = axis != 2;
        input.mouse.dxr = 3 * 65536L;
        input.mouse.dyr = -65536L;
        mm_matrix_identity(&expected);
        mm_matrix_rotate(&expected, axes[axis], angle, 1);
        mm_movie_actor_drag_sample(&drag, &input, &services);
        CHECK(mm_actor_events_get_fixed(t.actor.stage.owner.track.events, 1, &event) &&
              event.aet == (input.command ? MM_ACTOR_ROTATE_FORWARD : MM_ACTOR_ROTATE_HERE));
        CHECK(mm_actor_events_get_payload(t.actor.stage.owner.track.events, 1, &actual) &&
              !memcmp(&actual, &expected, sizeof(actual)));
        CHECK(!drag.undo.single && drag.undo.group == &t.copy && t.undo.refs == 1);
        CHECK(t.eventCount == 9 && !memcmp(t.events, rotate_steps, sizeof(rotate_steps)));
        stage_fixture_dispose(&t.actor.stage);
    }

    init(&t);
    CHECK(seed_edit_track(&t));
    memset(&drag, 0, sizeof(drag));
    drag.undo.single = &t.undo;
    t.fail = FAIL_APPLY;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(drag.undo.single == &t.undo && t.eventCount == 5 && !memcmp(t.events, failure_steps, sizeof(failure_steps)));
    CHECK(mm_actor_events_count(t.actor.stage.owner.track.events) == 1);
    stage_fixture_dispose(&t.actor.stage);

    for (kind = MM_MOVIE_DRAG_RESIZE; kind <= MM_MOVIE_DRAG_SQUASH; ++kind)
    {
        const int squash = kind == MM_MOVIE_DRAG_SQUASH;
        init(&t);
        memset(&drag, 0, sizeof(drag));
        input.tool = kind;
        input.mouse.dyr = 0;
        for (tick = 0; tick < 5; ++tick)
        {
            MMActorEvent event;
            MMActorState *state = &t.actor.stage.owner.runtime.state;
            int pixels = tick < 2 ? 1 : tick == 2 ? 0 : -1;
            MMI32 factor = 65536L + (squash ? -65 : 65) * pixels;
            CHECK(seed_edit_track(&t));
            t.eventCount = 0;
            input.mouse.dxr = pixels * 65536L;
            mm_movie_actor_drag_sample(&drag, &input, &services);
            CHECK(mm_actor_events_get_fixed(t.actor.stage.owner.track.events, 1, &event) &&
                  event.aet == (squash ? MM_ACTOR_STRETCH : MM_ACTOR_SIZE) && t.bars == !squash);
            CHECK((squash ? state->transform.aevpull.rScaleX : state->transform.rScaleStep) == factor);
            CHECK(t.events[0] == (tick == 0 ? SIZE_SOUND : tick == 3 ? SIZE_SOUND_ALTERNATE : SELECT));
            CHECK(t.events[t.eventCount - 1] == CURSOR);
            stage_fixture_dispose(&t.actor.stage);
        }
    }

    init(&t);
    memset(&drag, 0, sizeof(drag));
    owner.selected_actor = NULL;
    input.tool = MM_MOVIE_DRAG_COMPOSE;
    input.command = 0;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(!t.eventCount);
    input.command = 1;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(t.eventCount == 2 && t.events[0] == CURSOR && t.events[1] == MARK_VIEWS);
    return 0;
}

static int seed_scene(Test *t)
{
    if (!mm_scene_owner_init(&t->scene_owner, t, 1, NULL))
        return 0;
    t->scene_owner.owned[MM_SCENE_BACKGROUND] = t;
    t->scene_owner.data.transition = 1;
    return 1;
}
static void dispose_scene(Test *t)
{
    const MMSceneOwnerServices owners = scene_owners(t);
    if (t->undo.refs)
        release_scene_edit_undo(t, &t->undo);
    mm_scene_owner_dispose(&t->scene_owner, &owners);
}
static int scene_edits(void)
{
    Test t;
    MMMovieEditRequest q;
    MMTag tag = {2, NULL, 0, 5};
    MMSceneTrack *track = &t.scene_owner.tracks[MM_SCENE_FRAMES];
    const MMSceneSound *packet;
    MMSceneSoundTag record;
    int kind, fail, missing;
    memset(&q, 0, sizeof(q));
    q.camera = 2;
    q.reason = 1;
    q.delay = 40;
    q.transition = 3;
    q.tag = &tag;
    q.volume = 101;
    q.style = 3;
    q.loop = 2;
    for (kind = MM_MOVIE_EDIT_CAMERA; kind <= MM_MOVIE_EDIT_BACKGROUND_SOUND; ++kind)
        for (fail = 0; fail <= 1; ++fail)
        {
            MMSceneTrackEvent event;
            init(&t);
            CHECK(seed_scene(&t));
            if (fail)
                t.fail = kind == MM_MOVIE_EDIT_CAMERA ? FAIL_CACHE :
                         kind == MM_MOVIE_EDIT_BACKGROUND_SOUND ? FAIL_APPLY : FAIL_COMMIT;
            CHECK(mm_movie_edit_scene(kind, &q, &t.s) == !fail);
            CHECK(t.dirty == (!fail && kind != MM_MOVIE_EDIT_BACKGROUND_SOUND));
            CHECK(t.invalidates == (!fail && kind == MM_MOVIE_EDIT_CAMERA));
            CHECK(t.undo.refs == !fail);
            CHECK(track->rows.count == (MMU32)(!fail && kind != MM_MOVIE_EDIT_TRANSITION));
            CHECK(t.scene_owner.runtime.next_event == (MMI32)track->rows.count);
            if (kind == MM_MOVIE_EDIT_CAMERA)
            {
                CHECK(t.camera == (fail ? 0 : 2) && t.cameraCloses == 1);
                CHECK(fail || (t.undo.background.camera == 0 && t.sceneDirty == 1 &&
                               mm_scene_track_get(track, 0, &event) && event.kind == MM_SCENE_CHANGE_CAMERA));
            }
            if (kind == MM_MOVIE_EDIT_PAUSE)
            {
                CHECK(t.sceneDirty == 1 && t.undo.pause.reason == 0 && t.undo.pause.delay == 0);
                if (!fail)
                {
                    MMI32 values[2];
                    memcpy(values, mm_scene_track_payload(track, 0, NULL), sizeof(values));
                    CHECK(mm_scene_track_get(track, 0, &event) && event.kind == MM_SCENE_PAUSE &&
                          values[0] == q.reason && values[1] == q.delay);
                }
            }
            if (kind == MM_MOVIE_EDIT_TRANSITION)
                CHECK(t.scene_owner.data.transition == (fail ? 1 : 3) && t.undo.transition.transition == 1);
            if (kind == MM_MOVIE_EDIT_BACKGROUND_SOUND)
            {
                CHECK(t.clears == fail && t.sceneSoundFrames == !fail);
                if (!fail)
                {
                    packet = mm_scene_track_payload(track, 0, NULL);
                    CHECK(mm_scene_track_get(track, 0, &event) && event.kind == MM_SCENE_PLAY_SOUND &&
                          packet->fLoop == 1 && t.undo.sound.style == q.style);
                    mm_scene_sound_get_tag(packet, 0, &record);
                    CHECK(record.child == 29 && same_tag(&record.tag, &tag));
                }
            }
            dispose_scene(&t);
        }
    q.loop = 0;
    q.queue = 2;
    for (missing = 0; missing < 4; ++missing)
    {
        init(&t);
        CHECK(seed_scene(&t));
        q.volume = (missing & 1) ? -1 : 101;
        q.style = (missing & 2) ? 0 : 3;
        CHECK(mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s));
        packet = mm_scene_track_payload(track, 0, NULL);
        CHECK(packet->vlm == ((missing & 1) ? 83 : 101) && packet->sty == ((missing & 2) ? 4 : 3));
        CHECK(t.volumeCalls == !!(missing & 1) && t.styleCalls == !!(missing & 2) && !t.soundRefs && !t.dirty);
        CHECK(q.volume == ((missing & 1) ? -1 : 101) && q.style == ((missing & 2) ? 0 : 3));
        if (missing)
        {
            int release_index = 1 + !!(missing & 1) + !!(missing & 2);
            CHECK(t.events[release_index] == SOUND_RELEASE && t.events[release_index + 1] == APPLY);
        }
        dispose_scene(&t);
    }
    init(&t);
    CHECK(seed_scene(&t));
    tag.sid = 0;
    q.volume = 101;
    q.style = 3;
    CHECK(mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s));
    CHECK(t.sceneInputNumber == 12 && tag.number == 5);
    release_scene_edit_undo(&t, &t.undo);
    tag.number = 6;
    CHECK(mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s));
    packet = mm_scene_track_payload(track, 0, NULL);
    CHECK(packet->ctagc == 2 && t.undo.sound.sound && t.undo.sound.sound->ctagc == 1);
    CHECK(t.sceneInputNumber == 13 && tag.number == 6);
    dispose_scene(&t);
    init(&t);
    t.fail = FAIL_SOUND;
    q.volume = -1;
    CHECK(!mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s) && !t.sceneBindings && !t.soundRefs &&
          !t.dirty);
    return 0;
}
static int seed_route(Actor *actor, MMI32 frame, int clipboard)
{
    MMActorRoutePoint point = {{0, 0, 0}, 0};
    MMActorEvent event = {0};
    const MMI32 action[] = {0, 0};
    MMActorOwner *owner = &actor->stage.owner;
    if (!stage_fixture_init(&actor->stage, frame))
        return 0;
    if (clipboard)
        point.dwr = 65536L;
    if (!mm_actor_path_insert(owner->track.route, 0, &point))
        return 0;
    if (clipboard)
    {
        point.xyz.dxr = 65536L;
        point.dwr = 0;
        return mm_actor_path_insert(owner->track.route, 1, &point);
    }
    owner->runtime.actor = actor->id;
    owner->runtime.state.actionEvent = 0;
    owner->runtime.state.nextEvent = 1;
    event.aet = MM_ACTOR_ACTION;
    event.nfrm = frame;
    return mm_actor_events_insert(owner->track.events, 0, sizeof(action), action, &event);
}
static int pasting(void)
{
    Test t;
    int fault;
    const int failures[] = {0, FAIL_DUP, FAIL_PASTE, FAIL_UNDO, FAIL_COMMIT};
    for (fault = 0; fault < (int)(sizeof(failures) / sizeof(failures[0])); ++fault)
    {
        int edited = !fault || failures[fault] == FAIL_PASTE;
        init(&t);
        CHECK(seed_route(&t.actor, t.frame, 0) && seed_route(&t.fresh, t.frame, 1));
        t.fail = failures[fault];
        CHECK(mm_movie_paste_route(&t.fresh, &t.s) == !fault);
        CHECK(t.copy.refs == !fault && t.undo.refs == !fault && t.dirty == !fault && t.invalidates == !fault);
        CHECK(mm_actor_path_count(t.actor.stage.owner.track.route) == 1 + edited);
        CHECK(mm_actor_events_count(t.actor.stage.owner.track.events) == (fault ? 1 : 4));
        CHECK(mm_actor_path_count(t.fresh.stage.owner.track.route) == 2 &&
              !mm_actor_events_count(t.fresh.stage.owner.track.events));
        CHECK(t.restore == (fault >= 3) && t.clears == (t.fail == FAIL_COMMIT));
        if (fault >= 3)
            CHECK(t.events[t.eventCount - 2] == RESTORE && t.events[t.eventCount - 1] == RELEASE);
        if (!fault)
        {
            CHECK(t.undo.actor_state.actor == &t.copy && t.undo.actor_state.actor_id == t.actor.id);
            CHECK(mm_actor_path_count(t.copy.stage.owner.track.route) == 1 &&
                  mm_actor_events_count(t.copy.stage.owner.track.events) == 1 &&
                  t.copy.stage.owner.runtime.state.frame == 2147483647L);
            CHECK(t.events[t.eventCount - 2] == INVALIDATE && t.events[t.eventCount - 1] == RELEASE);
            release_actor_undo(&t, &t.undo);
        }
        CHECK(!t.copy.refs && !t.undo.refs);
        stage_fixture_dispose(&t.fresh.stage);
        stage_fixture_dispose(&t.actor.stage);
    }
    init(&t);
    t.selected = 0;
    CHECK(!mm_movie_paste_route(&t.fresh, &t.s) && t.error == MM_MOVIE_EDIT_NO_ACTOR);
    init(&t);
    t.view = 0;
    CHECK(!mm_movie_paste_actor(&t.actor, &t.s) && !t.selectionCalls && !t.dirty);
    init(&t);
    t.fail = FAIL_PASTE;
    CHECK(!mm_movie_paste_actor(&t.actor, &t.s) && !t.selectionCalls && !t.placeCalls);
    init(&t);
    CHECK(mm_movie_paste_actor(&t.actor, &t.s) && t.selectionCalls == 1 && t.entire && t.dirty && t.bars &&
          t.actor.refs == 1);
    init(&t);
    t.switchSelection = 1;
    t.fail = FAIL_COMMIT;
    CHECK(seed_route(&t.actor, t.frame, 0) && seed_route(&t.fresh, t.frame, 1));
    t.fresh.stage.owner.runtime.actor = 6;
    t.fresh.stage.owner.runtime.state.cel = 123;
    CHECK(!mm_movie_paste_route(&t.fresh, &t.s) && t.undoOnFresh && t.undo.actor_state.actor_id == 6 &&
          t.selectedCalls == 4 && !t.copy.refs && !t.dirty);
    CHECK(mm_actor_path_count(t.actor.stage.owner.track.route) == 2 &&
          mm_actor_events_count(t.actor.stage.owner.track.events) == 4);
    CHECK(t.fresh.stage.owner.runtime.state.cel == 0 && mm_actor_path_count(t.fresh.stage.owner.track.route) == 1 &&
          mm_actor_events_count(t.fresh.stage.owner.track.events) == 1);
    stage_fixture_dispose(&t.fresh.stage);
    stage_fixture_dispose(&t.actor.stage);
    return 0;
}

typedef struct ClipboardTest
{
    MMMovieClipboardServices services;
    StageOwnerFixture stage;
    int has_scene, text_mode, route_only, formats, refs, paste_refs, text_success;
    int pasted, enabled_text, tool, text_queued, generic, diagnosis, cues;
    int sounds, dirty, invalidated;
} ClipboardTest;

static void *clip_scene(void *p)
{
    return ((ClipboardTest *)p)->has_scene ? p : 0;
}
static int clip_text_mode(void *p)
{
    return ((ClipboardTest *)p)->text_mode;
}
static void *clip_selected(void *p)
{
    return p;
}
static int clip_yes(void *p)
{
    return p != 0;
}
static int clip_empty(void *p)
{
    return !((ClipboardTest *)p)->formats;
}
static int clip_format(void *p, int kind, void **document)
{
    ClipboardTest *t = p;
    if (!(t->formats & (1 << kind)))
        return 0;
    if (document)
    {
        *document = p;
        ++t->refs;
    }
    return 1;
}
static int clip_route(void *p)
{
    return ((ClipboardTest *)p)->route_only;
}
static void clip_release(void *p)
{
    --((ClipboardTest *)p)->refs;
}
static void clip_paste_actor(void *p, void *document)
{
    ClipboardTest *t = document;
    t->paste_refs = t->refs;
    ((ClipboardTest *)p)->pasted = 1 << MM_MOVIE_CLIP_ACTOR;
}
static int clip_paste_text(void *p, void *document)
{
    ClipboardTest *t = document;
    t->paste_refs = t->refs;
    ((ClipboardTest *)p)->pasted = 1 << MM_MOVIE_CLIP_TEXT_BOX;
    return t->text_success;
}
static void clip_enable_text(void *p)
{
    ++((ClipboardTest *)p)->enabled_text;
}
static void clip_set_tool(void *p, int tool)
{
    ((ClipboardTest *)p)->tool = tool;
}
static void clip_cue(void *p, int tool)
{
    if (tool == MM_MOVIE_CLIP_PASTE)
        ++((ClipboardTest *)p)->cues;
}
static void clip_queue_text(void *p, void *text)
{
    ((ClipboardTest *)p)->text_queued += (text == p);
}
static void clip_queue_generic(void *p, int command)
{
    ((ClipboardTest *)p)->generic = command;
}
static void clip_diagnose(void *p, int code)
{
    ((ClipboardTest *)p)->diagnosis = code;
}
static int clip_no_shift(void *p) { (void)p; return 0; }
static void *clip_copy(void *p, int route, int through_end)
{
    ClipboardTest *t = p;
    if (route || through_end)
        return NULL;
    ++t->refs;
    return p;
}
static void clip_publish(void *p, void *document)
{
    ClipboardTest *t = document;
    (void)p;
    ++t->refs;
}
static int clip_sounds_enabled(void *p) { return ((ClipboardTest *)p)->sounds; }
static void clip_sounds(void *p, int value) { ((ClipboardTest *)p)->sounds = value; }
static int clip_delete_survivor(void *p, int through_end, int *alive)
{
    ClipboardTest *t = p;
    *alive = 1;
    return !t->sounds && !through_end;
}
static MMActorStageEditServices clip_stage(void *p)
{
    ClipboardTest *t = p;
    return stage_fixture_services(&t->stage);
}
static void clip_dirty(void *p) { ++((ClipboardTest *)p)->dirty; }
static void clip_invalidate(void *p) { ++((ClipboardTest *)p)->invalidated; }
static void clip_init(ClipboardTest *t)
{
    MMMovieClipboardServices *s = &t->services;
    memset(t, 0, sizeof(*t));
    t->has_scene = 1;
    t->diagnosis = -1;
    s->context = t;
    s->scene = clip_scene;
    s->text_mode = clip_text_mode;
    s->selected_text = s->selected_actor = clip_selected;
    s->text_visible = s->text_selected = s->actor_visible = clip_yes;
    s->empty = clip_empty;
    s->format = clip_format;
    s->route_only = clip_route;
    s->release = clip_release;
    s->shift = clip_no_shift;
    s->copy_actor = clip_copy;
    s->publish = clip_publish;
    s->scene_sounds_enabled = clip_sounds_enabled;
    s->scene_sounds = clip_sounds;
    s->delete_actor = clip_delete_survivor;
    s->stage = clip_stage;
    s->dirty = clip_dirty;
    s->invalidate = clip_invalidate;
    s->paste_actor = clip_paste_actor;
    s->paste_text = clip_paste_text;
    s->enable_text = clip_enable_text;
    s->cue = clip_cue;
    s->set_tool = clip_set_tool;
    s->queue_text = clip_queue_text;
    s->queue_generic = clip_queue_generic;
    s->diagnostic = clip_diagnose;
}
static int clipboard(void)
{
    ClipboardTest t;
    const int actor = 1 << MM_MOVIE_CLIP_ACTOR, text_box = 1 << MM_MOVIE_CLIP_TEXT_BOX;

    clip_init(&t);
    t.formats = actor;
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 1, &t.services);
    CHECK(!t.refs && !t.pasted && !t.cues);
    t.route_only = 1;
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 0, &t.services);
    CHECK(!t.refs && !t.pasted && !t.cues);
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 1, &t.services);
    CHECK(!t.refs && t.pasted == actor && t.paste_refs == 2 && t.cues == 1);

    clip_init(&t);
    t.formats = actor;
    t.has_scene = 0;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.cues == 1 && t.diagnosis == MM_MOVIE_CLIP_NO_SCENE);
    t.formats = text_box;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.cues == 1);

    clip_init(&t);
    t.text_mode = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_COPY_TOOL, &t.services);
    CHECK(t.text_queued == 1 && t.generic == MM_MOVIE_CLIP_COPY && !t.tool);
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.text_queued == 2 && t.generic == MM_MOVIE_CLIP_PASTE && !t.tool);
    t.formats = actor;
    t.route_only = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.tool == MM_MOVIE_CLIP_PASTE && t.text_queued == 2 && !t.refs && !t.pasted);
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.diagnosis == MM_MOVIE_CLIP_UNSUPPORTED && t.text_queued == 3);
    t.route_only = 0;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.pasted == actor && t.text_queued == 3 && t.paste_refs == 1 && !t.refs);
    t.formats = actor | text_box;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.pasted == text_box && !t.enabled_text && t.cues == 3 && !t.refs);
    t.text_success = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(t.enabled_text == 1 && t.cues == 4 && !t.refs);

    clip_init(&t);
    CHECK(stage_fixture_init(&t.stage, 1));
    CHECK(mm_actor_stage_apply(&t, 1, t.services.stage));
    t.stage.owner.runtime.state.frame = 2;
    t.sounds = 1;
    mm_movie_clipboard_click(MM_MOVIE_CLIP_CUT, 1, &t.services);
    CHECK(t.sounds && t.stage.exits == 1 && !t.stage.owner.runtime.state.onStage);
    CHECK(t.dirty == 1 && t.invalidated == 1 && t.refs == 1);
    clip_release(&t);
    stage_fixture_dispose(&t.stage);
    return 0;
}

static int insertion(void)
{
    Test t;
    MMTag tag;
    int fault;
    memset(&tag, 0, sizeof(tag));
    for (fault = 0; fault <= 3; ++fault)
    {
        init(&t);
        t.fail = fault == 1 ? FAIL_CACHE : fault == 2 ? FAIL_NEW_ACTOR : fault == 3 ? FAIL_ADD : 0;
        CHECK(mm_movie_insert_actor(&tag, &t.s) == !fault);
        CHECK(t.begin == 1 && t.end == 1 && t.events[0] == BEGIN && t.events[1] == CACHE && t.events[2] == END &&
              !t.cacheBackground);
        CHECK(t.fresh.refs == !fault && t.dirty == !fault && t.invalidates == !fault);
        if (!fault)
            release(&t.fresh);
    }
    return 0;
}
static int allow(void *p) { (void)p; return 1; }
static void *background_file(void *p) { return p; }
static MMChunkAccess background_chunks(void *p) { return fixture_scene_chunks(&((Test *)p)->archive); }
static int write_background_scene(void *p, void *scene, MMU32 *number)
{
    Test *t = p;
    (void)scene;
    *number = 123;
    fixture_scene_add(&t->archive, MM_SCENE_CHUNK, *number);
    return 1;
}
static int adopt_background_scene(void *p, MMU32 movie, const MMChunkChild *child)
{
    Test *t = p;
    if (movie != FIXTURE_MOVIE_ROOT || child->type != MM_SCENE_CHUNK ||
        child->number != 123 || child->slot != (MMU32)t->data.sceneCount - 1)
        abort();
    return 1;
}
static MMChunkEdit edit_background_movie(void *p)
{
    Test *t = p;
    return fixture_scene_edit(&t->archive);
}
static MMMovieSceneServices background_scenes(Test *t)
{
    MMMovieSceneServices s = {0};
    s.context = t;
    s.state = &t->scenes;
    s.movie = &t->data;
    s.autosave = s.make_resources = s.use_temporary = allow;
    s.stop_loops = s.memory_cleanup = ignore_change;
    s.create = create_background_scene;
    s.open_session = open_background_movie;
    s.read = read_background_scene;
    s.start = s.seek_first = allow;
    s.close = s.retain = s.release = ignore_change;
    s.sound_frame = s.scene_change = s.invalidate = ignore_change;
    s.file = background_file;
    s.chunks = background_chunks;
    s.edit = edit_background_movie;
    s.write = write_background_scene;
    s.save = s.remove_actors = s.add_actors = ignore_change;
    s.dirty = s.rollcall_changed = s.flush_sound = ignore_change;
    s.sounds_enabled = cannot_notify;
    return s;
}
static int seed_background(Test *t)
{
    MMTag tag = {2, NULL, 4, 11};
    MMSceneTrackEvent event;
    event.frame = 0;
    event.kind = MM_SCENE_SET_BACKGROUND;
    t->data.resources = t;
    t->data.movieNumber = FIXTURE_MOVIE_ROOT;
    fixture_scene_archive_init(&t->archive, t->data.sceneCount);
    t->archive.observer = t;
    t->archive.before_insert = adopt_background_scene;
    t->archive.before_unlink = delete_background_scene;
    if (!seed_scene(t))
        return 0;
    t->scene_owner.background_tag = tag;
    return mm_scene_track_insert(&t->scene_owner.tracks[MM_SCENE_STARTS], 0, sizeof(tag), &tag, &event);
}
static int backgrounds(void)
{
    Test t;
    MMTag tag = {2, NULL, 4, 5};
    const MMMovieSceneServices scenes = background_scenes(&t);
    int fault, enabled;
    for (enabled = 0; enabled <= 1; ++enabled)
        for (fault = 0; fault < 6; ++fault)
        {
            init(&t);
            CHECK(seed_background(&t));
            t.scene_owner.data.lastFrame = t.scene_owner.data.firstFrame + 1;
            t.soundEnabled = enabled;
            t.fail = fault == 1   ? FAIL_CACHE
                     : fault == 2 ? FAIL_NEW_SCENE
                     : fault == 3 ? FAIL_BACKGROUND
                     : fault == 4 ? FAIL_NEW_UNDO
                     : fault == 5 ? FAIL_COMMIT
                                  : 0;
            CHECK(mm_movie_add_background(&tag, &t.s, &scenes) == !fault);
            CHECK(t.begin == 1 && t.end == 1 && t.cacheBackground && t.dirty == !fault && t.invalidates == !fault);
            CHECK(t.newScene == (fault != 1) && t.removeScene == (fault >= 3));
            CHECK(t.data.sceneCount == (fault ? 3 : 4) && t.scenes.index == (fault ? 2 : 3));
            CHECK(t.played == (enabled && (fault == 0 || fault >= 4)) &&
                  t.flushed == (!enabled && (fault == 0 || fault >= 4)));
            CHECK(t.undo.refs == !fault && !t.clears);
            if (fault >= 3)
                CHECK(t.removedIndex == 3);
            if (fault == 5)
                CHECK(t.notify == MM_MOVIE_UNDO_DISABLED && t.events[t.eventCount - 2] == UNDO_RELEASE &&
                      t.events[t.eventCount - 1] == REMOVE_SCENE);
            if (!fault)
                CHECK(t.undo.base.scene == 3 && t.undo.base.frame == 9 && t.bars &&
                      t.scene_owner.background_tag.number == tag.number);
            if (t.scenes.open)
                dispose_scene(&t);
            mm_movie_session_close(t.data.session);
        }
    init(&t);
    t.scenes.open = 0;
    t.scenes.index = -1;
    t.data.title.count = 0;
    t.fail = FAIL_NEW_SCENE;
    CHECK(!mm_movie_add_background(&tag, &t.s, &scenes) && t.defaults == 1 && t.data.title.count == 7 &&
          !t.removeScene);
    for (fault = 0; fault < 4; ++fault)
    {
        init(&t);
        CHECK(seed_background(&t));
        t.camera = 2;
        t.fail = fault == 1 ? FAIL_BACKGROUND : fault == 2 ? FAIL_NEW_UNDO : fault == 3 ? FAIL_COMMIT : 0;
        CHECK(mm_movie_add_background(&tag, &t.s, &scenes) == !fault);
        CHECK(!t.newScene && !t.removeScene && !t.played && t.dirty == !fault && t.undo.refs == !fault);
        CHECK(t.scene_owner.tracks[MM_SCENE_STARTS].rows.count == 1);
        CHECK(t.scene_owner.background_tag.number == (fault ? 11 : tag.number));
        if (fault)
            CHECK(t.error == MM_MOVIE_EDIT_BACKGROUND_FAILED && !t.clears);
        else
            CHECK(t.undo.background.background && t.undo.background.tag.number == 11 &&
                  t.undo.background.camera == 2);
        dispose_scene(&t);
    }
    return 0;
}
int main(void)
{
    if (registration() || choosing() || actor_edits() || actor_drag() || scene_edits() || pasting() || clipboard() ||
        insertion() || backgrounds())
        return 1;
    puts("Movie actor/scene edits, placement, sound defaults, clipboard and undo control passed");
    return 0;
}
