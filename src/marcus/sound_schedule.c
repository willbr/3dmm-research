#include "sound_schedule.h"
#include "player.h"
#include "mm/movie_audio.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct FrameHold
{
    long frame;
    double seconds;
} FrameHold;
typedef struct ScheduleScene
{
    long first;
    double start;
    FrameHold *holds;
    size_t hold_count;
    size_t *sounds;
    size_t sound_count;
} ScheduleScene;
typedef struct ScheduleVoice
{
    MarcusScheduledSound span;
    MarcusScheduledCue cue;
    long frame;
} ScheduleVoice;
struct MarcusSoundSchedule
{
    double frame_seconds, time, transition;
    ScheduleScene *scenes;
    size_t scene_count, scene_capacity;
    /* Scenes and the active list refer to voices by index. Cutting a voice
     * updates its span in every scene where it is displayed. */
    ScheduleVoice *voices;
    size_t voice_count, voice_capacity;
    size_t *active;
    size_t active_count, active_capacity;
};
typedef struct EventOrder
{
    long frame;
    size_t row;
} EventOrder;
typedef struct ResumeOrder
{
    const ScheduleVoice *voice;
    size_t row;
} ResumeOrder;

static void *room(void *buffer, size_t *capacity, size_t count, size_t item_size)
{
    size_t limit, next;
    void *grown;
    if (count <= *capacity)
        return buffer;
    limit = (size_t)-1 / item_size;
    if (count > limit)
        return NULL;
    next = *capacity <= limit / 2 ? *capacity * 2 : limit;
    if (next < count)
        next = count;
    grown = realloc(buffer, next * item_size);
    if (grown)
        *capacity = next;
    return grown;
}

static void *rows(size_t count, size_t size)
{
    if (count == 0 || count > (size_t)-1 / size)
        return NULL;
    return malloc(count * size);
}

MarcusSoundSchedule *marcus_sound_schedule_create(double frame_seconds)
{
    MarcusSoundSchedule *schedule;
    schedule = (MarcusSoundSchedule *)calloc(1, sizeof(*schedule));
    if (schedule)
        schedule->frame_seconds = frame_seconds;
    return schedule;
}

void marcus_sound_schedule_clear(MarcusSoundSchedule *schedule)
{
    MarcusSoundSchedule empty = {0};
    size_t i;
    for (i = 0; i < schedule->scene_count; ++i)
    {
        free(schedule->scenes[i].holds);
        free(schedule->scenes[i].sounds);
    }
    free(schedule->scenes);
    free(schedule->voices);
    free(schedule->active);
    empty.frame_seconds = schedule->frame_seconds;
    *schedule = empty;
}

void marcus_sound_schedule_destroy(MarcusSoundSchedule *schedule)
{
    if (schedule)
    {
        marcus_sound_schedule_clear(schedule);
        free(schedule);
    }
}

void marcus_sound_schedule_rate(MarcusSoundSchedule *schedule, double fps)
{
    marcus_sound_schedule_clear(schedule);
    /* Uncapped playback depends on host throughput. Use a nominal six FPS. */
    schedule->frame_seconds = fps > 0 ? 1.0 / fps : 1.0 / 6;
}

double marcus_sound_schedule_frame_seconds(const MarcusSoundSchedule *schedule)
{
    return schedule->frame_seconds;
}

static double time_at(const ScheduleScene *scene, double frame_seconds, double frame)
{
    double result, portion;
    size_t i;
    result = scene->start + (frame - scene->first) * frame_seconds;
    for (i = 0; i < scene->hold_count; ++i)
    {
        if (frame <= scene->holds[i].frame)
            break;
        portion = frame - scene->holds[i].frame;
        result += (portion < 1.0 ? portion : 1.0) * scene->holds[i].seconds;
    }
    return result;
}

double marcus_sound_schedule_time_at(const MarcusSoundSchedule *schedule, size_t scene, double frame)
{
    return time_at(&schedule->scenes[scene], schedule->frame_seconds, frame);
}

double marcus_sound_schedule_frame_at(const MarcusSoundSchedule *schedule, size_t index, double seconds)
{
    const ScheduleScene *scene;
    const FrameHold *hold;
    double frame, at, entry, duration;
    size_t i;
    scene = &schedule->scenes[index];
    frame = scene->first;
    at = scene->start;
    for (i = 0; i < scene->hold_count; ++i)
    {
        hold = &scene->holds[i];
        entry = at + (hold->frame - frame) * schedule->frame_seconds;
        if (seconds < entry)
            return frame + (seconds - at) / schedule->frame_seconds;
        duration = schedule->frame_seconds + hold->seconds;
        if (seconds < entry + duration)
            return hold->frame + (seconds - entry) / duration;
        frame = (double)hold->frame + 1;
        at = entry + duration;
    }
    return frame + (seconds - at) / schedule->frame_seconds;
}

static int same_lane(const MarcusScheduledCue *a, const MarcusScheduledCue *b)
{
    return a->actor_sound == b->actor_sound && (!a->actor_sound || a->actor == b->actor) && a->style == b->style;
}

static int same_audio(const MarcusScheduledCue *a, const MarcusScheduledCue *b)
{
    return a->resolved_type == b->resolved_type && a->resolved_chunk == b->resolved_chunk &&
           a->resolved_source == b->resolved_source;
}

static int finite_time(double time)
{
    return time >= -DBL_MAX && time <= DBL_MAX;
}

static void expire(MarcusSoundSchedule *schedule, double at)
{
    size_t i, kept;
    kept = 0;
    for (i = 0; i < schedule->active_count; ++i)
        if (!(schedule->voices[schedule->active[i]].span.end <= at))
            schedule->active[kept++] = schedule->active[i];
    schedule->active_count = kept;
}

static void enter_scene(MarcusSoundSchedule *schedule)
{
    size_t i, prior;
    ScheduleVoice *voice, *other;
    double at;
    at = schedule->time;
    for (i = 0; i < schedule->active_count; ++i)
    {
        voice = &schedule->voices[schedule->active[i]];
        if (mm_movie_audio_cut_stops(voice->cue.loop, voice->cue.kind == MARCUS_WAVEFORM_MIDI) && at < voice->span.end)
            voice->span.end = at;
    }
    expire(schedule, at);
    /* Stopping sampled loops can unblock queued sounds in their lanes. */
    for (i = 0; i < schedule->active_count; ++i)
    {
        voice = &schedule->voices[schedule->active[i]];
        if (finite_time(voice->span.start))
            continue;
        voice->span.start = at;
        for (prior = 0; prior < i; ++prior)
        {
            other = &schedule->voices[schedule->active[prior]];
            if (same_lane(&other->cue, &voice->cue) && other->span.end > voice->span.start)
                voice->span.start = other->span.end;
        }
        voice->span.end = voice->cue.loop ? HUGE_VAL : voice->span.start + voice->cue.seconds;
    }
    schedule->time += schedule->transition;
    expire(schedule, schedule->time);
}

static void add_cue(MarcusSoundSchedule *schedule, ScheduleScene *scene, const MarcusSoundEvent *event, double at)
{
    const MarcusScheduledCue *cue;
    ScheduleVoice *voice, *other;
    size_t i, kept, number;
    int playable, queued;
    cue = &event->data.cue;
    number = schedule->voice_count++;
    voice = &schedule->voices[number];
    voice->cue = *cue;
    voice->frame = event->frame;
    voice->span.scene = schedule->scene_count;
    voice->span.source_row = cue->source_row;
    voice->span.start = voice->span.end = at;
    scene->sounds[scene->sound_count++] = number;
    if (cue->kind == MARCUS_WAVEFORM_MIDI)
    {
        for (i = 0; i < schedule->active_count; ++i)
        {
            other = &schedule->voices[schedule->active[i]];
            /* Keep the authored marker while the original music voice plays. */
            if (other->cue.kind == MARCUS_WAVEFORM_MIDI && same_lane(&other->cue, cue) && same_audio(&other->cue, cue))
                return;
        }
    }
    playable = cue->kind == MARCUS_WAVEFORM_SAMPLED || cue->kind == MARCUS_WAVEFORM_MIDI;
    queued = cue->queue && playable && !(cue->seconds <= 0);
    kept = 0;
    for (i = 0; i < schedule->active_count; ++i)
    {
        other = &schedule->voices[schedule->active[i]];
        if (same_lane(&other->cue, cue))
        {
            if (!queued)
            {
                if (at < other->span.end)
                    other->span.end = at;
                continue;
            }
            if (other->span.end > voice->span.start)
                voice->span.start = other->span.end;
        }
        schedule->active[kept++] = schedule->active[i];
    }
    schedule->active_count = kept;
    if (playable)
        voice->span.end = cue->loop && cue->seconds > 0 ? HUGE_VAL : voice->span.start + cue->seconds;
    else
        voice->span.end = voice->span.start;
    if (playable && cue->seconds > 0)
        schedule->active[schedule->active_count++] = number;
}

static int event_order(const void *left, const void *right)
{
    const EventOrder *a, *b;
    a = (const EventOrder *)left;
    b = (const EventOrder *)right;
    if (a->frame != b->frame)
        return a->frame < b->frame ? -1 : 1;
    return a->row < b->row ? -1 : a->row != b->row;
}

static void schedule_frames(MarcusSoundSchedule *schedule, ScheduleScene *scene, const MarcusSoundEvent *events,
                            const EventOrder *order, size_t count)
{
    const MarcusSoundEvent *event, *pause;
    const ScheduleVoice *voice;
    double at, hold, tail;
    long frame;
    size_t i, j;
    i = 0;
    while (i < count)
    {
        frame = order[i].frame;
        at = time_at(scene, schedule->frame_seconds, frame);
        expire(schedule, at);
        hold = 0;
        pause = NULL;
        do
        {
            event = &events[order[i++].row];
            switch (event->kind)
            {
            case MARCUS_SCHEDULE_CUE:
                add_cue(schedule, scene, event, at);
                break;
            case MARCUS_SCHEDULE_SCROLL:
                hold = event->data.scroll;
                break;
            case MARCUS_SCHEDULE_PAUSE:
                pause = event;
                break;
            }
        } while (i < count && order[i].frame == frame);
        /* Click waits have no authored duration. */
        if (pause && pause->data.pause.kind == 3)
            hold += pause->data.pause.milliseconds / 1000.0;
        else if (pause && pause->data.pause.kind == 2)
        {
            for (j = 0; j < schedule->active_count; ++j)
            {
                voice = &schedule->voices[schedule->active[j]];
                if (!voice->cue.loop && finite_time(voice->span.end))
                {
                    tail = voice->span.end - at;
                    if (tail > hold)
                        hold = tail;
                }
            }
        }
        if (hold > 0)
        {
            scene->holds[scene->hold_count].frame = frame;
            scene->holds[scene->hold_count++].seconds = hold;
        }
    }
}

int marcus_sound_schedule_append(MarcusSoundSchedule *schedule, long first, long last, double transition,
                                 const MarcusSoundEvent *events, size_t count)
{
    ScheduleScene scene;
    EventOrder *order;
    const MarcusScheduledSound *span;
    size_t i, ordered, cues, holds, capacity, kept;
    double finish;
    void *grown;
    memset(&scene, 0, sizeof(scene));
    order = (EventOrder *)rows(count, sizeof(*order));
    if (count && !order)
        return 0;
    ordered = cues = holds = 0;
    for (i = 0; i < count; ++i)
    {
        if (events[i].frame > last || (events[i].frame < first && events[i].kind != MARCUS_SCHEDULE_CUE))
            continue;
        order[ordered].frame = events[i].frame < first ? first : events[i].frame;
        order[ordered++].row = i;
        if (events[i].kind == MARCUS_SCHEDULE_CUE)
            ++cues;
        else
            ++holds;
    }
    if (cues > (size_t)-1 - schedule->voice_count || cues > (size_t)-1 - schedule->active_count)
        goto failed;
    capacity = schedule->active_count + cues;
    scene.holds = (FrameHold *)rows(holds, sizeof(*scene.holds));
    scene.sounds = (size_t *)rows(capacity, sizeof(*scene.sounds));
    if ((holds && !scene.holds) || (capacity && !scene.sounds))
        goto failed;
    grown = room(schedule->scenes, &schedule->scene_capacity, schedule->scene_count + 1, sizeof(*schedule->scenes));
    if (!grown)
        goto failed;
    schedule->scenes = (ScheduleScene *)grown;
    grown = room(schedule->voices, &schedule->voice_capacity, schedule->voice_count + cues, sizeof(*schedule->voices));
    if (schedule->voice_count + cues && !grown)
        goto failed;
    schedule->voices = (ScheduleVoice *)grown;
    grown = room(schedule->active, &schedule->active_capacity, capacity, sizeof(*schedule->active));
    if (capacity && !grown)
        goto failed;
    schedule->active = (size_t *)grown;
    if (ordered > 1)
        qsort(order, ordered, sizeof(*order), event_order);

    enter_scene(schedule);
    scene.first = first;
    scene.start = schedule->time;
    scene.sound_count = schedule->active_count;
    if (scene.sound_count)
        memcpy(scene.sounds, schedule->active, scene.sound_count * sizeof(*scene.sounds));
    schedule_frames(schedule, &scene, events, order, ordered);
    finish = time_at(&scene, schedule->frame_seconds, (double)last + 1);
    kept = 0;
    for (i = 0; i < scene.sound_count; ++i)
    {
        span = &schedule->voices[scene.sounds[i]].span;
        if (span->scene == schedule->scene_count || !(span->end <= scene.start || span->start >= finish))
            scene.sounds[kept++] = scene.sounds[i];
    }
    scene.sound_count = kept;
    schedule->scenes[schedule->scene_count++] = scene;
    schedule->time = finish;
    schedule->transition = transition;
    free(order);
    return 1;

failed:
    free(order);
    free(scene.holds);
    free(scene.sounds);
    return 0;
}

size_t marcus_sound_schedule_sound_count(const MarcusSoundSchedule *schedule, size_t scene)
{
    return schedule->scenes[scene].sound_count;
}

MarcusScheduledSound marcus_sound_schedule_sound(const MarcusSoundSchedule *schedule, size_t scene, size_t index)
{
    return schedule->voices[schedule->scenes[scene].sounds[index]].span;
}

static int resume_order(const void *left, const void *right)
{
    const ResumeOrder *a, *b;
    a = (const ResumeOrder *)left;
    b = (const ResumeOrder *)right;
    if (a->voice->span.start != b->voice->span.start)
        return a->voice->span.start < b->voice->span.start ? -1 : 1;
    return a->row < b->row ? -1 : a->row != b->row;
}

int marcus_sound_schedule_resume(const MarcusSoundSchedule *schedule, size_t index, long frame,
                                 MarcusScheduledResume *out, size_t *count)
{
    const ScheduleScene *scene;
    const ScheduleVoice *voice;
    ResumeOrder *pending;
    size_t i, prior;
    double at;
    scene = &schedule->scenes[index];
    *count = 0;
    pending = (ResumeOrder *)rows(scene->sound_count, sizeof(*pending));
    if (scene->sound_count && !pending)
        return 0;
    at = time_at(scene, schedule->frame_seconds, frame);
    for (i = 0; i < scene->sound_count; ++i)
    {
        voice = &schedule->voices[scene->sounds[i]];
        if (!voice->cue.motion && voice->span.end > at && finite_time(voice->span.start) &&
            (voice->span.scene < index || voice->frame < frame))
        {
            pending[*count].voice = voice;
            pending[(*count)++].row = i;
        }
    }
    if (*count > 1)
        qsort(pending, *count, sizeof(*pending), resume_order);
    for (i = 0; i < *count; ++i)
    {
        voice = pending[i].voice;
        out[i].scene = voice->span.scene;
        out[i].source_row = voice->span.source_row;
        out[i].seconds = at > voice->span.start ? at - voice->span.start : 0;
        out[i].queue = 0;
        for (prior = 0; prior < i; ++prior)
            if (same_lane(&pending[prior].voice->cue, &voice->cue))
            {
                out[i].queue = 1;
                break;
            }
    }
    free(pending);
    return 1;
}
