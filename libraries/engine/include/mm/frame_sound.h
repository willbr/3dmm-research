#ifndef MM_FRAME_SOUND_H
#define MM_FRAME_SOUND_H
#include "audio_clock.h"
#include "collection_storage.h"
#include "playback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMFrameSoundCue
{
    MMI32 frame, style, actor;
    int queued, motion;
} MMFrameSoundCue;
typedef struct MMFrameSoundServices
{
    void *context;
    size_t scene_count, pause_count;
    int (*actor_cues)(void *, size_t *count);
    void (*cue)(void *, int actor, size_t index, MMFrameSoundCue *);
    int (*play)(void *, int actor, size_t index, MMAudioCounter offset);
    void (*pause)(void *, size_t index, MMI32 *frame, MMPolledFrameWait *);
} MMFrameSoundServices;

/* Synchronous callbacks borrow authored rows and do not unwind through C.
 * A failed play leaves the completed prefix and skips later callbacks. Actor
 * cues are requested only after playing the scene cues. The last authored
 * pause at the frame supplies the wait, if present. */
int mm_frame_sound_enter(MMI32 frame, int restore_music, double fps,
                         const MMFrameSoundServices *, MMPolledFrameWait *);
/* Round a timeline position to the nearest frame of 48 kHz PCM. */
MMAudioCounter mm_frame_sound_resume_samples(double seconds);

typedef struct MMFrameSoundRequest
{
    void *sound;
    MMI32 style, object, volume, priority;
    MMU32 offset;
    int loop, queued, actor;
} MMFrameSoundRequest;

typedef struct MMFrameSoundOutput
{
    void *context;
    void *(*create_clock)(void *);
    void (*release_clock)(void *, void *clock), (*start_clock)(void *, void *clock), (*stop_clock)(void *, void *clock);
    int (*set_alarm)(void *, void *clock, MMI32 interval);
    void (*begin)(void *), (*end)(void *);
    void (*play)(void *, const MMFrameSoundRequest *);
    void (*stop_class)(void *, MMI32 sound_class);
    int (*playing_class)(void *, MMI32 sound_class);
} MMFrameSoundOutput;

typedef struct MMFrameSoundQueue
{
    MMCollectionStorage storage;
    MMU32 count;
    void (*retain)(void *), (*release)(void *);
    MMI32 duration;
    int trash_unused, initialized;
    void *clock;
    MMFrameSoundOutput output;
} MMFrameSoundQueue;
enum
{
    MM_FRAME_SOUND_EMPTY,
    MM_FRAME_SOUND_FLUSH,
    MM_FRAME_SOUND_PLAY,
    MM_FRAME_SOUND_OFF = 0,
    MM_FRAME_SOUND_SHORT = MM_PLAYBACK_TICKS_PER_SECOND * 2 * 10 / 12,
    MM_FRAME_SOUND_LONG = 2147483647
};

/* The initialized queue owns each pending reference. Enqueue borrows the
 * supplied request for the call. Dequeue transfers the reference. The
 * legacy queue number determines conflicts for Studio, including its signed
 * low word. A low-priority nonqueued conflict is ignored, a normal
 * nonqueued conflict replaces the first stored match. Callbacks may not
 * reenter the queue. Prepare a fresh record before initializing storage.
 * Initialization can be retried after failure and is idempotent on success. */
void mm_frame_sound_queue_prepare(MMFrameSoundQueue *);
int mm_frame_sound_queue_init(MMFrameSoundQueue *, const MMStorageAllocator *,
                              void (*retain)(void *), void (*release)(void *), int trash_unused);
void mm_frame_sound_queue_dispose(MMFrameSoundQueue *);
int mm_frame_sound_queue_enqueue(MMFrameSoundQueue *, const MMFrameSoundRequest *, int low);
int mm_frame_sound_queue_dequeue(MMFrameSoundQueue *, MMFrameSoundRequest *);
void mm_frame_sound_queue_clear(MMFrameSoundQueue *);
const MMFrameSoundRequest *mm_frame_sound_queue_at(const MMFrameSoundQueue *, MMU32 index);
int mm_frame_sound_queue_dispatch(const MMFrameSoundQueue *);
int mm_frame_sound_queue_timeout(const MMFrameSoundQueue *, MMI32 *interval);

/* The queue copies the output binding and borrows its context until close.
 * The host creates a clock after storage is initialized. Failed open leaves
 * the prepared queue safe to close or retry. Close stops outputs, releases
 * pending references and the clock, then disposes storage and drops the binding.
 * Output callbacks may observe the queue but must not reenter it. Play and alarm
 * handling require an open clock. Play submits one synchronized batch. A long
 * batch preserves any existing preview alarm. */
int mm_frame_sound_queue_open(MMFrameSoundQueue *, const MMStorageAllocator *,
                              void (*retain)(void *), void (*release)(void *), int trash_unused,
                              const MMFrameSoundOutput *);
/* Open a queue whose requests carry retained MMSoundOwner references. */
int mm_frame_sound_queue_open_owners(MMFrameSoundQueue *, const MMStorageAllocator *, int trash_unused,
                                     const MMFrameSoundOutput *);
void mm_frame_sound_queue_close(MMFrameSoundQueue *);
void mm_frame_sound_queue_play(MMFrameSoundQueue *);
void mm_frame_sound_queue_stop(const MMFrameSoundQueue *);
void mm_frame_sound_queue_alarm(const MMFrameSoundQueue *);
int mm_frame_sound_queue_playing(const MMFrameSoundQueue *, int loop);
void mm_frame_sound_stop_classes(void *, void (*stop_class)(void *, MMI32));

#ifdef __cplusplus
}
#endif
#endif
