#include "mm/frame_sound.h"
#include "mm/actor_sound.h"
#include "mm/scene_events.h"
#include "mm/sound_resource.h"
#include <math.h>
#include <string.h>

static MMAudioCounter pcm_position(double frames)
{
    MMU32 high;
    if (!(frames > 0))
        return mm_audio_counter(0, 0);
    if (frames >= 18446744073709551616.0)
        return mm_audio_counter(0xffffffffUL, 0xffffffffUL);
    high = (MMU32)(frames / 4294967296.0);
    return mm_audio_counter((MMU32)(frames - (double)high * 4294967296.0), high);
}

MMAudioCounter mm_frame_sound_resume_samples(double seconds)
{
    return pcm_position(floor(seconds * 48000 + .5));
}

static int restore(MMI32 frame, double fps, const MMFrameSoundServices *s)
{
    MMFrameSoundCue cue;
    MMI32 first;
    MMAudioCounter offset, zero = {0, 0};
    size_t i;
    int found;
    found = 0;
    first = 0;
    for (i = 0; i < s->scene_count; ++i)
    {
        s->cue(s->context, 0, i, &cue);
        if (cue.style == MM_SCENE_EVENT_MIDI && cue.frame < frame && (!found || cue.frame > first))
        {
            first = cue.frame;
            found = 1;
        }
    }
    if (!found)
        return 1;
    offset = pcm_position(((double)frame - first) * (fps > 0 ? 48000 / fps : 8000));
    for (i = 0; i < s->scene_count; ++i)
    {
        s->cue(s->context, 0, i, &cue);
        if (cue.style == MM_SCENE_EVENT_MIDI && cue.frame == first &&
            !s->play(s->context, 0, i, cue.queued ? zero : offset))
            return 0;
    }
    return 1;
}

static int motion_overridden(const MMFrameSoundCue *cue, size_t count, const MMFrameSoundServices *s)
{
    MMFrameSoundCue other;
    size_t i;
    for (i = 0; i < count; ++i)
    {
        s->cue(s->context, 1, i, &other);
        if (!other.motion && other.actor == cue->actor && other.style == cue->style)
            return 1;
    }
    return 0;
}

int mm_frame_sound_enter(MMI32 frame, int restore_music, double fps,
                         const MMFrameSoundServices *s, MMPolledFrameWait *wait)
{
    MMFrameSoundCue cue;
    MMPolledFrameWait pause;
    MMAudioCounter zero = {0, 0};
    MMI32 pause_frame;
    size_t i, actor_count;
    if (restore_music && !restore(frame, fps, s))
        return 0;
    for (i = 0; i < s->scene_count; ++i)
    {
        s->cue(s->context, 0, i, &cue);
        if (cue.frame == frame && !s->play(s->context, 0, i, zero))
            return 0;
    }
    if (!s->actor_cues(s->context, &actor_count))
        return 0;
    for (i = 0; i < actor_count; ++i)
    {
        s->cue(s->context, 1, i, &cue);
        if ((!cue.motion || !motion_overridden(&cue, actor_count, s)) && !s->play(s->context, 1, i, zero))
            return 0;
    }
    for (i = 0; i < s->pause_count; ++i)
    {
        s->pause(s->context, i, &pause_frame, &pause);
        if (pause_frame == frame)
            *wait = pause;
    }
    return 1;
}

static MMArray queue_view(MMFrameSoundQueue *queue)
{
    MMArray view;
    view.storage = &queue->storage;
    view.width = sizeof(MMFrameSoundRequest);
    view.count = queue->count;
    view.free_count = -1;
    view.allocated = 0;
    view.trash_unused = queue->trash_unused;
    return view;
}

void mm_frame_sound_queue_prepare(MMFrameSoundQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
    queue->duration = MM_FRAME_SOUND_SHORT;
}

int mm_frame_sound_queue_init(MMFrameSoundQueue *queue, const MMStorageAllocator *allocator,
                              void (*retain)(void *), void (*release)(void *), int trash_unused)
{
    MMArray view;
    if (queue->initialized)
        return 1;
    queue->count = 0;
    queue->retain = retain;
    queue->release = release;
    queue->trash_unused = trash_unused;
    if (mm_storage_init(&queue->storage, allocator) != MM_STORAGE_OK)
        return 0;
    queue->storage.min_grow[0] = 128;
    view = queue_view(queue);
    if (mm_array_reserve(&view, 10, 0) == MM_STORAGE_OK)
    {
        queue->initialized = 1;
        return 1;
    }
    mm_storage_dispose(&queue->storage);
    return 0;
}

const MMFrameSoundRequest *mm_frame_sound_queue_at(const MMFrameSoundQueue *queue, MMU32 index)
{
    if (index >= queue->count)
        return NULL;
    return (const MMFrameSoundRequest *)queue->storage.buffers[0].data + index;
}

int mm_frame_sound_queue_enqueue(MMFrameSoundQueue *queue, const MMFrameSoundRequest *request, int low)
{
    MMArray view;
    MMFrameSoundRequest value;
    MMFrameSoundRequest *entries;
    void *removed;
    MMU32 i;
    MMI32 number;
    if (!queue || !queue->initialized || !request)
        return 0;
    if (queue->duration == MM_FRAME_SOUND_OFF)
        return 1;
    view = queue_view(queue);
    value = *request;
    if (!value.queued)
    {
        number = mm_sound_queue_number(value.style, value.object, value.actor);
        entries = (MMFrameSoundRequest *)queue->storage.buffers[0].data;
        for (i = 0; i < view.count; ++i)
        {
            if (mm_sound_queue_number(entries[i].style, entries[i].object, entries[i].actor) == number)
            {
                if (low)
                    return 1;
                removed = entries[i].sound;
                queue->retain(value.sound);
                entries[i] = value;
                mm_array_move(&view, i, 0);
                queue->release(removed);
                return 1;
            }
        }
    }
    queue->retain(value.sound);
    if (mm_array_insert(&view, low ? view.count : 0, &value, 1) != MM_STORAGE_OK)
    {
        queue->release(value.sound);
        return 0;
    }
    queue->count = view.count;
    return 1;
}

int mm_frame_sound_queue_dequeue(MMFrameSoundQueue *queue, MMFrameSoundRequest *request)
{
    MMArray view;
    if (!queue || !request || !queue->count)
        return 0;
    view = queue_view(queue);
    if (mm_array_pop(&view, request) != MM_STORAGE_OK)
        return 0;
    queue->count = view.count;
    return 1;
}

void mm_frame_sound_queue_clear(MMFrameSoundQueue *queue)
{
    MMFrameSoundRequest request;
    while (mm_frame_sound_queue_dequeue(queue, &request))
        queue->release(request.sound);
}

void mm_frame_sound_queue_dispose(MMFrameSoundQueue *queue)
{
    if (!queue->initialized)
        return;
    mm_frame_sound_queue_clear(queue);
    mm_storage_dispose(&queue->storage);
    queue->initialized = 0;
}

int mm_frame_sound_queue_dispatch(const MMFrameSoundQueue *queue)
{
    if (!queue || !queue->count)
        return MM_FRAME_SOUND_EMPTY;
    return queue->duration == MM_FRAME_SOUND_OFF ? MM_FRAME_SOUND_FLUSH : MM_FRAME_SOUND_PLAY;
}

int mm_frame_sound_queue_timeout(const MMFrameSoundQueue *queue, MMI32 *interval)
{
    if (queue->duration >= MM_FRAME_SOUND_LONG)
        return 0;
    *interval = queue->duration;
    return 1;
}

int mm_frame_sound_queue_open(MMFrameSoundQueue *queue, const MMStorageAllocator *allocator,
                              void (*retain)(void *), void (*release)(void *), int trash_unused,
                              const MMFrameSoundOutput *s)
{
    if (queue->clock)
        return 1;
    queue->output = *s;
    if (!mm_frame_sound_queue_init(queue, allocator, retain, release, trash_unused))
        return 0;
    queue->clock = queue->output.create_clock(queue->output.context);
    return queue->clock != NULL;
}

static void retain_sound(void *sound) { mm_sound_owner_retain(sound); }
static void release_sound(void *sound) { mm_sound_owner_release(sound); }

int mm_frame_sound_queue_open_owners(MMFrameSoundQueue *queue, const MMStorageAllocator *allocator,
                                     int trash_unused, const MMFrameSoundOutput *output)
{
    return mm_frame_sound_queue_open(queue, allocator, retain_sound, release_sound, trash_unused, output);
}

void mm_frame_sound_stop_classes(void *context, void (*stop_class)(void *, MMI32))
{
    stop_class(context, MM_SOUND_CLASS_NONLOOP);
    stop_class(context, MM_SOUND_CLASS_LOOP_WAVE);
    stop_class(context, MM_SOUND_CLASS_LOOP_MIDI);
}

void mm_frame_sound_queue_stop(const MMFrameSoundQueue *queue)
{
    const MMFrameSoundOutput *s = &queue->output;
    if (queue->clock)
        s->stop_clock(s->context, queue->clock);
    if (s->stop_class)
        mm_frame_sound_stop_classes(s->context, s->stop_class);
}

void mm_frame_sound_queue_alarm(const MMFrameSoundQueue *queue)
{
    const MMFrameSoundOutput *s = &queue->output;
    /* Unregister the delivered clock before the legacy StopAll path. */
    if (queue->clock)
    {
        s->stop_clock(s->context, queue->clock);
        mm_frame_sound_queue_stop(queue);
    }
}

void mm_frame_sound_queue_close(MMFrameSoundQueue *queue)
{
    const MMFrameSoundOutput *s = &queue->output;
    mm_frame_sound_queue_stop(queue);
    mm_frame_sound_queue_clear(queue);
    if (queue->clock)
    {
        s->release_clock(s->context, queue->clock);
        queue->clock = NULL;
    }
    mm_frame_sound_queue_dispose(queue);
    memset(&queue->output, 0, sizeof(queue->output));
}

void mm_frame_sound_queue_play(MMFrameSoundQueue *queue)
{
    const MMFrameSoundOutput *s = &queue->output;
    MMFrameSoundRequest request;
    MMI32 interval;
    int dispatch = mm_frame_sound_queue_dispatch(queue);
    if (dispatch != MM_FRAME_SOUND_PLAY)
    {
        if (dispatch == MM_FRAME_SOUND_FLUSH)
            mm_frame_sound_queue_clear(queue);
        return;
    }
    s->begin(s->context);
    while (mm_frame_sound_queue_dequeue(queue, &request))
    {
        s->play(s->context, &request);
        queue->release(request.sound);
    }
    s->end(s->context);
    if (mm_frame_sound_queue_timeout(queue, &interval))
    {
        s->start_clock(s->context, queue->clock);
        if (!s->set_alarm(s->context, queue->clock, interval))
            mm_frame_sound_queue_stop(queue);
    }
}

int mm_frame_sound_queue_playing(const MMFrameSoundQueue *queue, int loop)
{
    const MMFrameSoundOutput *s = &queue->output;
    return loop ? s->playing_class(s->context, MM_SOUND_CLASS_LOOP_MIDI) ||
                  s->playing_class(s->context, MM_SOUND_CLASS_LOOP_WAVE)
                : s->playing_class(s->context, MM_SOUND_CLASS_NONLOOP);
}
