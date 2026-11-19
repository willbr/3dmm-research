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
typedef struct MMFrameSoundQueue
{
    MMCollectionStorage storage;
    MMU32 count;
    void (*retain)(void *), (*release)(void *);
    int trash_unused;
} MMFrameSoundQueue;
enum
{
    MM_FRAME_SOUND_EMPTY,
    MM_FRAME_SOUND_FLUSH,
    MM_FRAME_SOUND_PLAY,
    MM_FRAME_SOUND_OFF = 0,
    MM_FRAME_SOUND_LONG = 2147483647
};

/* The initialized queue owns each pending reference. Enqueue borrows the
 * supplied request for the call. Dequeue transfers the reference. The
 * legacy queue number determines conflicts for Studio, including its signed
 * low word. A low-priority nonqueued conflict is ignored, a normal
 * nonqueued conflict replaces the first stored match. Callbacks may not
 * reenter the queue. */
int mm_frame_sound_queue_init(MMFrameSoundQueue *, const MMStorageAllocator *,
                              void (*retain)(void *), void (*release)(void *), int trash_unused);
void mm_frame_sound_queue_dispose(MMFrameSoundQueue *);
int mm_frame_sound_queue_enqueue(MMFrameSoundQueue *, const MMFrameSoundRequest *, MMI32 duration, int low);
int mm_frame_sound_queue_dequeue(MMFrameSoundQueue *, MMFrameSoundRequest *);
void mm_frame_sound_queue_clear(MMFrameSoundQueue *);
const MMFrameSoundRequest *mm_frame_sound_queue_at(const MMFrameSoundQueue *, MMU32 index);
int mm_frame_sound_queue_dispatch(const MMFrameSoundQueue *, MMI32 duration);
int mm_frame_sound_queue_timeout(MMI32 duration, MMI32 *interval);

#ifdef __cplusplus
}
#endif
#endif
