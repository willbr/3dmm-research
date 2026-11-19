#ifndef MM_ENGINE_SCENE_REFRAME_H
#define MM_ENGINE_SCENE_REFRAME_H
#include "actor_effect.h"
#include "movie_archive_edit.h"
#ifdef __cplusplus
extern "C" {
#endif

enum MMSceneReframeResult
{
    MM_SCENE_REFRAME_OK,
    MM_SCENE_REFRAME_LENS,
    MM_SCENE_REFRAME_RANGE,
    MM_SCENE_REFRAME_EMPTY,
    MM_SCENE_REFRAME_MEMORY,
    MM_SCENE_REFRAME_ACTOR,
    MM_SCENE_REFRAME_COPY,
    MM_SCENE_REFRAME_TRACK,
    MM_SCENE_REFRAME_ENTRANCE,
    MM_SCENE_REFRAME_LOCATION,
    MM_SCENE_REFRAME_SUBROUTE,
    MM_SCENE_REFRAME_EVENTS,
    MM_SCENE_REFRAME_WRITE,
    MM_SCENE_REFRAME_LINK
};

typedef struct MMSceneReframeCamera
{
    const float *transform; /* Twelve camera-to-world matrix values. */
    float vertical_fov, aspect;
} MMSceneReframeCamera;

typedef struct MMSceneReframePose
{
    MMActorLocation location;
    MMActorPoint position, translation;
    float rotation[9];
} MMSceneReframePose;

typedef struct MMSceneReframeHost
{
    void *context;
    int (*pose)(void *, MMI32 actor_id, MMSceneReframePose *); /* Zero for an offstage actor. */
    void (*from_euler)(const MMActorEntrance *, MMActorMatrix *);
    void (*to_euler)(const MMActorMatrix *, MMActorEntrance *); /* Changes x/y/z only. */
} MMSceneReframeHost;

/* Reframe the selected scene from an evaluated perspective pose. The edited
 * archive is private. On failure the caller discards the candidate archive. */
int mm_scene_archive_reframe(const MMMovieArchiveEditor *, MMU32 scene, MMI32 frame,
                             const MMSceneReframeCamera *work, const MMSceneReframeCamera *camera,
                             const MMSceneReframeHost *);

#ifdef __cplusplus
}
#endif
#endif
