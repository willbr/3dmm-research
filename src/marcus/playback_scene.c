#include "playback_scene.h"
#include "mm/collections.h"

typedef struct PlaybackPause
{
    MMI32 frame, kind, milliseconds;
} PlaybackPause;

static int append(MMCollectionStorage *storage, MMU32 width, MMU32 *count, const void *row)
{
    MMArray array = {0};
    array.storage = storage;
    array.width = width;
    array.count = *count;
    array.free_count = -1;
    storage->min_grow[0] = storage->buffers[0].capacity ? storage->buffers[0].capacity : 8 * width;
    if (mm_array_insert(&array, array.count, row, 1) != MM_STORAGE_OK)
        return -1;
    *count = array.count;
    return 1;
}

void marcus_playback_scene_init(MarcusPlaybackScene *scene)
{
    mm_storage_init(&scene->cues, NULL);
    mm_storage_init(&scene->pauses, NULL);
    mm_storage_init(&scene->actors, NULL);
    scene->cue_count = scene->pause_count = scene->actor_count = 0;
    scene->transition = 0;
}

void marcus_playback_scene_clear(MarcusPlaybackScene *scene)
{
    mm_storage_dispose(&scene->cues);
    mm_storage_dispose(&scene->pauses);
    mm_storage_dispose(&scene->actors);
    scene->cue_count = scene->pause_count = scene->actor_count = 0;
    scene->transition = 0;
}

int marcus_playback_scene_cue(MarcusPlaybackScene *scene, const MMPreviewCue *cue)
{
    return append(&scene->cues, sizeof(*cue), &scene->cue_count, cue);
}

int marcus_playback_scene_pause(MarcusPlaybackScene *scene, MMI32 frame, MMI32 kind, MMI32 milliseconds)
{
    PlaybackPause pause;
    pause.frame = frame;
    pause.kind = kind;
    pause.milliseconds = milliseconds;
    return append(&scene->pauses, sizeof(pause), &scene->pause_count, &pause);
}

void marcus_playback_presentation_clear(MarcusPlaybackPresentation *presentation)
{
    marcus_playback_scene_clear(presentation->sound);
    marcus_caption_scene_clear(presentation->captions);
}

static void reset_presentation(void *p)
{
    marcus_playback_presentation_clear(p);
}

static void presentation_transition(void *p, MMI32 kind)
{
    ((MarcusPlaybackPresentation *)p)->sound->transition = kind;
}

static int presentation_caption(void *p, MMPreviewOwnedCaption *caption)
{
    return marcus_caption_scene_take(((MarcusPlaybackPresentation *)p)->captions, caption);
}

static int presentation_pause(void *p, MMI32 frame, MMI32 kind, MMI32 duration)
{
    return marcus_playback_scene_pause(((MarcusPlaybackPresentation *)p)->sound, frame, kind, duration);
}

static int presentation_cue(void *p, const MMPreviewCue *cue)
{
    return marcus_playback_scene_cue(((MarcusPlaybackPresentation *)p)->sound, cue);
}

MMPreviewOwnedPresentationOutput marcus_playback_presentation_output(MarcusPlaybackPresentation *presentation)
{
    MMPreviewOwnedPresentationOutput out = {
        NULL, reset_presentation, presentation_transition, presentation_caption, presentation_pause, presentation_cue};
    out.context = presentation;
    return out;
}

typedef struct FrameEntry
{
    MarcusPlaybackScene *scene;
    const MarcusPlaybackFrameServices *services;
} FrameEntry;

static int append_actor(void *p, const MMPreviewCue *cue)
{
    MarcusPlaybackScene *scene = p;
    return append(&scene->actors, sizeof(*cue), &scene->actor_count, cue);
}

static int collect_actors(void *p, size_t *count)
{
    FrameEntry *entry = p;
    const MarcusPlaybackFrameServices *services = entry->services;
    if (!services->actors(services->context, entry->scene, append_actor))
        return 0;
    *count = entry->scene->actor_count;
    return 1;
}

static const MMPreviewCue *cue_at(const FrameEntry *entry, int actor, size_t row)
{
    const MMCollectionStorage *storage = actor ? &entry->scene->actors : &entry->scene->cues;
    return (const MMPreviewCue *)storage->buffers[0].data + row;
}

static void read_cue(void *p, int actor, size_t row, MMFrameSoundCue *out)
{
    const MMPreviewCue *cue = cue_at(p, actor, row);
    out->frame = cue->frame;
    out->style = cue->style;
    out->actor = cue->actor;
    out->queued = cue->queue;
    out->motion = cue->motion;
}

static int play_cue(void *p, int actor, size_t row, MMAudioCounter offset)
{
    const FrameEntry *entry = p;
    const MarcusPlaybackFrameServices *services = entry->services;
    return services->play(services->context, cue_at(entry, actor, row), offset);
}

static void read_pause(void *p, size_t row, MMI32 *frame, MMPolledFrameWait *out)
{
    const FrameEntry *entry = p;
    const PlaybackPause *pause = (const PlaybackPause *)entry->scene->pauses.buffers[0].data + row;
    *frame = pause->frame;
    out->kind = pause->kind;
    out->milliseconds = pause->milliseconds;
}

int marcus_playback_scene_enter(MarcusPlaybackScene *scene, MMI32 frame, int restore_music, double fps,
                                const MarcusPlaybackFrameServices *services, MMPolledFrameWait *wait)
{
    FrameEntry entry;
    MMFrameSoundServices reader;
    int status;
    entry.scene = scene;
    entry.services = services;
    reader.context = &entry;
    reader.scene_count = scene->cue_count;
    reader.pause_count = scene->pause_count;
    reader.actor_cues = collect_actors;
    reader.cue = read_cue;
    reader.play = play_cue;
    reader.pause = read_pause;
    status = mm_frame_sound_enter(frame, restore_music, fps, &reader, wait);
    scene->actor_count = 0;
    return status;
}
