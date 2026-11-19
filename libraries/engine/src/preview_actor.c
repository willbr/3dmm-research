#include "mm/preview_actor.h"
#include "mm/template_create.h"
#include "mm/wide.h"
#include <string.h>
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
}
int mm_preview_actor_replace_events(MMPreviewActor *p, void *events, MMI32 frame, const MMPreviewActorServices *s)
{
    int r;
    p->events = events;
    mm_actor_state_init(&p->state);
    mm_actor_state_init(&p->orientation);
    mm_actor_state_rewind(&p->state, p->route, &s->timeline.motion);
    p->orientation_reset = 1;
    s->clear_cues(s->context);
    s->clear_motion(s->context);
    r = s->default_costume(s->context, p->tmpl, p->body);
    if (r <= 0) return r;
    return mm_preview_actor_seek(p, frame, s);
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
    r = s->track_create(s->context, &p->route, &p->events);
    if (r <= 0)
        return r;
    s->rest(s->context, p->tmpl, angles);
    r = s->fault(s->context);
    if (r)
        return r;
    mm_actor_stationary_track(&track, p->header.first_frame, angles);
    r = s->route_insert(s->context, p->route, 0, &track.route);
    if (r <= 0)
        return r;
    r = s->event_insert(s->context, p->events, 0, sizeof(track.entrance), &track.entrance, &track.events[0]);
    if (r <= 0)
        return r;
    r = s->event_insert(s->context, p->events, 1, sizeof(track.action), track.action, &track.events[1]);
    if (r <= 0)
        return r;
    r = s->event_insert(s->context, p->events, 2, sizeof(track.step), &track.step, &track.events[2]);
    if (r <= 0)
        return r;
    mm_actor_state_rewind(&p->state, p->route, &s->timeline.motion);
    return 1;
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

int mm_preview_actor_seek(MMPreviewActor *p, MMI32 target, const MMPreviewActorServices *s)
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
