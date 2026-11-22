#ifndef MARCUS_PLAYBACK_SCENE_H
#define MARCUS_PLAYBACK_SCENE_H

#include "mm/frame_sound.h"
#include "mm/preview_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusPlaybackScene
{
    MMCollectionStorage cues, pauses, actors;
    MMU32 cue_count, pause_count, actor_count;
    MMI32 transition;
} MarcusPlaybackScene;

typedef int MarcusPlaybackCueSink(void *, const MMPreviewCue *);

typedef struct MarcusPlaybackFrameServices
{
    void *context;
    int (*actors)(void *, void *output, MarcusPlaybackCueSink *);
    int (*play)(void *, const MMPreviewCue *, MMAudioCounter offset);
} MarcusPlaybackFrameServices;

void marcus_playback_scene_init(MarcusPlaybackScene *);
void marcus_playback_scene_clear(MarcusPlaybackScene *);
/* Appends copy the borrowed rows and return 1 or -1 for allocation failure. */
int marcus_playback_scene_cue(MarcusPlaybackScene *, const MMPreviewCue *);
int marcus_playback_scene_pause(MarcusPlaybackScene *, MMI32 frame, MMI32 kind, MMI32 milliseconds);
/* Playback order and pause selection use the shared frame-sound evaluator.
 * Current-frame actor cues are borrowed from the host via a synchronous sink. */
int marcus_playback_scene_enter(MarcusPlaybackScene *, MMI32 frame, int restore_music, double fps,
                                const MarcusPlaybackFrameServices *, MMPolledFrameWait *);

#ifdef __cplusplus
}
#endif
#endif
