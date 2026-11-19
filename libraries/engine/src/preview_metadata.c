#include "mm/preview_metadata.h"
#include "mm/movie_session.h"
#include "mm/scene_data.h"
#include "mm/scene_sound.h"
#include <string.h>
static int marker(void *events, MMI32 index, const MMActorEvent *e, const MMActorState *state, MMPreviewMarker *out,
                  const MMActorMotionServices *m)
{
    MMI32 action[2];
    MMActorCostume costume;
    MMActorPoint point;
    memset(out, 0, sizeof(*out));
    out->frame = state->frame;
    switch (e->aet)
    {
    case MM_ACTOR_ADD:
        out->kind = MM_PREVIEW_MARK_ENTRANCE;
        break;
    case MM_ACTOR_REMOVE:
        out->kind = MM_PREVIEW_MARK_EXIT;
        break;
    case MM_ACTOR_ACTION:
        out->kind = MM_PREVIEW_MARK_ACTION;
        m->payload(events, index, action);
        out->values[0] = action[0];
        out->values[1] = action[1];
        break;
    case MM_ACTOR_COSTUME:
        out->kind = MM_PREVIEW_MARK_COSTUME;
        m->payload(events, index, &costume);
        out->values[0] = costume.body_set;
        out->values[1] = costume.custom;
        out->values[2] = costume.costume;
        out->values[3] = costume.tag.sid;
        out->number = costume.tag.number;
        break;
    case MM_ACTOR_ROTATE_FORWARD:
    case MM_ACTOR_ROTATE_HERE:
        out->kind = MM_PREVIEW_MARK_ROTATION;
        out->values[0] = e->aet == MM_ACTOR_ROTATE_HERE;
        break;
    case MM_ACTOR_SIZE:
        out->kind = MM_PREVIEW_MARK_SCALE;
        out->values[0] = state->transform.rScaleStep;
        break;
    case MM_ACTOR_STRETCH:
        out->kind = MM_PREVIEW_MARK_STRETCH;
        out->values[0] = state->transform.aevpull.rScaleX;
        out->values[1] = state->transform.aevpull.rScaleY;
        out->values[2] = state->transform.aevpull.rScaleZ;
        break;
    case MM_ACTOR_MOVE:
    case MM_ACTOR_TWEAK:
        out->kind = e->aet == MM_ACTOR_MOVE ? MM_PREVIEW_MARK_MOVE : MM_PREVIEW_MARK_PATH;
        m->payload(events, index, &point);
        out->values[0] = point.dxr;
        out->values[1] = point.dyr;
        out->values[2] = point.dzr;
        break;
    case MM_ACTOR_STEP:
        out->kind = MM_PREVIEW_MARK_STEP;
        out->values[0] = state->step;
        break;
    case MM_ACTOR_FREEZE:
        out->kind = MM_PREVIEW_MARK_FREEZE;
        out->values[0] = state->frozen;
        break;
    default:
        return 0;
    }
    return 1;
}
int mm_preview_actor_timeline(const MMPreviewActor *p, const MMActorTrack *live, MMI32 first, MMI32 last,
                              const MMPreviewTimelineServices *s)
{
    const MMActorMotionServices *m = &s->timeline.motion;
    MMActorTrack track;
    MMActorPath *route;
    MMActorEventTable *events;
    MMActorState cursor;
    MMActorFrameInputs inputs;
    MMActorFrame sequence;
    MMActorFrameCommand command;
    MMActorEvent event;
    MMPreviewMarker mark;
    MMPreviewSpan span;
    MMI32 frame, previous_action = -1, previous_entrance = -1;
    MMU32 markers = 0, spans = 0;
    MMWide span_end = {0, 0};
    int r, result = 0, frozen = 0;
    s->reset(s->context);
    if (p->inactive || p->header.first_frame > last)
        return 1;
    if ((double)last - p->header.first_frame > 100000)
        return 0;
    mm_actor_track_init(&track);
    if (!mm_actor_track_clone(&track, live, s->tags))
        return 0;
    route = track.route;
    events = track.events;
    mm_actor_state_init(&cursor);
    mm_actor_state_rewind(&cursor, route, m);
    frame = p->header.first_frame;
    for (;;)
    {
        cursor.frame = frame;
        cursor.firstEventInFrame = cursor.nextEvent;
        memset(&inputs, 0, sizeof(inputs));
        inputs.firstFrame = p->header.first_frame;
        inputs.displayFrame = inputs.suppressSounds = 1;
        inputs.stepSucceeded = mm_actor_playback_step(&cursor, &inputs.step, &s->timeline);
        r = s->fault(s->context);
        if (r)
        {
            result = r;
            goto done;
        }
        mm_actor_frame_init(&sequence, &cursor, route, events, &inputs, 1, m);
        for (;;)
        {
            if (!mm_actor_frame_next(&sequence, &cursor, events, &command, m))
                goto done;
            if (command.kind == MM_ACTOR_FRAME_FINISHED)
                break;
            m->event(events, command.eventIndex, &event);
            if (command.kind == MM_ACTOR_CHECK_ENTRANCE)
                r = event.nfrm <= cursor.frame;
            else
            {
                r = s->apply(s->context, &cursor, &track, command.eventIndex);
                if (r < 0)
                {
                    result = r;
                    goto done;
                }
                if (r && cursor.frame >= first)
                {
                    if (markers >= 100000)
                        r = 0;
                    else if (marker(events, command.eventIndex, &event, &cursor, &mark, m))
                    {
                        r = s->marker(s->context, &mark);
                        if (r < 0)
                        {
                            result = r;
                            goto done;
                        }
                        if (r)
                            ++markers;
                    }
                }
            }
            mm_actor_frame_complete(&sequence, &cursor, events, r, m);
        }
        if (!sequence.result.success)
            goto done;
        if (frame >= first && cursor.onStage)
        {
            span.end = mm_wide_add(mm_wide_i32(frame), mm_wide_i32(1));
            if (!spans || mm_wide_compare(span_end, mm_wide_i32(frame)) || previous_action != cursor.actionEvent ||
                previous_entrance != cursor.addEvent || frozen != cursor.frozen)
            {
                span.first = mm_wide_i32(frame);
                span.action = cursor.action;
                span.frozen = cursor.frozen;
                r = s->span(s->context, &span);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                ++spans;
                frozen = cursor.frozen;
            }
            else
                s->extend(s->context, span.end);
            span_end = span.end;
        }
        previous_action = cursor.actionEvent;
        previous_entrance = cursor.addEvent;
        if (frame == last)
            break;
        ++frame;
    }
    result = 1;
done:
    mm_actor_track_dispose(&track, s->tags);
    return result;
}
void mm_preview_actor_cue(MMPreviewCue *out, const MMActorSound *sound, MMI32 frame, MMI32 actor)
{
    memset(out, 0, sizeof(*out));
    out->frame = frame;
    out->actor = actor;
    out->actor_sound = 1;
    out->type = sound->tag.type;
    out->number = sound->tag.number;
    out->source = sound->tag.sid;
    out->child = sound->child;
    out->style = sound->style;
    out->volume = sound->volume;
    out->loop = sound->loop != 0;
    out->queue = sound->queue != 0;
    out->silent = sound->no_sound == 1;
    out->motion = sound->cel != -1;
    out->motion_cel = sound->cel;
}
static int presentation_status(int status)
{
    return status == MM_MOVIE_OK ? 1 : status == MM_MOVIE_MEMORY ? -1 : 0;
}

typedef struct ActorCueOutput
{
    const MMPreviewPresentationServices *services;
    MMI32 actor;
    int result;
} ActorCueOutput;

/* The stream reader validates every row before calling insert. Borrow the
 * output context as its sink and emit sound rows without keeping a track. */
static void *actor_cue_sink(void *context, MMU32 width)
{
    (void)width;
    return context;
}
static void release_cue_sink(void *group) { (void)group; }
static int insert_actor_cue(void *group, MMU32 row, MMU32 bytes, const void *data, const void *fixed)
{
    ActorCueOutput *out = (ActorCueOutput *)group;
    MMActorEvent event;
    MMPreviewCue cue;
    (void)row;
    (void)bytes;
    memcpy(&event, fixed, sizeof(event));
    if (event.aet == MM_ACTOR_SOUND)
    {
        mm_preview_actor_cue(&cue, (const MMActorSound *)data, event.nfrm, out->actor);
        out->result = out->services->cue(out->services->context, &cue);
    }
    return out->result > 0;
}

static int present_actor(const MMMovieSessionGraph *graph, MMU32 number, const MMPreviewPresentationServices *s)
{
    ActorCueOutput output;
    MMActorEventServices events = {0};
    MMActorHeader header;
    void *sink;
    int status = mm_movie_actor_header(graph, number, &header);
    if (status != MM_MOVIE_OK)
        return presentation_status(status);
    if (header.first_frame == 2147483647)
        return 1;
    output.services = s;
    output.actor = header.actor_id;
    output.result = 1;
    events.context = &output;
    events.new_group = actor_cue_sink;
    events.insert = insert_actor_cue;
    events.release = release_cue_sink;
    status = mm_movie_actor_events(graph, number, &events, &sink);
    return output.result > 0 ? presentation_status(status) : output.result;
}

static int present_sound(const MMMovieSceneEvent *event, const MMPreviewPresentationServices *s)
{
    MMPreviewCue cue = {0};
    MMSceneSoundWire sound;
    MMTag tag;
    MMI32 i;
    int result;
    if (!mm_scene_sound_wire(event->data, event->bytes, event->big_endian, &sound))
        return 0;
    cue.frame = event->frame;
    cue.volume = sound.header.vlm;
    cue.style = sound.header.sty;
    cue.loop = sound.header.fLoop != 0;
    cue.motion_cel = -1;
    for (i = 0; i < sound.header.ctagc; ++i)
    {
        cue.queue = i != 0;
        mm_scene_sound_wire_tag(&sound, i, &cue.child, &tag);
        cue.source = tag.sid;
        cue.type = tag.type;
        cue.number = tag.number;
        result = s->cue(s->context, &cue);
        if (result <= 0)
            return result;
    }
    return 1;
}

static int present_group(const MMMovieSessionGraph *graph, const MMChunkAccess *chunks, MMU32 parent,
                         const MMMovieSceneReader *reader, const MMMovieSceneGroup *group, int sound_sheet,
                         int include_captions, const MMPreviewPresentationServices *s)
{
    const MMU32 actor_type = 0x41435452UL, text_type = 0x54424f58UL;
    MMMovieSceneEvent event;
    MMChunkChild child;
    MMU32 row, slot;
    MMI32 kind, duration;
    int actor, result;
    for (row = 0; row < group->count; ++row)
    {
        reader->event(reader->context, group, row, &event);
        actor = sound_sheet && event.kind == MM_SCENE_ADD_ACTOR;
        if (actor || ((!sound_sheet || include_captions) && event.kind == MM_SCENE_ADD_TEXT))
        {
            slot = mm_movie_scene_event_word(&event, 0);
            if (mm_chunk_child_exact(chunks, parent, slot, actor ? actor_type : text_type, &child) != MM_CHUNK_OK)
                return 0;
            result = actor ? present_actor(graph, child.number, s) : s->caption(s->context, child.number);
        }
        else if (event.kind == MM_SCENE_PAUSE)
        {
            kind = mm_signed_word(mm_movie_scene_event_word(&event, 0));
            duration = mm_signed_word(mm_movie_scene_event_word(&event, 4));
            if (kind < 0 || kind >= 4 || duration < 0)
                return 0;
            result = s->pause(s->context, event.frame, kind, duration);
        }
        else if (event.kind == MM_SCENE_PLAY_SOUND)
            result = present_sound(&event, s);
        else
            continue;
        if (result <= 0)
            return result;
    }
    return 1;
}

int mm_preview_presentation(const MMMovieSessionGraph *graph, MMU32 scene, int sound_sheet, int include_captions,
                            const MMPreviewPresentationServices *s)
{
    const MMU32 scene_type = 0x5343454eUL;
    MMMovieSceneSource source;
    MMMovieSceneReader reader;
    MMMovieSessionScene metadata;
    MMMovieSceneGroup group;
    MMChunkAccess chunks;
    MMU32 parent;
    int start, status, result;
    s->reset(s->context);
    source.movie = NULL;
    source.graph = *graph;
    reader = mm_movie_scene_reader(&source);
    status = reader.header(reader.context, scene, &metadata);
    if (status != MM_MOVIE_OK)
        return presentation_status(status);
    s->transition(s->context, metadata.transition);
    graph->access(graph->context, &chunks);
    if (mm_chunk_find(&chunks, scene_type, scene, &parent) != MM_CHUNK_OK)
        return 0;
    for (start = 1; start >= 0; --start)
    {
        status = reader.group(reader.context, scene, start, &group);
        if (status != MM_MOVIE_OK)
            return presentation_status(status);
        result = present_group(graph, &chunks, parent, &reader, &group, sound_sheet, include_captions, s);
        reader.close(reader.context, &group);
        if (result <= 0)
            return result;
    }
    return 1;
}
static int before(const MMPreviewCue *a, const MMPreviewCue *b)
{
    return a->frame < b->frame || (a->frame == b->frame && a->actor_sound < b->actor_sound);
}
static void reverse(const MMPreviewCueOrder *s, MMU32 first, MMU32 end)
{
    MMPreviewCue a, b;
    while (first < end && first < --end)
    {
        s->get(s->context, first, &a);
        s->get(s->context, end, &b);
        s->put(s->context, first++, &b);
        s->put(s->context, end, &a);
    }
}
static void merge_in_place(const MMPreviewCueOrder *s, MMU32 first, MMU32 middle, MMU32 end)
{
    MMU32 a, b, low, high, probe, next_middle;
    MMPreviewCue pivot, value;
    if (first == middle || middle == end)
        return;
    s->get(s->context, middle - 1, &pivot);
    s->get(s->context, middle, &value);
    if (!before(&value, &pivot))
        return;
    if (end - first == 2)
    {
        s->put(s->context, first, &value);
        s->put(s->context, middle, &pivot);
        return;
    }
    if (middle - first > end - middle)
    {
        a = first + (middle - first) / 2;
        s->get(s->context, a, &pivot);
        low = middle;
        high = end;
        while (low < high)
        {
            probe = low + (high - low) / 2;
            s->get(s->context, probe, &value);
            if (before(&value, &pivot))
                low = probe + 1;
            else
                high = probe;
        }
        b = low;
    }
    else
    {
        b = middle + (end - middle) / 2;
        s->get(s->context, b, &pivot);
        low = first;
        high = middle;
        while (low < high)
        {
            probe = low + (high - low) / 2;
            s->get(s->context, probe, &value);
            if (!before(&pivot, &value))
                low = probe + 1;
            else
                high = probe;
        }
        a = low;
    }
    next_middle = a + (b - middle);
    reverse(s, a, middle);
    reverse(s, middle, b);
    reverse(s, a, b);
    merge_in_place(s, first, a, next_middle);
    merge_in_place(s, next_middle, b, end);
}
void mm_preview_cues_sort(MMU32 scene_type, MMU32 scene_number, const MMPreviewCueOrder *s)
{
    MMCollectionStorage scratch;
    MMPreviewCue value, *a, *b, *swap;
    MMU32 i, width, first, middle, end, left, right, out;
    for (i = 0; i < s->count; ++i)
    {
        s->get(s->context, i, &value);
        value.scene_type = scene_type;
        value.scene_number = scene_number;
        s->put(s->context, i, &value);
    }
    if (s->count < 2)
        return;
    mm_storage_init(&scratch, s->allocator);
    if (s->count >= MM_STORAGE_LIMIT / sizeof(value) ||
        mm_storage_ensure(&scratch, s->count * sizeof(value), s->count * sizeof(value), 0) != MM_STORAGE_OK)
    {
        for (width = 1; width < s->count;)
        {
            for (first = 0; first < s->count; first = end)
            {
                middle = width < s->count - first ? first + width : s->count;
                end = width < s->count - middle ? middle + width : s->count;
                merge_in_place(s, first, middle, end);
            }
            if (width > s->count / 2)
                break;
            width *= 2;
        }
        mm_storage_dispose(&scratch);
        return;
    }
    a = (MMPreviewCue *)scratch.buffers[0].data;
    b = (MMPreviewCue *)scratch.buffers[1].data;
    for (i = 0; i < s->count; ++i)
        s->get(s->context, i, a + i);
    for (width = 1; width < s->count;)
    {
        for (first = 0; first < s->count; first = end)
        {
            middle = width < s->count - first ? first + width : s->count;
            end = width < s->count - middle ? middle + width : s->count;
            left = first;
            right = middle;
            for (out = first; out < end; ++out)
                b[out] = left < middle && (right == end || !before(a + right, a + left)) ? a[left++] : a[right++];
        }
        swap = a;
        a = b;
        b = swap;
        if (width > s->count / 2)
            break;
        width *= 2;
    }
    for (i = 0; i < s->count; ++i)
        s->put(s->context, i, a + i);
    mm_storage_dispose(&scratch);
}
