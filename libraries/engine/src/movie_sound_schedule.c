#include "mm/movie_sound_schedule.h"
#include "mm/playback.h"
#include "mm/movie_audio.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct FrameHold
{
    MMI32 frame;
    double seconds;
} FrameHold;
typedef struct ScheduleScene
{
    MMMovieTimingScene source;
    double start;
    FrameHold *holds;
    size_t hold_count;
    size_t *sounds;
    size_t sound_count;
} ScheduleScene;
typedef struct ScheduleVoice
{
    MMScheduledSound span;
    const MMMovieTimingCue *cue;
} ScheduleVoice;
struct MMMovieSoundSchedule
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
enum EventKind { EVENT_CUE, EVENT_SCROLL, EVENT_PAUSE };

typedef struct EventOrder
{
    MMI32 frame;
    enum EventKind kind;
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

static void dispose_scene(ScheduleScene *scene)
{
    free((void *)scene->source.scrolls);
    free((void *)scene->source.pauses);
    free((void *)scene->source.cues);
    free(scene->holds);
    free(scene->sounds);
}

static void *copy_rows(const void *source, size_t count, size_t size)
{
    void *copy = rows(count, size);
    if (copy)
        memcpy(copy, source, count * size);
    return copy;
}

static int copy_source(MMMovieTimingScene *out, const MMMovieTimingScene *source)
{
    *out = *source;
    out->scrolls = (MMMovieTimingScroll *)copy_rows(source->scrolls, source->scroll_count, sizeof(*source->scrolls));
    out->pauses = (MMMovieTimingPause *)copy_rows(source->pauses, source->pause_count, sizeof(*source->pauses));
    out->cues = (MMMovieTimingCue *)copy_rows(source->cues, source->cue_count, sizeof(*source->cues));
    return (!source->scroll_count || out->scrolls) && (!source->pause_count || out->pauses) &&
        (!source->cue_count || out->cues);
}

MMMovieSoundSchedule *mm_movie_sound_schedule_create(double frame_seconds)
{
    MMMovieSoundSchedule *schedule;
    schedule = (MMMovieSoundSchedule *)calloc(1, sizeof(*schedule));
    if (schedule)
        schedule->frame_seconds = frame_seconds;
    return schedule;
}

void mm_movie_sound_schedule_clear(MMMovieSoundSchedule *schedule)
{
    MMMovieSoundSchedule empty = {0};
    size_t i;
    for (i = 0; i < schedule->scene_count; ++i)
        dispose_scene(&schedule->scenes[i]);
    free(schedule->scenes);
    free(schedule->voices);
    free(schedule->active);
    empty.frame_seconds = schedule->frame_seconds;
    *schedule = empty;
}

void mm_movie_sound_schedule_destroy(MMMovieSoundSchedule *schedule)
{
    if (schedule)
    {
        mm_movie_sound_schedule_clear(schedule);
        free(schedule);
    }
}

void mm_movie_sound_schedule_rate(MMMovieSoundSchedule *schedule, double fps)
{
    mm_movie_sound_schedule_clear(schedule);
    /* Uncapped playback depends on host throughput. Use a nominal six FPS. */
    schedule->frame_seconds = fps > 0 ? 1.0 / fps : 1.0 / 6;
}

double mm_movie_sound_schedule_frame_seconds(const MMMovieSoundSchedule *schedule)
{
    return schedule->frame_seconds;
}

static double time_at(const ScheduleScene *scene, double frame_seconds, double frame)
{
    double result, portion;
    size_t i;
    result = scene->start + (frame - scene->source.range.first) * frame_seconds;
    for (i = 0; i < scene->hold_count; ++i)
    {
        if (frame <= scene->holds[i].frame)
            break;
        portion = frame - scene->holds[i].frame;
        result += (portion < 1.0 ? portion : 1.0) * scene->holds[i].seconds;
    }
    return result;
}

double mm_movie_sound_schedule_time_at(const MMMovieSoundSchedule *schedule, size_t scene, double frame)
{
    return time_at(&schedule->scenes[scene], schedule->frame_seconds, frame);
}

double mm_movie_sound_schedule_frame_at(const MMMovieSoundSchedule *schedule, size_t index, double seconds)
{
    const ScheduleScene *scene;
    const FrameHold *hold;
    double frame, at, entry, duration;
    size_t i;
    scene = &schedule->scenes[index];
    frame = scene->source.range.first;
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

static int same_lane(const MMMovieTimingCue *a, const MMMovieTimingCue *b)
{
    int actor = (a->flags & MM_TIMING_ACTOR_SOUND) != 0;
    return actor == ((b->flags & MM_TIMING_ACTOR_SOUND) != 0) &&
        (!actor || a->actor == b->actor) && a->style == b->style;
}

static int same_audio(const MMMovieTimingCue *a, const MMMovieTimingCue *b)
{
    return a->resolved_type == b->resolved_type && a->resolved_chunk == b->resolved_chunk &&
           a->resolved_source == b->resolved_source;
}

static int finite_time(double time)
{
    return time >= -DBL_MAX && time <= DBL_MAX;
}

static void expire(MMMovieSoundSchedule *schedule, double at)
{
    size_t i, kept;
    kept = 0;
    for (i = 0; i < schedule->active_count; ++i)
        if (!(schedule->voices[schedule->active[i]].span.end <= at))
            schedule->active[kept++] = schedule->active[i];
    schedule->active_count = kept;
}

static void enter_scene(MMMovieSoundSchedule *schedule)
{
    size_t i, prior;
    ScheduleVoice *voice, *other;
    double at;
    at = schedule->time;
    for (i = 0; i < schedule->active_count; ++i)
    {
        voice = &schedule->voices[schedule->active[i]];
        if (mm_movie_audio_cut_stops((voice->cue->flags & MM_TIMING_LOOP), voice->cue->audio_kind == MM_SOUND_CLIP_MIDI) && at < voice->span.end)
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
            if (same_lane(other->cue, voice->cue) && other->span.end > voice->span.start)
                voice->span.start = other->span.end;
        }
        voice->span.end = (voice->cue->flags & MM_TIMING_LOOP) ? HUGE_VAL : voice->span.start + voice->cue->seconds;
    }
    schedule->time += schedule->transition;
    expire(schedule, schedule->time);
}

static void add_cue(MMMovieSoundSchedule *schedule, ScheduleScene *scene, size_t row, double at)
{
    const MMMovieTimingCue *cue = &scene->source.cues[row];
    ScheduleVoice *voice, *other;
    size_t i, kept, number;
    int playable, queued;
    number = schedule->voice_count++;
    voice = &schedule->voices[number];
    voice->cue = cue;
    voice->span.scene = schedule->scene_count;
    voice->span.source_row = row;
    voice->span.start = voice->span.end = at;
    scene->sounds[scene->sound_count++] = number;
    if (cue->audio_kind == MM_SOUND_CLIP_MIDI)
    {
        for (i = 0; i < schedule->active_count; ++i)
        {
            other = &schedule->voices[schedule->active[i]];
            /* Keep the authored marker while the original music voice plays. */
            if (other->cue->audio_kind == MM_SOUND_CLIP_MIDI && same_lane(other->cue, cue) && same_audio(other->cue, cue))
                return;
        }
    }
    playable = cue->audio_kind == MM_SOUND_CLIP_SAMPLED || cue->audio_kind == MM_SOUND_CLIP_MIDI;
    queued = (cue->flags & MM_TIMING_QUEUE) && playable && !(cue->seconds <= 0);
    kept = 0;
    for (i = 0; i < schedule->active_count; ++i)
    {
        other = &schedule->voices[schedule->active[i]];
        if (same_lane(other->cue, cue))
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
        voice->span.end = (cue->flags & MM_TIMING_LOOP) && cue->seconds > 0 ? HUGE_VAL : voice->span.start + cue->seconds;
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
    if (a->kind != b->kind)
        return a->kind < b->kind ? -1 : 1;
    return a->row < b->row ? -1 : a->row != b->row;
}

static void schedule_frames(MMMovieSoundSchedule *schedule, ScheduleScene *scene, const EventOrder *order, size_t count)
{
    const EventOrder *event;
    const MMMovieTimingPause *pause;
    const ScheduleVoice *voice;
    double at, hold, tail;
    MMI32 frame;
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
            event = &order[i++];
            switch (event->kind)
            {
            case EVENT_CUE:
                add_cue(schedule, scene, event->row, at);
                break;
            case EVENT_SCROLL:
                hold = scene->source.scrolls[event->row].seconds;
                break;
            case EVENT_PAUSE:
                pause = &scene->source.pauses[event->row];
                break;
            }
        } while (i < count && order[i].frame == frame);
        /* Click waits have no authored duration. */
        if (pause && pause->kind == MM_PLAYBACK_WAIT_TIME)
            hold += pause->milliseconds / 1000.0;
        else if (pause && pause->kind == MM_PLAYBACK_WAIT_SOUND)
        {
            for (j = 0; j < schedule->active_count; ++j)
            {
                voice = &schedule->voices[schedule->active[j]];
                if (!(voice->cue->flags & MM_TIMING_LOOP) && finite_time(voice->span.end))
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

static void add_order(EventOrder *order, size_t *count, MMI32 frame, enum EventKind kind, size_t row)
{
    EventOrder *event = &order[(*count)++];
    event->frame = frame;
    event->kind = kind;
    event->row = row;
}

int mm_movie_sound_schedule_append(MMMovieSoundSchedule *schedule, const MMMovieTimingScene *source)
{
    ScheduleScene scene;
    EventOrder *order;
    const MMScheduledSound *span;
    size_t i, count, ordered, cues, holds, capacity, kept;
    MMI32 first = source->range.first, last = source->range.last, frame;
    double finish;
    void *grown;
    memset(&scene, 0, sizeof(scene));
    if (source->scroll_count > (size_t)-1 - source->cue_count ||
        source->pause_count > (size_t)-1 - source->cue_count - source->scroll_count ||
        schedule->scene_count == (size_t)-1)
        return 0;
    count = source->scroll_count + source->pause_count + source->cue_count;
    order = (EventOrder *)rows(count, sizeof(*order));
    if (count && !order)
        return 0;
    ordered = 0;
    for (i = 0; i < source->cue_count; ++i)
    {
        frame = source->cues[i].frame;
        if (frame <= last)
            add_order(order, &ordered, frame < first ? first : frame, EVENT_CUE, i);
    }
    cues = ordered;
    for (i = 0; i < source->scroll_count; ++i)
    {
        frame = source->scrolls[i].frame;
        if (frame >= first && frame <= last)
            add_order(order, &ordered, frame, EVENT_SCROLL, i);
    }
    for (i = 0; i < source->pause_count; ++i)
    {
        frame = source->pauses[i].frame;
        if (frame >= first && frame <= last)
            add_order(order, &ordered, frame, EVENT_PAUSE, i);
    }
    holds = ordered - cues;
    if (cues > (size_t)-1 - schedule->voice_count || cues > (size_t)-1 - schedule->active_count)
        goto failed;
    if (!copy_source(&scene.source, source))
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
    scene.start = schedule->time;
    scene.sound_count = schedule->active_count;
    if (scene.sound_count)
        memcpy(scene.sounds, schedule->active, scene.sound_count * sizeof(*scene.sounds));
    schedule_frames(schedule, &scene, order, ordered);
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
    schedule->transition = source->transition;
    free(order);
    return 1;

failed:
    free(order);
    dispose_scene(&scene);
    return 0;
}

size_t mm_movie_sound_schedule_size(const MMMovieSoundSchedule *schedule)
{
    return schedule->scene_count;
}

void mm_movie_sound_schedule_source(const MMMovieSoundSchedule *schedule, size_t scene, MMMovieTimingScene *source)
{
    *source = schedule->scenes[scene].source;
}

int mm_movie_sound_schedule_reframe(const MMMovieSoundSchedule *schedule, const MMMovieTimingRange *ranges,
                                     size_t count, MMMovieSoundSchedule **out)
{
    MMMovieSoundSchedule *replacement;
    MMMovieTimingScene source;
    size_t i;
    if (count > schedule->scene_count)
        count = schedule->scene_count;
    for (i = 0; i < count; ++i)
        if (ranges[i].chunk != schedule->scenes[i].source.range.chunk)
            return MM_SOUND_REFRAME_MISMATCH;
    replacement = mm_movie_sound_schedule_create(schedule->frame_seconds);
    if (!replacement)
        return MM_SOUND_REFRAME_MEMORY;
    for (i = 0; i < count; ++i)
    {
        source = schedule->scenes[i].source;
        source.range = ranges[i];
        if (!mm_movie_sound_schedule_append(replacement, &source))
        {
            mm_movie_sound_schedule_destroy(replacement);
            return MM_SOUND_REFRAME_MEMORY;
        }
    }
    *out = replacement;
    return MM_SOUND_REFRAME_OK;
}

size_t mm_movie_sound_schedule_sound_count(const MMMovieSoundSchedule *schedule, size_t scene)
{
    return schedule->scenes[scene].sound_count;
}

MMScheduledSound mm_movie_sound_schedule_sound(const MMMovieSoundSchedule *schedule, size_t scene, size_t index)
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

int mm_movie_sound_schedule_resume(const MMMovieSoundSchedule *schedule, size_t index, double frame,
                                 MMScheduledResume *out, size_t *count)
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
        if (!(voice->cue->flags & MM_TIMING_MOTION) && voice->span.end > at && finite_time(voice->span.start) &&
            (voice->span.scene < index || voice->cue->frame < frame))
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
            if (same_lane(pending[prior].voice->cue, voice->cue))
            {
                out[i].queue = 1;
                break;
            }
    }
    free(pending);
    return 1;
}
