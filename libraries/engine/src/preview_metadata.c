#include "mm/preview_metadata.h"
#include "mm/scene_data.h"
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
int mm_preview_actor_timeline(const MMPreviewActor *p, MMI32 first, MMI32 last, const MMPreviewTimelineServices *s)
{
    const MMActorMotionServices *m = &s->timeline.motion;
    void *owner = 0, *route = 0, *events = 0;
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
    r = s->clone(s->context, &owner, &route, &events);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
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
                r = s->apply(s->context, &cursor, owner, command.eventIndex);
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
    if (owner)
        s->release(s->context, owner);
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
int mm_preview_presentation(MMU32 scene, int sound_sheet, int include_captions, const MMPreviewPresentationServices *s)
{
    MMPreviewCueGroup group, actor_events;
    MMPreviewCue cue;
    MMActorHeader header;
    MMActorSound sound;
    MMI32 transition, kind, frame, actor_kind, actor_frame, i, count, volume, style, loop, pause_kind, duration;
    MMU32 row, actor_row, child, slot, offset;
    int start, r, result = 0;
    memset(&group, 0, sizeof(group));
    memset(&actor_events, 0, sizeof(actor_events));
    s->reset(s->context);
    r = s->header(s->context, scene, &transition);
    if (r <= 0)
        return r;
    s->transition(s->context, transition);
    for (start = 1; start >= 0; --start)
    {
        r = s->group(s->context, scene, start, &group);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        for (row = 0; row < group.count; ++row)
        {
            s->event(s->context, &group, row, &kind, &frame);
            if (sound_sheet && kind == MM_SCENE_ADD_ACTOR)
            {
                slot = s->word(s->context, &group, row, 0);
                r = s->child(s->context, scene, slot, 1, &child);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                r = s->actor_header(s->context, child, &header);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                if (header.first_frame == 2147483647)
                    continue;
                r = s->group(s->context, child, MM_PREVIEW_CUE_ACTOR, &actor_events);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                for (actor_row = 0; actor_row < actor_events.count; ++actor_row)
                {
                    s->event(s->context, &actor_events, actor_row, &actor_kind, &actor_frame);
                    if (actor_kind != MM_ACTOR_SOUND)
                        continue;
                    s->actor_sound(s->context, &actor_events, actor_row, &sound);
                    mm_preview_actor_cue(&cue, &sound, actor_frame, header.actor_id);
                    r = s->cue(s->context, &cue);
                    if (r <= 0)
                    {
                        result = r;
                        goto done;
                    }
                }
                s->release(s->context, &actor_events);
            }
            else if ((!sound_sheet || include_captions) && kind == MM_SCENE_ADD_TEXT)
            {
                slot = s->word(s->context, &group, row, 0);
                r = s->child(s->context, scene, slot, 0, &child);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                r = s->caption(s->context, child);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
            }
            else if (kind == MM_SCENE_PAUSE)
            {
                pause_kind = mm_signed_word(s->word(s->context, &group, row, 0));
                duration = mm_signed_word(s->word(s->context, &group, row, 1));
                if (pause_kind < 0 || pause_kind >= 4 || duration < 0)
                    goto done;
                r = s->pause(s->context, frame, pause_kind, duration);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
            }
            else if (kind == MM_SCENE_PLAY_SOUND)
            {
                volume = mm_signed_word(s->word(s->context, &group, row, 0));
                style = mm_signed_word(s->word(s->context, &group, row, 1));
                loop = mm_signed_word(s->word(s->context, &group, row, 2));
                count = mm_signed_word(s->word(s->context, &group, row, 3));
                for (i = 0; i < count; ++i)
                {
                    memset(&cue, 0, sizeof(cue));
                    cue.frame = frame;
                    cue.style = style;
                    cue.volume = volume;
                    cue.loop = loop != 0;
                    cue.queue = i != 0;
                    cue.motion_cel = -1;
                    offset = 4 + (MMU32)i * 5;
                    cue.child = s->word(s->context, &group, row, offset);
                    cue.source = mm_signed_word(s->word(s->context, &group, row, offset + 1));
                    cue.type = s->word(s->context, &group, row, offset + 3);
                    cue.number = s->word(s->context, &group, row, offset + 4);
                    r = s->cue(s->context, &cue);
                    if (r <= 0)
                    {
                        result = r;
                        goto done;
                    }
                }
            }
        }
        s->release(s->context, &group);
    }
    result = 1;
done:
    if (actor_events.handle)
        s->release(s->context, &actor_events);
    if (group.handle)
        s->release(s->context, &group);
    return result;
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
