#ifndef MM_ENGINE_ACTOR_ANIMATION_H
#define MM_ENGINE_ACTOR_ANIMATION_H
#include "actor_record.h"
#include "preview_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_ACTOR_ANIMATION_ACTION, MM_ACTOR_ANIMATION_RECORD, MM_ACTOR_ANIMATION_TWEAK };
enum { MM_ACTOR_ANIMATION_MAX_POINTS = 2048 };

typedef struct MMActorAnimationPoint
{
    float x, y, z;
} MMActorAnimationPoint;

typedef struct MMActorAnimation
{
    int kind, freeze, replace;
    MMI32 action, cel;
    const MMActorAnimationPoint *points; /* World-axis offsets from the playhead position. */
    size_t point_count;
} MMActorAnimation;

typedef struct MMActorAnimationRange
{
    MMI32 actor_last, edited_last;
} MMActorAnimationRange;

/* Edit a private draft of the actor and its tables. The caller discards the
 * draft on failure. The range is published only when evaluation succeeds. */
int mm_actor_animation_edit(MMPreviewActor *, MMActorRecording *,
                            const MMActorAnimation *, MMI32 action_count,
                            const MMActorRecordServices *, MMActorAnimationRange *);

#ifdef __cplusplus
}
#endif
#endif
