#include "mm/preview_actor_edit.h"
#include "mm/actor_costume_edit.h"
#include "mm/body_runtime.h"
#include "mm/material_resource.h"
#include "mm/movie_session.h"
#include "mm/sound_resource.h"
#include <string.h>

typedef struct DraftEdit
{
    MMPreviewActor *actor;
    const MMPreviewActorEditServices *services;
    MMActorRecording recording;
    int life_dirty;
    MMSoundState sound;
} DraftEdit;

static void ignore(void *context) { (void)context; }

static int begin_draft(DraftEdit *edit, MMPreviewActorDraft *draft)
{
    const MMPreviewActorEditServices *s = edit->services;
    int result = mm_preview_actor_begin_draft(edit->actor, s->playback->motion, draft, s->playback);
    if (result > 0)
        s->swap_cues(s->context);
    return result;
}

static int finish_draft(DraftEdit *edit, MMPreviewActorDraft *draft, int result, int with_route)
{
    const MMPreviewActorEditServices *s = edit->services;
    const MMPreviewActorServices *play = s->playback;
    int fault = play->fault ? play->fault(play->context) : 0;
    if (fault)
        result = fault;
    if (result > 0)
    {
        MMActorTrack track = mm_preview_actor_track(edit->actor);
        result = s->write(s->context, &track, with_route);
    }
    mm_preview_actor_end_draft(edit->actor, play->motion, draft, play);
    s->swap_cues(s->context);
    return result;
}

static int apply_event(void *context, MMActorState *state, MMI32 index)
{
    DraftEdit *edit = context;
    MMActorTrack track = mm_preview_actor_track(edit->actor);
    return mm_preview_actor_event(edit->actor, state, &track, index, edit->services->playback) > 0;
}

static int scene_frame(void *context, MMI32 *out)
{
    *out = ((DraftEdit *)context)->actor->state.frame;
    return 1;
}

static void get_range(void *context, MMActorEditRange *range)
{
    const MMActorHeader *header = &((DraftEdit *)context)->actor->header;
    range->first = header->first_frame;
    range->last = header->last_frame;
}

static void publish_range(void *context, const MMActorEditRange *range)
{
    MMActorHeader *header = &((DraftEdit *)context)->actor->header;
    header->first_frame = range->first;
    header->last_frame = range->last;
}

static MMActorEditServices event_edits(DraftEdit *edit)
{
    MMActorEditServices s = {0};
    MMActorTrack track = mm_preview_actor_track(edit->actor);
    s.context = edit;
    s.tables = mm_actor_edit_tables(track.events, edit->services->playback->tags);
    s.scene_frame = scene_frame;
    s.update_sound_frame = ignore;
    s.apply_event = apply_event;
    s.dirty = s.invalidate_range = ignore;
    s.get_range = get_range;
    s.publish_range = publish_range;
    return s;
}

static void *current_material(void *context, MMI32 set, int *custom)
{
    const DraftEdit *edit = context;
    void *material = NULL, *costume = NULL;
    int generic = 0;
    mm_body_get_material(edit->actor->body, set, &generic, &material, &costume,
                         edit->services->playback->renderer.body);
    *custom = !generic;
    return generic ? material : costume;
}

static void *fetch_custom(void *context, MMI32 id)
{
    const DraftEdit *edit = context;
    return edit->services->playback->renderer.fetch_custom(edit->actor->tmpl, id);
}

static void *fetch_material(void *context, const MMTag *tag)
{
    const MMPreviewActorServices *play = ((DraftEdit *)context)->services->playback;
    return play->fetch_event_material(play->context, tag);
}

static MMI32 material_set(void *context, void *material)
{
    (void)context;
    return mm_material_owner_custom(material)->set;
}

static void release_material(void *context, void *material)
{
    (void)context;
    mm_material_owner_release(material);
}

static int record_costume(void *context, const MMPreviewCostumeChoice *choice)
{
    const MMActorCostumeEditServices *s = context;
    DraftEdit *edit = s->context;
    MMPreviewActor *actor = edit->actor;
    const MMPreviewActorServices *play = edit->services->playback;
    const MMTag tag = {0};
    int result = mm_actor_edit_set_costume(&actor->state, play->motion, actor->events,
                                          actor->header.actor_id, choice->set, &tag, choice->id, 1,
                                          &play->timeline.motion, s);
    int fault = play->fault ? play->fault(play->context) : 0;
    return fault ? fault : result;
}

int mm_preview_actor_write_costumes(MMPreviewActor *actor, const MMPreviewCostumeEdits *choices,
                                    const MMPreviewCostumeCatalog *catalog, const MMPreviewActorEditServices *s)
{
    DraftEdit edit = {0};
    MMPreviewActorDraft draft = {0};
    MMActorCostumeEditServices costume = {0};
    const MMPreviewActorServices *play = s->playback;
    int result;
    if (!actor->state.onStage || !actor->tmpl || play->text(play->context, actor->tmpl))
        return 0;
    edit.actor = actor;
    edit.services = s;
    result = begin_draft(&edit, &draft);
    if (result <= 0)
        return result;
    costume.context = &edit;
    costume.edits = event_edits(&edit);
    costume.current = current_material;
    costume.fetch_custom = fetch_custom;
    costume.fetch_material = fetch_material;
    costume.body_set = material_set;
    costume.release = release_material;
    result = mm_preview_actor_edit_costumes(actor, catalog, choices, &costume, record_costume);
    return finish_draft(&edit, &draft, result, 0);
}

static int cel_count(void *context, MMI32 action, MMI32 *out)
{
    const DraftEdit *edit = context;
    *out = 0;
    return edit->services->playback->renderer.cel_count(edit->actor->tmpl, action, out);
}

static int action_flags(void *context, MMI32 action, MMU32 *out)
{
    const DraftEdit *edit = context;
    *out = 0;
    return edit->services->playback->renderer.action_flags(edit->actor->tmpl, action, out);
}

static int default_sound(void *context, MMI32 action, MMI32 cel, int *exists, MMTag *out)
{
    const DraftEdit *edit = context;
    MMTag tag = {0};
    int available = 0;
    int ok = edit->services->template_sound(edit->actor->tmpl, action, cel, &available, &tag);
    *exists = ok && available && tag.sid != 0;
    if (*exists)
        *out = tag;
    return ok;
}

static void *fetch_sound(void *context, const MMTag *tag)
{
    DraftEdit *edit = context;
    const MMPreviewActorEditServices *s = edit->services;
    const unsigned short one = 1;
    MMMovieSessionGraph graph;
    if (!s->sound_graph || s->sound_graph(s->context, tag, &graph) <= 0)
        return NULL;
    memset(&edit->sound, 0, sizeof(edit->sound));
    if (!mm_sound_read_graph(&edit->sound, &graph, tag->type, tag->number, *(const unsigned char *)&one == 0) ||
        edit->sound.invalid)
        return NULL;
    return &edit->sound;
}

static void release_sound(void *context, void *sound) { (void)context; (void)sound; }

static void sound_info(void *context, void *sound, MMActorSoundInfo *info)
{
    const MMSoundState *value = sound;
    (void)context;
    info->style = value->resource.style;
    info->volume = value->resource.volume;
    info->no_sound = value->silent;
}

static int suppress_sound(void *context) { (void)context; return 1; }

static int text_template(void *context)
{
    const DraftEdit *edit = context;
    const MMPreviewActorServices *play = edit->services->playback;
    return play->text(play->context, edit->actor->tmpl);
}

static void mark_dirty(void *context) { ((DraftEdit *)context)->life_dirty = 1; }
static void recording(void *context, int enabled) { ((DraftEdit *)context)->recording.recording = enabled; }

static void default_costume(void *context)
{
    const DraftEdit *edit = context;
    const MMPreviewActorServices *play = edit->services->playback;
    play->default_costume(play->context, edit->actor->tmpl, edit->actor->body);
}

static void pose_inputs(void *context, MMActorPoseInputs *inputs)
{
    const DraftEdit *edit = context;
    const MMPreviewActorServices *play = edit->services->playback;
    unsigned short angles[3] = {0};
    play->rest(play->context, edit->actor->tmpl, angles);
    inputs->restX = angles[0];
    inputs->restY = angles[1];
    inputs->restZ = angles[2];
    inputs->fixedPathOrientation = text_template(context);
}

static void defer_pose(void *context, const MMActorBodyPose *pose) { (void)context; (void)pose; }

static MMI32 distance(void *context, const MMActorPoint *point)
{
    return ((DraftEdit *)context)->services->distance(point);
}

static MMActorRecordServices record_services(DraftEdit *edit)
{
    MMActorRecordServices s = {0};
    MMActorSubrouteEditServices *subroute = &s.subroute;
    MMActorActionEditServices *action = &subroute->action;
    MMActorSoundEditServices *sounds = &action->sounds;
    MMActorHeader *header = &edit->actor->header;
    action->context = sounds->context = sounds->runtime.context = edit;
    sounds->edits = event_edits(edit);
    sounds->runtime.cel_count = cel_count;
    sounds->default_sound = default_sound;
    sounds->runtime.fetch = fetch_sound;
    sounds->runtime.release = release_sound;
    sounds->sound_info = sound_info;
    sounds->sounds_suppressed = suppress_sound;
    sounds->set_sound_frame = sounds->midi_error = ignore;
    action->action_flags = action_flags;
    action->text_template = text_template;
    action->mark_life_dirty = mark_dirty;
    subroute->route.edits = sounds->edits;
    subroute->timeline = edit->services->playback->timeline;
    mm_actor_route_edit_bind_tables(&subroute->route);
    subroute->route.life_dirty = mark_dirty;
    subroute->route.recording = recording;
    subroute->route.hide_body = ignore;
    subroute->route.default_costume = default_costume;
    subroute->lifetime.first = &header->first_frame;
    subroute->lifetime.last = &header->last_frame;
    subroute->lifetime.dirty = &edit->life_dirty;
    subroute->lifetime.recording = &edit->recording.recording;
    subroute->lifetime.rejoining = &edit->recording.rejoining;
    subroute->pose_inputs = pose_inputs;
    subroute->locate_body = defer_pose;
    s.distance = distance;
    s.draft = edit->actor;
    s.draft_playback = edit->services->playback;
    s.stationary_step = 2 * 65536L;
    return s;
}

int mm_preview_actor_write_animation(MMPreviewActor *actor, const MMActorAnimation *request,
                                     MMActorAnimationRange *range, const MMPreviewActorEditServices *s)
{
    DraftEdit edit = {0};
    MMPreviewActorDraft draft = {0};
    MMActorRecordServices record;
    MMActorAnimationRange computed;
    MMI32 actions;
    int result;
    if (!actor->state.onStage || !actor->tmpl || request->point_count > MM_ACTOR_ANIMATION_MAX_POINTS)
        return 0;
    edit.actor = actor;
    edit.services = s;
    result = begin_draft(&edit, &draft);
    if (result <= 0)
        return result;
    record = record_services(&edit);
    actions = request->kind == MM_ACTOR_ANIMATION_ACTION ? mm_preview_actor_action_count(actor) : 0;
    result = mm_actor_animation_edit(actor, s->playback->motion, &edit.recording, request, actions, &record, &computed);
    result = finish_draft(&edit, &draft, result, 1);
    if (result > 0)
        *range = computed;
    return result;
}
