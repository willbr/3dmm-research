#include "mm/preview_actor.h"
#include "mm/actor_persistence.h"
#include "mm/body_runtime.h"
#include "mm/material_resource.h"
#include "mm/preview_camera.h"
#include "mm/preview_metadata.h"
#include "mm/template_create.h"
#include "mm/text_resource.h"
#include "mm/wide.h"
#include <string.h>

static int seek_actor(MMPreviewActor *, MMI32, const MMPreviewActorServices *);

MMActorTrack mm_preview_actor_track(const MMPreviewActor *p)
{
    MMActorTrack track;
    track.route = (MMActorPath *)p->route;
    track.events = (MMActorEventTable *)p->events;
    return track;
}

typedef struct PreviewEvent
{
    MMPreviewActor *actor;
    const MMPreviewActorServices *services;
    MMActorEventTable *events;
    int cue_failed;
} PreviewEvent;

static int publish_sound(void *context, const MMActorSound *sound)
{
    PreviewEvent *event = context;
    MMPreviewActorCues *cues = &event->actor->cues;
    MMArray rows = {0};
    MMPreviewCue cue;
    mm_preview_actor_cue(&cue, sound, event->actor->state.frame, event->actor->header.actor_id);
    rows.storage = &cues->storage;
    rows.width = sizeof(cue);
    rows.count = cues->count;
    rows.free_count = -1;
    rows.storage->min_grow[0] = rows.storage->buffers[0].capacity ?
        rows.storage->buffers[0].capacity : 8 * rows.width;
    if (mm_array_add(&rows, &cue, NULL) != MM_STORAGE_OK)
    {
        event->cue_failed = cues->allocation_failed = 1;
        return 0;
    }
    cues->count = rows.count;
    return 1;
}
static int event_cue(void *context, const MMActorSound *sound)
{
    const PreviewEvent *event = context;
    return !event->actor->sound_display || publish_sound(context, sound);
}
static int event_sound(void *context, MMI32 index)
{
    const PreviewEvent *event = context;
    MMActorSoundServices sound = {0};
    sound.context = context;
    sound.cues_only = 1;
    sound.cue = event_cue;
    return mm_actor_sound_event(&event->actor->motion, event->events, index, &sound);
}
static void event_visibility(void *context, int shown)
{
    if (shown)
        mm_preview_actor_shown(((PreviewEvent *)context)->actor);
}
static MMMaterialOwner *event_material(void *context, const MMTag *tag)
{
    const MMPreviewActorServices *s = ((PreviewEvent *)context)->services;
    return s->fetch_event_material(s->context, tag);
}
int mm_preview_actor_event(MMPreviewActor *p, MMActorState *state, const MMActorTrack *track, MMI32 index,
                           const MMPreviewActorServices *s)
{
    PreviewEvent event;
    MMActorPoint origin;
    MMActorEffectHooks hooks = {NULL, event_material, event_visibility, event_sound, 1};
    int result, fault;
    event.actor = p;
    event.services = s;
    event.events = track->events;
    event.cue_failed = 0;
    hooks.context = &event;
    origin.dxr = mm_signed_word(p->header.translation[0]);
    origin.dyr = mm_signed_word(p->header.translation[1]);
    origin.dzr = mm_signed_word(p->header.translation[2]);
    result = mm_actor_runtime_effect(state, track, index, &origin, p->tmpl, p->body, &p->motion, 0,
                                     &s->renderer, &hooks);
    fault = s->fault ? s->fault(s->context) : 0;
    return fault ? fault : event.cue_failed ? -1 : result;
}

static MMU32 template_flags(const MMPreviewActor *p)
{
    return p->tmpl ? mm_template_owner_state((MMTemplateOwner *)p->tmpl)->flags : 0;
}

void mm_preview_actor_info(const MMPreviewActor *p, MMPreviewActorInfo *out)
{
    MMU32 flags = template_flags(p);
    out->number = p->number;
    out->template_type = p->header.template_type;
    out->template_number = p->header.template_number;
    out->actor = p->header.actor_id;
    out->source = p->header.source_id;
    out->has_lifetime = !p->inactive;
    out->first = p->inactive ? 0 : p->header.first_frame;
    out->last = p->inactive ? 0 : p->header.last_frame;
    out->prop = (flags & MM_TEMPLATE_PROP) != 0;
    out->text = (flags & MM_TEMPLATE_TEXT) != 0;
    out->visible = !p->inactive && p->state.onStage;
    out->action = p->state.action;
    out->cel = p->state.cel;
    mm_actor_pose_translation(&p->state, &out->position);
}

int mm_preview_actor_text(const MMPreviewActor *p, MMPreviewActorText *out)
{
    MMTemplateOwner *tmpl = (MMTemplateOwner *)p->tmpl;
    const MMTextDescriptor *text;
    if (!(template_flags(p) & MM_TEMPLATE_TEXT) || !mm_template_owner_is_text(tmpl))
        return 0;
    text = mm_template_owner_descriptor(tmpl);
    out->name = mm_template_owner_name(tmpl);
    out->shape = text->shape;
    out->font_source = text->font.sid;
    out->font_number = text->font.number;
    return 1;
}

MMI32 mm_preview_actor_action_count(const MMPreviewActor *p)
{
    return p->tmpl ? mm_template_owner_state((MMTemplateOwner *)p->tmpl)->action_count : 0;
}

MMI32 mm_preview_actor_action_cels(const MMPreviewActor *p, MMI32 action, MMPreviewActorCelCount *cels)
{
    MMI32 count = 0;
    if (action < 0 || action >= mm_preview_actor_action_count(p) || !cels((MMTemplateOwner *)p->tmpl, action, &count))
        return 0;
    return count > 0 ? count : 0;
}

int mm_preview_actor_costumes(const MMPreviewActor *p, const MMPreviewCostumeCatalog *catalog,
                             void *context, MMPreviewCostumeVisit *visit)
{
    MMTemplateOwner *tmpl = (MMTemplateOwner *)p->tmpl;
    MMI32 set, sets;
    if (!tmpl || !p->body || (template_flags(p) & MM_TEMPLATE_TEXT))
        return 0;
    sets = mm_body_owner_state((MMBodyOwner *)p->body)->shape.set_count;
    for (set = 0; set < sets; ++set)
    {
        void *material = NULL, *current = NULL;
        int generic = 0;
        MMI32 index;
        mm_body_get_material(p->body, set, &generic, &material, &current, catalog->body);
        for (index = 0; index < catalog->count(tmpl, set); ++index)
        {
            MMPreviewCostumeEntry entry;
            MMMaterialOwner *costume;
            entry.choice.set = set;
            entry.choice.id = catalog->id(tmpl, set, index);
            costume = catalog->fetch(tmpl, entry.choice.id);
            if (!costume)
                return 0;
            entry.selected = !generic && current == costume;
            mm_material_owner_release(costume);
            entry.accessory = catalog->accessory(tmpl, set) != 0;
            if (visit && !visit(context, &entry))
                return 0;
        }
    }
    return 1;
}

static int apply_costume(MMPreviewActor *actor, const MMPreviewCostumeCatalog *catalog, MMI32 id)
{
    MMMaterialOwner *costume = catalog->fetch((MMTemplateOwner *)actor->tmpl, id);
    if (!costume)
        return 0;
    mm_body_set_custom(actor->body, costume, catalog->body);
    mm_material_owner_release(costume);
    return 1;
}

int mm_preview_actor_edit_costumes(MMPreviewActor *p, const MMPreviewCostumeCatalog *catalog,
                                  const MMPreviewCostumeEdits *edits, void *context, MMPreviewCostumeApply *apply)
{
    MMTemplateOwner *tmpl = (MMTemplateOwner *)p->tmpl;
    MMI32 sets;
    size_t i;
    if (!mm_preview_actor_costumes(p, catalog, NULL, NULL))
        return 0;
    sets = mm_body_owner_state((MMBodyOwner *)p->body)->shape.set_count;
    for (i = 0; i < edits->count; ++i)
    {
        MMPreviewCostumeChoice choice;
        MMI32 index, count;
        edits->get(edits->context, i, &choice);
        if (choice.set < 0 || choice.set >= sets)
            return 0;
        count = catalog->count(tmpl, choice.set);
        for (index = 0; index < count; ++index)
            if (catalog->id(tmpl, choice.set, index) == choice.id)
                break;
        if (index >= count)
            return 0;
        if (apply)
        {
            int result = apply(context, &choice);
            if (result <= 0)
                return result;
        }
        else if (!apply_costume(p, catalog, choice.id))
            return 0;
    }
    return 1;
}

static void install_track(MMPreviewActor *p, const MMActorTrack *track)
{
    p->route = track->route;
    p->events = track->events;
}

static void release_track(MMPreviewActor *p, const MMTagServices *tags)
{
    MMActorTrack track = mm_preview_actor_track(p);
    mm_actor_track_dispose(&track, tags);
    install_track(p, &track);
}

void mm_preview_actor_init(MMPreviewActor *p)
{
    unsigned i;
    memset(p, 0, sizeof(*p));
    mm_storage_init(&p->cues.storage, NULL);
    mm_actor_sounds_init(&p->motion, NULL);
    mm_actor_state_init(&p->state);
    mm_actor_state_init(&p->orientation);
    for (i = 0; i < 3; ++i)
    {
        p->shape.rotation[i * 4] = 1;
        p->shape.scale[i] = 1;
    }
    p->orientation_reset = 1;
}

void mm_preview_actor_shown(MMPreviewActor *p)
{
    if (p->draw_clock)
    {
        *p->draw_clock = mm_wide_add(*p->draw_clock, mm_wide_i32(1));
        p->draw_order = *p->draw_clock;
    }
}

void mm_preview_actor_dispose(MMPreviewActor *p, const MMPreviewActorServices *s)
{
    if (p->body)
        s->release_body(s->context, p->body);
    p->body = 0;
    if (p->tmpl)
        s->release_template(s->context, p->tmpl);
    p->tmpl = 0;
    mm_actor_sounds_dispose(&p->motion);
    if (s->tags)
        release_track(p, s->tags);
    if (p->material_override)
    {
        s->release_material(s->context, p->material_override);
        p->material_override = NULL;
    }
    mm_storage_dispose(&p->cues.storage);
    p->cues.count = 0;
    p->cues.allocation_failed = 0;
}

const MMPreviewCue *mm_preview_actor_frame_cues(const MMPreviewActor *p, MMU32 *count)
{
    *count = p->cues.count;
    return (const MMPreviewCue *)p->cues.storage.buffers[0].data;
}

int mm_preview_actor_replay_track(MMPreviewActor *p, MMI32 frame, const MMPreviewActorServices *s)
{
    int r;
    mm_actor_state_init(&p->state);
    mm_actor_state_init(&p->orientation);
    mm_actor_state_rewind(&p->state, p->route, &s->timeline.motion);
    p->orientation_reset = 1;
    p->cues.count = 0;
    mm_actor_sounds_clear(&p->motion);
    r = s->default_costume(s->context, p->tmpl, p->body);
    if (r <= 0) return r;
    return seek_actor(p, frame, s);
}
static void remember_origin(MMPreviewActor *p)
{
    unsigned i;
    for (i = 0; i < 3; ++i)
        p->original[i] = mm_signed_word(p->header.translation[i]);
}
static int load_template(MMPreviewActor *p, const MMPreviewActorServices *s)
{
    int r = s->fetch(s->context, &p->header, &p->tmpl);
    if (r <= 0)
        return r;
    return s->bind(s->context, p->tmpl);
}
int mm_preview_actor_load(MMPreviewActor *p, MMU32 number, const MMChunkAccess *chunks, int require_template_target,
                          const MMPreviewActorServices *s)
{
    const MMActorMotionServices *m = &s->timeline.motion;
    MMActorEvent e;
    MMI32 i, n, last;
    int r;
    p->number = number;
    r = s->header(s->context, number, &p->header);
    if (r <= 0)
        return r;
    r = s->route(s->context, number, &p->route);
    if (r <= 0)
        return r;
    r = s->events(s->context, number, &p->events);
    if (r <= 0)
        return r;
    remember_origin(p);
    if (p->header.first_frame == 2147483647)
    {
        p->inactive = 1;
        return 1;
    }
    mm_actor_template_link(chunks, number, p->header.source_id, require_template_target, &p->header.template_number);
    r = load_template(p, s);
    if (r <= 0)
        return r;
    r = s->body_create(s->context, p->tmpl, &p->body);
    if (r <= 0)
        return r;
    n = m->event_count(p->events);
    if (n && !m->route_count(p->route))
        return 0;
    for (i = 0; i < n; ++i)
    {
        m->event(p->events, i, &e);
        if (e.rtel.irpt < 0 || e.rtel.irpt >= m->route_count(p->route))
            return 0;
    }
    if (p->header.last_frame == 2147483647)
    {
        r = mm_actor_lifetime(p->route, p->events, p->header.first_frame, 0, &last, 100000, &s->timeline);
        if (s->fault(s->context))
            return s->fault(s->context);
        if (!r)
            return 0;
        p->header.last_frame = last;
    }
    if (p->header.last_frame < p->header.first_frame || (double)p->header.last_frame - p->header.first_frame > 100000)
        return 0;
    mm_actor_state_rewind(&p->state, p->route, m);
    return 1;
}
int mm_preview_actor_create(MMPreviewActor *p, const MMPreviewActorDefinition *d, const MMPreviewActorServices *s)
{
    MMActorStationaryTrack track;
    MMActorTrack owned;
    unsigned short angles[3];
    unsigned i;
    int r;
    p->number = d->number;
    p->header = d->header;
    for (i = 0; i < 3; ++i)
        p->header.translation[i] = (MMU32)s->scalar(d->point[i]);
    remember_origin(p);
    r = s->fetch(s->context, &p->header, &p->tmpl);
    if (r <= 0)
        return r;
    r = s->bind(s->context, p->tmpl);
    if (r <= 0)
        return r;
    r = s->body_create(s->context, p->tmpl, &p->body);
    if (r <= 0)
        return r;
    owned = mm_preview_actor_track(p);
    if (!mm_actor_track_create(&owned, s->track_allocator, s->trash_unused, s->tags))
        return 0;
    install_track(p, &owned);
    s->rest(s->context, p->tmpl, angles);
    r = s->fault(s->context);
    if (r)
        return r;
    mm_actor_stationary_track(&track, p->header.first_frame, angles);
    if (!mm_actor_path_insert(owned.route, 0, &track.route) ||
        !mm_actor_events_insert(owned.events, 0, sizeof(track.entrance), &track.entrance, &track.events[0]) ||
        !mm_actor_events_insert(owned.events, 1, sizeof(track.action), track.action, &track.events[1]) ||
        !mm_actor_events_insert(owned.events, 2, sizeof(track.step), &track.step, &track.events[2]))
        return 0;
    mm_actor_state_rewind(&p->state, p->route, &s->timeline.motion);
    return 1;
}

int mm_preview_actor_begin_draft(MMPreviewActor *p, MMPreviewActorDraft *draft, const MMPreviewActorServices *s)
{
    MMActorTrack original = mm_preview_actor_track(p);
    MMActorTrack candidate = {0};
    MMActorSounds motion;
    void *body = NULL;
    int result = 0;
    mm_actor_sounds_init(&motion, &p->motion.storage.allocator);
    if (!mm_actor_track_clone(&candidate, &original, s->tags) ||
        !mm_actor_sounds_clone(&motion, &p->motion))
        goto fail;
    result = s->duplicate_body(s->context, p->body, &body);
    if (result <= 0)
        goto fail;
    draft->original = *p;
    mm_storage_init(&p->cues.storage, &draft->original.cues.storage.allocator);
    p->cues.count = 0;
    p->cues.allocation_failed = 0;
    if (p->draw_clock)
        draft->draw_clock = *p->draw_clock;
    p->motion = motion;
    p->body = body;
    install_track(p, &candidate);
    draft->active = 1;
    return 1;

fail:
    if (body)
        s->release_body(s->context, body);
    mm_actor_sounds_dispose(&motion);
    mm_actor_track_dispose(&candidate, s->tags);
    return result;
}

void mm_preview_actor_end_draft(MMPreviewActor *p, MMPreviewActorDraft *draft, const MMPreviewActorServices *s)
{
    MMActorTrack candidate;
    if (!draft->active)
        return;
    candidate = mm_preview_actor_track(p);
    s->release_body(s->context, p->body);
    mm_storage_dispose(&p->cues.storage);
    mm_actor_sounds_dispose(&p->motion);
    *p = draft->original;
    mm_actor_track_dispose(&candidate, s->tags);
    if (p->draw_clock)
        *p->draw_clock = draft->draw_clock;
    draft->active = 0;
}

int mm_preview_actor_replace_track(MMPreviewActor *p, MMI32 source, const MMPreviewEventReplacement *edit,
                                   const MMPreviewActorServices *s)
{
    MMPreviewActor original;
    MMActorTrack candidate = {0}, live;
    MMActorEventTable *events = NULL;
    MMActorEventFactory factory;
    MMResourceCache *cache = NULL;
    MMOwnedBlock block;
    MMBodyCostume costume = {0};
    int result;
    if (source == p->event_source)
        return 1;
    if (!p->body || !p->tmpl || s->text(s->context, p->tmpl))
        return 0;
    result = edit->resolve(edit->context, source, MM_ACTOR_EVENTS_CHUNK, 0, &cache);
    if (result <= 0)
        return result;
    if (!cache || mm_owned_block_init(&block, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    live = mm_preview_actor_track(p);
    factory.allocator = s->track_allocator;
    factory.trash_unused = s->trash_unused;
    result = mm_resource_cache_read_block(cache, MM_ACTOR_EVENTS_CHUNK, 0, &block) == MM_CHUNK_OK &&
             mm_actor_track_clone(&candidate, &live, s->tags) &&
             mm_actor_block_events(&block, edit->decode_context, edit->decode, &factory, &events) == MM_MOVIE_OK;
    mm_owned_block_clear(&block);
    if (!result)
        goto fail;
    mm_actor_track_adopt_events(&candidate, events, s->tags);
    if (!mm_body_costume_capture(&costume, p->body, s->renderer.body))
    {
        result = s->fault ? s->fault(s->context) : 0;
        goto fail;
    }
    original = *p;
    /* Bind the rebuilt sounds to the candidate events. Each list borrows its track's tags. */
    mm_actor_sounds_init(&p->motion, &original.motion.storage.allocator);
    install_track(p, &candidate);
    result = mm_preview_actor_replay_track(p, original.state.frame, s);
    if (result > 0)
    {
        p->event_source = source;
        mm_actor_sounds_dispose(&original.motion);
        mm_actor_track_dispose(&live, s->tags);
        mm_body_costume_clear(&costume, s->renderer.body);
        return 1;
    }
    /* Keep the cue prefix and any entrance completed by the failed replay. */
    original.draw_order = p->draw_order;
    original.cues = p->cues;
    mm_actor_sounds_dispose(&p->motion);
    *p = original;
    mm_body_costume_apply(&costume, p->body, 0, s->renderer.body);

fail:
    mm_actor_track_dispose(&candidate, s->tags);
    mm_body_costume_clear(&costume, s->renderer.body);
    return result;
}

int mm_preview_actor_replace_template(MMPreviewActor *p, MMI32 source, MMU32 number,
                                      const MMPreviewActorServices *s)
{
    MMPreviewActor saved;
    MMActorHeader request;
    void *next = NULL, *body = NULL;
    int result;
    if (!p->tmpl || !p->body || !s->text(s->context, p->tmpl))
        return 0;
    if (p->header.source_id == source && p->header.template_number == number)
        return 1;
    request = p->header;
    request.source_id = source;
    request.template_type = MM_TMPL;
    request.template_number = number;
    result = s->fetch(s->context, &request, &next);
    if (result <= 0)
        goto fail;
    if (!s->text(s->context, next))
    {
        result = 0;
        goto fail;
    }
    result = s->bind(s->context, next);
    if (result <= 0)
        goto fail;
    result = s->duplicate_body(s->context, p->body, &body);
    if (result <= 0)
        goto fail;
    result = s->adjust_text_body(s->context, next, body);
    if (result <= 0)
        goto fail;

    saved = *p;
    p->tmpl = next;
    p->body = body;
    p->header.source_id = source;
    p->header.template_number = number;
    result = mm_preview_actor_position(p, 1, s);
    if (result > 0)
    {
        s->release_body(s->context, saved.body);
        s->release_template(s->context, saved.tmpl);
        return 1;
    }
    *p = saved;

fail:
    if (body)
        s->release_body(s->context, body);
    if (next)
        s->release_template(s->context, next);
    return result;
}

static int equal_shape(const MMPreviewActorShape *a, const MMPreviewActorShape *b)
{
    unsigned i;
    for (i = 0; i < 9; ++i)
        if (a->rotation[i] != b->rotation[i])
            return 0;
    for (i = 0; i < 3; ++i)
        if (a->scale[i] != b->scale[i])
            return 0;
    return 1;
}
static int select_pose(MMPreviewActor *p, const MMPreviewActorServices *s)
{
    return !p->state.onStage ? 1 : s->pose(s->context, p->tmpl, p->body, p->state.action, p->state.cel);
}
int mm_preview_actor_position(MMPreviewActor *p, int select_cel, const MMPreviewActorServices *s)
{
    MMActorPoseInputs inputs;
    MMActorBodyPose pose;
    MMActorMatrix rotation, combined, *basis;
    MMActorState edited;
    MMPreviewActorShape identity;
    unsigned short angles[3];
    unsigned i;
    int fault;
    memset(&identity, 0, sizeof(identity));
    for (i = 0; i < 3; ++i)
    {
        identity.rotation[i * 4] = 1;
        identity.scale[i] = 1;
    }
    s->rest(s->context, p->tmpl, angles);
    fault = s->fault(s->context);
    if (fault)
        return fault;
    inputs.restX = angles[0];
    inputs.restY = angles[1];
    inputs.restZ = angles[2];
    inputs.fixedPathOrientation = s->text(s->context, p->tmpl);
    mm_actor_pose_evaluate(&p->state, p->route, p->events, &inputs, &p->state.position, &pose, &s->timeline.motion);
    if (!p->state.useCurrentRotation || p->orientation_reset)
    {
        p->orientation = p->state;
        if (p->orientation_reset == 1)
            p->orientation.useCurrentRotation = 0;
        p->orientation_reset = 0;
    }
    if (!equal_shape(&p->shape, &identity))
    {
        edited = p->orientation;
        edited.transform.aevpull = p->state.transform.aevpull;
        edited.transform.rScaleStep = p->state.transform.rScaleStep;
        memset(&rotation, 0, sizeof(rotation));
        for (i = 0; i < 9; ++i)
            rotation.m[i / 3][i % 3] = s->scalar(p->shape.rotation[i]);
        basis = edited.useCurrentRotation ? &edited.transform.bmat34Cur : &edited.transform.bmat34Fwd;
        mm_matrix_multiply(&combined, &rotation, basis);
        *basis = combined;
        edited.transform.aevpull.rScaleX = mm_fixed_mul(edited.transform.aevpull.rScaleX, s->scalar(p->shape.scale[0]));
        edited.transform.aevpull.rScaleY = mm_fixed_mul(edited.transform.aevpull.rScaleY, s->scalar(p->shape.scale[1]));
        edited.transform.aevpull.rScaleZ = mm_fixed_mul(edited.transform.aevpull.rScaleZ, s->scalar(p->shape.scale[2]));
        mm_actor_pose_update(&edited, p->route, p->events, &inputs, &p->state.position, &pose.orientation,
                             &s->timeline.motion);
    }
    s->locate(s->context, p->body, &pose);
    fault = s->fault(s->context);
    if (fault)
        return fault;
    return select_cel ? select_pose(p, s) : 1;
}
static int move(MMPreviewActor *p, const float delta[3], int offset, const MMPreviewActorServices *s)
{
    MMI32 origin[3], translation[3], current[3];
    unsigned i;
    double a, b;
    int changed = 0;
    if (!p->body || !p->tmpl)
        return 0;
    for (i = 0; i < 3; ++i)
        current[i] = mm_signed_word(p->header.translation[i]);
    if (offset && delta[0] == 0 && delta[1] == 0 && delta[2] == 0 && current[0] == p->original[0] &&
        current[1] == p->original[1] && current[2] == p->original[2])
        return 1;
    translation[0] = p->state.routeTranslation.dxr;
    translation[1] = p->state.routeTranslation.dyr;
    translation[2] = p->state.routeTranslation.dzr;
    for (i = 0; i < 3; ++i)
    {
        a = (double)s->scalar_float(offset ? p->original[i] : current[i]) + delta[i];
        b = offset ? (double)s->scalar_float(translation[i]) + a - s->scalar_float(current[i])
                   : (double)s->scalar_float(translation[i]) + delta[i];
        if (!(a >= -32700 && a <= 32700 && b >= -32700 && b <= 32700))
            return 0;
        origin[i] = offset && delta[i] == 0 ? p->original[i] : s->scalar((float)a);
        translation[i] = s->scalar((float)b);
        changed = changed || current[i] != origin[i];
    }
    if (offset && !changed && p->state.routeTranslation.dxr == translation[0] &&
        p->state.routeTranslation.dyr == translation[1] && p->state.routeTranslation.dzr == translation[2])
        return 1;
    for (i = 0; i < 3; ++i)
        p->header.translation[i] = (MMU32)origin[i];
    p->state.routeTranslation.dxr = translation[0];
    p->state.routeTranslation.dyr = translation[1];
    p->state.routeTranslation.dzr = translation[2];
    return !p->state.onStage ? 1 : mm_preview_actor_position(p, 0, s);
}
static int place(MMPreviewActor *p, const float delta[3], int offset, MMPreviewOrbit *orbit, MMI32 object,
                 const MMPreviewActorServices *s)
{
    MMActorPoint before, after;
    float from[3], to[3];
    int follow = orbit && mm_preview_orbit_follows(orbit, object, 1);
    int result;
    if (follow)
        mm_actor_pose_translation(&p->state, &before);
    result = move(p, delta, offset, s);
    if (result > 0 && follow)
    {
        mm_actor_pose_translation(&p->state, &after);
        from[0] = s->scalar_float(before.dxr);
        from[1] = s->scalar_float(before.dyr);
        from[2] = s->scalar_float(before.dzr);
        to[0] = s->scalar_float(after.dxr);
        to[1] = s->scalar_float(after.dyr);
        to[2] = s->scalar_float(after.dzr);
        mm_preview_orbit_move(orbit, object, from, to);
    }
    return result;
}
int mm_preview_actor_translate(MMPreviewActor *p, const float d[3], MMPreviewOrbit *orbit, MMI32 object,
                                const MMPreviewActorServices *s)
{
    return place(p, d, 0, orbit, object, s);
}
int mm_preview_actor_offset(MMPreviewActor *p, const float d[3], MMPreviewOrbit *orbit, MMI32 object,
                             const MMPreviewActorServices *s)
{
    return place(p, d, 1, orbit, object, s);
}
static int validate_shape(const MMPreviewActorShape *shape)
{
    const float *r = shape->rotation;
    int a, b, c;
    float determinant, dot;
    for (a = 0; a < 3; ++a)
        if (!(shape->scale[a] >= .01f && shape->scale[a] <= 10))
            return MM_PREVIEW_SHAPE_SCALE;
    for (a = 0; a < 9; ++a)
        if (!(r[a] >= -1.01f && r[a] <= 1.01f))
            return MM_PREVIEW_SHAPE_ROTATION;
    determinant = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                  r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (determinant < .99f)
        return MM_PREVIEW_SHAPE_REFLECTION;
    for (a = 0; a < 3; ++a)
        for (b = a; b < 3; ++b)
        {
            dot = 0;
            for (c = 0; c < 3; ++c)
                dot += r[a * 3 + c] * r[b * 3 + c];
            dot -= a == b ? 1.f : 0.f;
            if (dot < -.003f || dot > .003f)
                return MM_PREVIEW_SHAPE_AXES;
        }
    return MM_PREVIEW_SHAPE_OK;
}
int mm_preview_actor_set_shape(MMPreviewActor *p, const MMPreviewActorShape *shape, MMPreviewOrbit *orbit,
                               MMI32 object, const MMBodyGeometryServices *geometry, const MMPreviewActorServices *s)
{
    const MMBodyShape *body;
    MMBodyBounds before, after;
    int follow, result = validate_shape(shape);
    if (result != MM_PREVIEW_SHAPE_OK)
        return result;
    if (!p->body || !p->tmpl)
        return MM_PREVIEW_SHAPE_FAILED;
    if (!equal_shape(&p->shape, shape))
    {
        follow = orbit && mm_preview_orbit_follows(orbit, object, p->state.onStage);
        body = follow ? &mm_body_owner_state((MMBodyOwner *)p->body)->shape : NULL;
        if (follow)
            mm_body_bounds(body, 1, &before, geometry);
        p->shape = *shape;
        if (p->state.onStage)
            result = mm_preview_actor_position(p, 0, s);
        if (result == MM_PREVIEW_SHAPE_OK && follow)
        {
            mm_body_bounds(body, 1, &after, geometry);
            mm_preview_orbit_resize(orbit, &before, &after, s->scalar_float);
        }
    }
    return result;
}
void mm_preview_actor_shape(const MMPreviewActor *p, MMPreviewActorShape *shape, float left[9], float right[9],
                            const MMPreviewActorServices *s)
{
    MMActorMatrix a, b;
    MMActorEvent e;
    MMActorEntrance add;
    const MMActorState *c = &p->orientation;
    unsigned short x, y, z;
    unsigned i;
    *shape = p->shape;
    mm_matrix_identity(&a);
    b = c->useCurrentRotation ? c->transform.bmat34Cur : c->transform.bmat34Fwd;
    if (!c->useCurrentRotation && c->addEvent >= 0)
    {
        s->timeline.motion.event(p->events, c->addEvent, &e);
        if (s->text(s->context, p->tmpl) || (c->location.irpt == e.rtel.irpt && !c->location.dwrOffset))
        {
            s->timeline.motion.payload(p->events, c->addEvent, &add);
            mm_matrix_rotate(&b, MM_MATRIX_X, add.x, 0);
            mm_matrix_rotate(&b, MM_MATRIX_Y, add.y, 0);
            mm_matrix_rotate(&b, MM_MATRIX_Z, add.z, 0);
        }
        else
        {
            mm_actor_pose_orientation(c, p->route, p->events, 0, &x, &y, &z, 0, &s->timeline.motion);
            mm_matrix_identity(&a);
            mm_matrix_rotate(&a, MM_MATRIX_X, x, 0);
            mm_matrix_rotate(&b, MM_MATRIX_Y, y, 0);
        }
    }
    for (i = 0; i < 9; ++i)
    {
        if (left)
            left[i] = s->scalar_float(a.m[i / 3][i % 3]);
        if (right)
            right[i] = s->scalar_float(b.m[i / 3][i % 3]);
    }
}
typedef struct PreviewActorHost
{
    MMPreviewActor *actor;
    const MMPreviewActorServices *services;
} PreviewActorHost;

static int frame_event(void *context, MMI32 index)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    MMPreviewActor *p = host->actor;
    const MMPreviewActorServices *s = host->services;
    MMActorEvent event;
    s->timeline.motion.event(p->events, index, &event);
    if (event.aet == MM_ACTOR_ADD)
        p->orientation_reset = 1;
    if (event.aet == MM_ACTOR_ROTATE_HERE)
        p->orientation_reset = 2;
    if (s->apply)
        return s->apply(s->context, index);
    {
        MMActorTrack track = mm_preview_actor_track(p);
        return mm_preview_actor_event(p, &p->state, &track, index, s);
    }
}

static int motion_cels(void *context, MMI32 action, MMI32 *out)
{
    const PreviewEvent *event = context;
    (void)action;
    *out = event->actor->state.celCount;
    return 1;
}

static int frame_motion_sounds(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    const MMPreviewActorServices *s = host->services;
    MMActorState *state = &host->actor->state;
    PreviewEvent event;
    MMActorSoundServices sound = {0};
    int result, fault;
    if (s->motion_cues)
        return s->motion_cues(s->context);
    event.actor = host->actor;
    event.services = s;
    event.events = NULL;
    event.cue_failed = 0;
    sound.context = &event;
    sound.cues_only = 1;
    sound.cel_count = motion_cels;
    sound.cue = publish_sound;
    result = mm_actor_sound_motion(&host->actor->motion, state->action, state->cel, &sound);
    fault = s->fault ? s->fault(s->context) : 0;
    return fault ? fault : event.cue_failed ? -1 : result;
}

static void frame_position(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    mm_preview_actor_position(host->actor, 0, host->services);
}

static int frame_cel(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    return select_pose(host->actor, host->services);
}

static void prepare_frame(void *context, MMActorFrameInputs *inputs)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    host->actor->cues.count = 0;
    host->actor->sound_display = inputs->displayFrame;
}

static int seek_fault(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    return host->services->fault(host->services->context);
}

static void seek_hide(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    host->actor->state.onStage = 0;
}

static int seek_default_costume(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    MMPreviewActor *p = host->actor;
    const MMPreviewActorServices *s = host->services;
    p->orientation_reset = 1;
    mm_actor_sounds_clear(&p->motion);
    return s->default_costume(s->context, p->tmpl, p->body);
}

static int seek_actor(MMPreviewActor *p, MMI32 target, const MMPreviewActorServices *s)
{
    PreviewActorHost host;
    MMActorPlaybackEffects effects;
    if (target != p->state.frame)
        p->cues.count = 0;
    if (p->inactive)
        return 1;
    if ((double)target - p->header.first_frame > 100000)
        return 0;
    host.actor = p;
    host.services = s;
    memset(&effects, 0, sizeof(effects));
    effects.context = &host;
    effects.hide = seek_hide;
    effects.reset_costume = seek_default_costume;
    effects.prepare = prepare_frame;
    effects.apply_event = frame_event;
    effects.motion_sounds = frame_motion_sounds;
    effects.position_body = frame_position;
    effects.select_cel = frame_cel;
    effects.update_pose = frame_position;
    effects.fault = seek_fault;
    return mm_actor_playback_seek(&p->state, p->route, p->events, p->header.first_frame, p->header.last_frame, target,
                                  MM_ACTOR_FRAME_PREVIEW, 0, &s->timeline, &effects);
}

int mm_preview_actor_seek(MMPreviewActor *p, MMI32 target, const MMPreviewActorServices *s)
{
    int result = seek_actor(p, target, s);
    if (result > 0 && p->material_override && p->body)
        return s->apply_material(s->context, p->body, p->material_override);
    return result;
}

int mm_preview_actor_set_material(MMPreviewActor *p, MMI32 source, MMU32 number, const MMPreviewActorServices *s)
{
    void *next = NULL, *previous;
    int result;
    if (!p->body || !p->tmpl || !s->text(s->context, p->tmpl))
        return 0;
    result = s->fetch_material(s->context, source, number, &next);
    if (result > 0 && next)
    {
        result = s->apply_material(s->context, p->body, next);
        if (result > 0)
        {
            previous = p->material_override;
            p->material_override = next;
            if (previous)
                s->release_material(s->context, previous);
            return result;
        }
    }
    if (next)
        s->release_material(s->context, next);
    return result <= 0 ? result : 0;
}

static float lower(float a, float b) { return b < a ? b : a; }
static float upper(float a, float b) { return a < b ? b : a; }

static void box_corners(const float low[3], const float high[3], float corners[24])
{
    unsigned corner, axis;
    for (corner = 0; corner < 8; ++corner)
        for (axis = 0; axis < 3; ++axis)
            corners[corner * 3 + axis] = corner & (1U << axis) ? high[axis] : low[axis];
}

static void include_corners(const float corners[24], float low[3], float high[3])
{
    unsigned corner, axis;
    for (corner = 0; corner < 8; ++corner)
        for (axis = 0; axis < 3; ++axis)
        {
            const float value = corners[corner * 3 + axis];
            low[axis] = lower(low[axis], value);
            high[axis] = upper(high[axis], value);
        }
}

static void rest_orientation(MMActorMatrix *out, const unsigned short angle[3])
{
    mm_matrix_identity(out);
    mm_matrix_rotate(out, MM_MATRIX_X, angle[0], 0);
    mm_matrix_rotate(out, MM_MATRIX_Y, angle[1], 0);
    mm_matrix_rotate(out, MM_MATRIX_Z, angle[2], 0);
}

int mm_preview_actor_track_pose(const MMPreviewActor *actor, MMPreviewTrackPose *out, const MMPreviewActorServices *s)
{
    const MMActorState *state = &actor->state;
    const MMActorMatrix *root;
    MMActorMatrix rest;
    unsigned short angles[3] = {0};
    MMI32 stretch[3], scaled;
    int row, col, i;
    double value;
    if (!state->onStage || !actor->body || !actor->tmpl)
        return 0;
    out->node = state->location.irpt;
    out->offset = state->location.dwrOffset;
    out->route_frame = state->location.dnfrm;
    out->position[0] = state->position.dxr;
    out->position[1] = state->position.dyr;
    out->position[2] = state->position.dzr;
    out->translation[0] = state->routeTranslation.dxr;
    out->translation[1] = state->routeTranslation.dyr;
    out->translation[2] = state->routeTranslation.dzr;
    s->rest(s->context, actor->tmpl, angles);
    if (s->fault && s->fault(s->context))
        return 0;
    rest_orientation(&rest, angles);
    stretch[0] = state->transform.aevpull.rScaleX;
    stretch[1] = state->transform.aevpull.rScaleY;
    stretch[2] = state->transform.aevpull.rScaleZ;
    root = &mm_body_owner_state((MMBodyOwner *)actor->body)->shape.bindings[0].matrix;
    /* Remove the template rest orientation and authored size from the posed root. */
    for (row = 0; row < 3; ++row)
    {
        scaled = mm_fixed_mul(mm_fixed_mul(stretch[row], s->scalar(actor->shape.scale[row])),
                              state->transform.rScaleStep);
        if (!scaled)
            return 0;
        for (col = 0; col < 3; ++col)
        {
            value = 0;
            for (i = 0; i < 3; ++i)
                value += s->scalar_float(rest.m[i][row]) * (double)s->scalar_float(root->m[i][col]);
            out->rotation[row * 3 + col] = (float)(value / s->scalar_float(scaled));
        }
    }
    return 1;
}

int mm_preview_actor_bounds(const MMPreviewActor *actor, float corners[24], const MMBodyGeometryServices *geometry,
                            float (*scalar_float)(MMI32))
{
    const MMBodyShape *body;
    MMBodyBounds local;
    if (!actor || !actor->state.onStage || !actor->body)
        return 0;
    body = &mm_body_owner_state((MMBodyOwner *)actor->body)->shape;
    mm_body_bounds(body, 0, &local, geometry);
    return mm_preview_camera_bounds(&local, &body->bindings[0].matrix, corners, scalar_float);
}

static int rest_pose(const MMPreviewActor *actor, const MMPreviewActorServices *s)
{
    MMActorBodyPose pose = {0};
    unsigned short angle[3] = {0};
    s->rest(s->context, actor->tmpl, angle);
    if (s->fault && s->fault(s->context))
        return 0;
    rest_orientation(&pose.orientation, angle);
    s->locate(s->context, actor->body, &pose);
    return !s->fault || !s->fault(s->context);
}

static int action_clip(const MMPreviewActor *actor, MMI32 action, MMI32 cel, MMI32 count,
                       const float pose[24], float clip[24], const MMPreviewActorServices *s,
                       const MMPreviewIsolatedView *v)
{
    float low[3], high[3], bounds[24];
    MMI32 i, samples = count < 24 ? count : 24;
    memcpy(low, pose, sizeof(low));
    memcpy(high, low, sizeof(high));
    include_corners(pose, low, high);
    for (i = 0; i < samples; ++i)
    {
        MMI32 sample = 0;
        if (samples > 1)
        {
            const MMI32 stride = samples - 1, span = count - 1;
            sample = i * (span / stride) + (i * (span % stride)) / stride;
        }
        if (sample == cel)
            continue;
        if (s->pose(s->context, actor->tmpl, actor->body, action, sample) <= 0 ||
            !mm_preview_actor_bounds(actor, bounds, v->geometry, s->scalar_float))
            return MM_PREVIEW_ACTION_FRAME;
        include_corners(bounds, low, high);
    }
    if (s->pose(s->context, actor->tmpl, actor->body, action, cel) <= 0)
        return MM_PREVIEW_ACTION_POSE;
    box_corners(low, high, clip);
    return MM_PREVIEW_ACTION_OK;
}

int mm_preview_actor_action_render(MMPreviewActor *actor, MMI32 action, MMI32 cel, MMI32 count,
                                   float clip[24], int *clip_valid, const MMPreviewActorServices *s,
                                   const MMPreviewIsolatedView *v)
{
    void *original = actor->body, *duplicate = NULL;
    float pose[24];
    int result = MM_PREVIEW_ACTION_OK;
    if (cel < 0 || cel >= count)
        return MM_PREVIEW_ACTION_RANGE;
    if (s->duplicate_body(s->context, original, &duplicate) <= 0 || !duplicate)
    {
        if (duplicate)
            s->release_body(s->context, duplicate);
        return MM_PREVIEW_ACTION_MEMORY;
    }
    actor->body = duplicate;
    if (!rest_pose(actor, s) || s->pose(s->context, actor->tmpl, duplicate, action, cel) <= 0 ||
        !mm_preview_actor_bounds(actor, pose, v->geometry, s->scalar_float))
        result = MM_PREVIEW_ACTION_POSE;
    else if (clip && !*clip_valid)
    {
        result = action_clip(actor, action, cel, count, pose, clip, s, v);
        *clip_valid = result == MM_PREVIEW_ACTION_OK;
    }
    if (result == MM_PREVIEW_ACTION_OK && !v->render(v->context, clip ? clip : pose, 0))
        result = MM_PREVIEW_ACTION_FAILED;
    actor->body = original;
    s->release_body(s->context, duplicate);
    return result;
}

static int costume_frame(MMPreviewActor *actor, MMI32 set, float corners[24], const MMPreviewCostumeCatalog *catalog,
                         const MMPreviewActorServices *s, const MMPreviewIsolatedView *v)
{
    MMTemplateOwner *tmpl = (MMTemplateOwner *)actor->tmpl;
    const MMBodyShape *shape = &mm_body_owner_state((MMBodyOwner *)actor->body)->shape;
    MMBodyCostume selected;
    MMBodyBounds bounds;
    float low[3], high[3];
    float body_low = corners[1], body_high = corners[1];
    MMI32 count, i;
    unsigned corner, axis;
    int has_bounds = 0, result = MM_PREVIEW_COSTUME_OK;
    if (!mm_preview_actor_costumes(actor, catalog, NULL, NULL))
        return MM_PREVIEW_COSTUME_FAILED;
    mm_body_costume_init(&selected);
    if (!mm_body_costume_capture(&selected, actor->body, catalog->body))
    {
        mm_body_costume_clear(&selected, catalog->body);
        return MM_PREVIEW_COSTUME_FAILED;
    }
    count = set < shape->set_count ? catalog->count(tmpl, set) : 0;
    for (corner = 1; corner < 8; ++corner)
    {
        body_low = lower(body_low, corners[corner * 3 + 1]);
        body_high = upper(body_high, corners[corner * 3 + 1]);
    }
    for (i = 0; i < count; ++i)
    {
        if (!apply_costume(actor, catalog, catalog->id(tmpl, set, i)))
        {
            result = MM_PREVIEW_COSTUME_FAILED;
            break;
        }
        mm_body_bounds(shape, 1, &bounds, v->geometry);
        body_low = lower(body_low, s->scalar_float(bounds.min[1]));
        body_high = upper(body_high, s->scalar_float(bounds.max[1]));
        if (!mm_body_set_bounds(shape, set, v->model_bounds, &bounds))
            continue;
        for (axis = 0; axis < 3; ++axis)
        {
            const float part_low = s->scalar_float(bounds.min[axis]);
            const float part_high = s->scalar_float(bounds.max[axis]);
            low[axis] = has_bounds ? lower(low[axis], part_low) : part_low;
            high[axis] = has_bounds ? upper(high[axis], part_high) : part_high;
        }
        has_bounds = 1;
    }
    if (result == MM_PREVIEW_COSTUME_OK)
        mm_body_costume_apply(&selected, actor->body, 0, catalog->body);
    mm_body_costume_clear(&selected, catalog->body);
    if (result != MM_PREVIEW_COSTUME_OK)
        return result;
    if (count <= 0)
        return MM_PREVIEW_COSTUME_NO_SET;
    if (has_bounds)
    {
        const float minimum = (body_high - body_low) * .15f;
        for (axis = 0; axis < 3; ++axis)
        {
            const float center = (low[axis] + high[axis]) * .5f;
            const float half = upper(minimum, high[axis] - low[axis]) * .6f;
            low[axis] = center - half;
            high[axis] = center + half;
        }
        box_corners(low, high, corners);
    }
    return MM_PREVIEW_COSTUME_OK;
}

int mm_preview_actor_costume_render(MMPreviewActor *actor, MMI32 set, const MMPreviewCostumeEdits *edits,
                                    const MMPreviewCostumeCatalog *catalog, const MMPreviewActorServices *s,
                                    const MMPreviewIsolatedView *v)
{
    MMBodyCostume original;
    MMActorBodyPose pose = {0};
    int result = MM_PREVIEW_COSTUME_OK;
    float corners[24];
    if (!actor->body || !actor->tmpl)
        return MM_PREVIEW_COSTUME_PREPARE;
    mm_body_costume_init(&original);
    if (!mm_body_costume_capture(&original, actor->body, catalog->body))
    {
        mm_body_costume_clear(&original, catalog->body);
        return MM_PREVIEW_COSTUME_PREPARE;
    }
    pose.orientation = mm_body_owner_state((MMBodyOwner *)actor->body)->shape.bindings[0].matrix;
    if (!rest_pose(actor, s))
        result = MM_PREVIEW_COSTUME_PREPARE;
    else if (!mm_preview_actor_edit_costumes(actor, catalog, edits, NULL, NULL))
        result = MM_PREVIEW_COSTUME_APPLY;
    else if (!mm_preview_actor_bounds(actor, corners, v->geometry, s->scalar_float))
        result = MM_PREVIEW_COSTUME_FAILED;
    else if (set >= 0)
        result = costume_frame(actor, set, corners, catalog, s, v);
    if (result == MM_PREVIEW_COSTUME_OK && !v->render(v->context, corners, set >= 0))
        result = MM_PREVIEW_COSTUME_FAILED;
    mm_body_costume_apply(&original, actor->body, 0, catalog->body);
    mm_body_locate(actor->body, &pose, catalog->body);
    mm_body_costume_clear(&original, catalog->body);
    return result;
}
