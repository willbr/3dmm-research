#include "mm/preview_actor.h"
#include "mm/actor_persistence.h"
#include "mm/template_create.h"
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
    mm_actor_state_init(&p->state);
    mm_actor_state_init(&p->orientation);
    for (i = 0; i < 3; ++i)
    {
        p->shape.rotation[i * 4] = 1;
        p->shape.scale[i] = 1;
    }
    p->orientation_reset = 1;
}
void mm_preview_actor_dispose(MMPreviewActor *p, const MMPreviewActorServices *s)
{
    if (p->body)
        s->release_body(s->context, p->body);
    p->body = 0;
    if (p->tmpl)
        s->release_template(s->context, p->tmpl);
    p->tmpl = 0;
    if (s->tags)
        release_track(p, s->tags);
    if (p->material_override)
    {
        s->release_material(s->context, p->material_override);
        p->material_override = NULL;
    }
}
int mm_preview_actor_replay_track(MMPreviewActor *p, MMI32 frame, const MMPreviewActorServices *s)
{
    int r;
    mm_actor_state_init(&p->state);
    mm_actor_state_init(&p->orientation);
    mm_actor_state_rewind(&p->state, p->route, &s->timeline.motion);
    p->orientation_reset = 1;
    s->clear_cues(s->context);
    s->clear_motion(s->context);
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
    int r = s->header(s->context, number, &p->header);
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

int mm_preview_actor_begin_draft(MMPreviewActor *p, MMActorSounds *motion, MMPreviewActorDraft *draft,
                                 const MMPreviewActorServices *s)
{
    MMActorTrack original = mm_preview_actor_track(p);
    MMActorTrack candidate = {0};
    void *body = NULL;
    int result = 0;
    mm_actor_sounds_init(&draft->motion, NULL);
    if (!mm_actor_track_clone(&candidate, &original, s->tags) ||
        !mm_actor_sounds_clone(&draft->motion, motion))
        goto fail;
    result = s->duplicate_body(s->context, p->body, &body);
    if (result <= 0)
        goto fail;
    draft->original = *p;
    mm_actor_sounds_swap(motion, &draft->motion);
    p->body = body;
    install_track(p, &candidate);
    draft->active = 1;
    return 1;

fail:
    if (body)
        s->release_body(s->context, body);
    mm_actor_sounds_dispose(&draft->motion);
    mm_actor_track_dispose(&candidate, s->tags);
    return result;
}

void mm_preview_actor_end_draft(MMPreviewActor *p, MMActorSounds *motion, MMPreviewActorDraft *draft,
                                const MMPreviewActorServices *s)
{
    MMActorTrack candidate;
    if (!draft->active)
        return;
    candidate = mm_preview_actor_track(p);
    s->release_body(s->context, p->body);
    *p = draft->original;
    mm_actor_sounds_swap(motion, &draft->motion);
    mm_actor_sounds_dispose(&draft->motion);
    mm_actor_track_dispose(&candidate, s->tags);
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
    result = edit->save_costume(edit->context);
    if (result <= 0)
        goto fail;
    original = *p;
    install_track(p, &candidate);
    result = mm_preview_actor_replay_track(p, original.state.frame, s);
    if (result > 0)
    {
        p->event_source = source;
        mm_actor_track_dispose(&live, s->tags);
        return 1;
    }
    *p = original;
    edit->restore_costume(edit->context);

fail:
    mm_actor_track_dispose(&candidate, s->tags);
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
int mm_preview_actor_translate(MMPreviewActor *p, const float d[3], const MMPreviewActorServices *s)
{
    return move(p, d, 0, s);
}
int mm_preview_actor_offset(MMPreviewActor *p, const float d[3], const MMPreviewActorServices *s)
{
    return move(p, d, 1, s);
}
int mm_preview_actor_set_shape(MMPreviewActor *p, const MMPreviewActorShape *shape, const MMPreviewActorServices *s)
{
    if (!p->body || !p->tmpl)
        return 0;
    if (equal_shape(&p->shape, shape))
        return 1;
    p->shape = *shape;
    return !p->state.onStage ? 1 : mm_preview_actor_position(p, 0, s);
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
    return s->apply(s->context, index);
}

static int frame_motion_sounds(void *context)
{
    const PreviewActorHost *host = (const PreviewActorHost *)context;
    return host->services->motion_cues(host->services->context);
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
    const MMPreviewActorServices *s = host->services;
    s->clear_cues(s->context);
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
    s->clear_motion(s->context);
    return s->default_costume(s->context, p->tmpl, p->body);
}

static int seek_actor(MMPreviewActor *p, MMI32 target, const MMPreviewActorServices *s)
{
    PreviewActorHost host;
    MMActorPlaybackEffects effects;
    if (target != p->state.frame)
        s->clear_cues(s->context);
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

int mm_preview_actor_rest_pose(const MMPreviewActor *actor, const MMPreviewActorServices *s)
{
    MMActorBodyPose pose = {0};
    unsigned short angle[3] = {0};
    s->rest(s->context, actor->tmpl, angle);
    if (s->fault && s->fault(s->context))
        return 0;
    mm_matrix_identity(&pose.orientation);
    mm_matrix_rotate(&pose.orientation, MM_MATRIX_X, angle[0], 0);
    mm_matrix_rotate(&pose.orientation, MM_MATRIX_Y, angle[1], 0);
    mm_matrix_rotate(&pose.orientation, MM_MATRIX_Z, angle[2], 0);
    s->locate(s->context, actor->body, &pose);
    return !s->fault || !s->fault(s->context);
}

static int action_clip(const MMPreviewActor *actor, MMI32 action, MMI32 cel, MMI32 count,
                       const float pose[24], float clip[24], const MMPreviewActorServices *s,
                       const MMPreviewActionView *v)
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
        if (s->pose(s->context, actor->tmpl, actor->body, action, sample) <= 0 || !v->bounds(v->context, bounds))
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
                                   const MMPreviewActionView *v)
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
    if (!mm_preview_actor_rest_pose(actor, s) || s->pose(s->context, actor->tmpl, duplicate, action, cel) <= 0 ||
        !v->bounds(v->context, pose))
        result = MM_PREVIEW_ACTION_POSE;
    else if (clip && !*clip_valid)
    {
        result = action_clip(actor, action, cel, count, pose, clip, s, v);
        *clip_valid = result == MM_PREVIEW_ACTION_OK;
    }
    if (result == MM_PREVIEW_ACTION_OK && !v->render(v->context, clip ? clip : pose))
        result = MM_PREVIEW_ACTION_FAILED;
    actor->body = original;
    s->release_body(s->context, duplicate);
    return result;
}

static int costume_frame(const MMPreviewCostumeServices *s, MMI32 set, float corners[24])
{
    float low[3], high[3];
    float body_low = corners[1], body_high = corners[1];
    size_t count, i;
    unsigned corner, axis;
    int has_set = 0, has_bounds = 0;
    if (!s->variants(s->context, &count) || !s->save(s->context, 1))
        return MM_PREVIEW_COSTUME_FAILED;
    for (corner = 1; corner < 8; ++corner)
    {
        body_low = lower(body_low, corners[corner * 3 + 1]);
        body_high = upper(body_high, corners[corner * 3 + 1]);
    }
    for (i = 0; i < count; ++i)
    {
        float vertical[2], part_low[3], part_high[3];
        if (s->variant_set(s->context, i) != set)
            continue;
        has_set = 1;
        if (!s->apply_variant(s->context, i))
            return MM_PREVIEW_COSTUME_FAILED;
        s->vertical_bounds(s->context, vertical);
        body_low = lower(body_low, vertical[0]);
        body_high = upper(body_high, vertical[1]);
        if (!s->part_bounds(s->context, set, part_low, part_high))
            continue;
        for (axis = 0; axis < 3; ++axis)
        {
            low[axis] = has_bounds ? lower(low[axis], part_low[axis]) : part_low[axis];
            high[axis] = has_bounds ? upper(high[axis], part_high[axis]) : part_high[axis];
        }
        has_bounds = 1;
    }
    s->restore(s->context, 1);
    if (!has_set)
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

int mm_preview_costume_render(MMI32 set, const MMPreviewCostumeServices *s)
{
    int result = MM_PREVIEW_COSTUME_OK;
    float corners[24];
    if (!s->save(s->context, 0))
        return MM_PREVIEW_COSTUME_PREPARE;
    if (!s->rest_pose(s->context))
        result = MM_PREVIEW_COSTUME_PREPARE;
    else if (!s->apply(s->context))
        result = MM_PREVIEW_COSTUME_APPLY;
    else if (!s->object_bounds(s->context, corners))
        result = MM_PREVIEW_COSTUME_FAILED;
    else if (set >= 0)
        result = costume_frame(s, set, corners);
    if (result == MM_PREVIEW_COSTUME_OK && !s->render(s->context, corners, set >= 0))
        result = MM_PREVIEW_COSTUME_FAILED;
    s->restore(s->context, 0);
    return result;
}
