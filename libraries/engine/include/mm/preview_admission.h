/* Preview admission, dependency budgets, pose validation and geometry counts. */
#ifndef MM_ENGINE_PREVIEW_ADMISSION_H
#define MM_ENGINE_PREVIEW_ADMISSION_H
#include "movie_session.h"
#include "preview_live.h"
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewChunk
    {
        MMU32 type, number, stored;
        int packed, named;
    } MMPreviewChunk;
    typedef struct MMPreviewBudgetServices
    {
        void *context;
        MMU32 count;
        void (*chunk)(void *, MMU32, MMPreviewChunk *);
        int (*header)(void *, const MMPreviewChunk *, unsigned char bytes[8]);
    } MMPreviewBudgetServices;
    enum
    {
        MM_PREVIEW_BUDGET_OK,
        MM_PREVIEW_BUDGET_HEADER,
        MM_PREVIEW_BUDGET_LIMIT
    };
    /* Reads packed headers before size checks. failed_index changes only for a
     * limit failure. Limits are inclusive: 64 MiB per stored/decoded chunk and
     * 512 MiB total decoded bytes. Getters borrow immutable native metadata. */
    int mm_preview_budget(const MMPreviewBudgetServices *, MMU32 *failed_index);
    enum
    {
        MM_PREVIEW_TEMPLATE,
        MM_PREVIEW_MODEL,
        MM_PREVIEW_BACKGROUND,
        MM_PREVIEW_SCENE,
        MM_PREVIEW_ACTOR,
        MM_PREVIEW_UNKNOWN
    };
    typedef struct MMPreviewAdmission
    {
        MMPreviewLive *live;
        MMU32 type, number, action_count, detail;
        int kind, cache, mark_chunk;
        void *tmpl, *body, *model;
    } MMPreviewAdmission;
    typedef struct MMPreviewAdmissionTag
    {
        MMI32 source;
        MMU32 type, number;
    } MMPreviewAdmissionTag;
    typedef struct MMPreviewAdmissionServices
    {
        void *context;
        const MMStorageAllocator *allocator;
        void (*clear)(void *, int preserve_framing);
        void (*publish)(void *, const MMPreviewAdmission *);
        void (*begin)(void *);
        int (*healthy)(void *);
        int (*template_fetch)(void *, MMU32, MMU32, void **);
        int (*template_bind)(void *, void *);
        int (*body_create)(void *, void *, void **);
        int (*prop)(void *, void *);
        MMI32 (*actions)(void *, void *);
        int (*cels)(void *, void *, MMU32, MMI32 *);
        int (*child)(void *, MMU32 type, MMU32 number, MMU32 slot, int actor, MMPreviewChunk *);
        MMU32 (*chunks)(void *);
        void (*chunk)(void *, MMU32, MMPreviewChunk *);
        int (*action_add)(void *, MMU32 action, MMU32 cels, MMI32 name_index);
        MMU32 (*action_cels)(void *, MMU32);
        int (*pose)(void *, void *tmpl, void *body, MMU32 action, MMU32 cel);
        void (*rest)(void *, void *, unsigned short angles[3]);
        void (*rotate)(void *, MMActorMatrix *, int axis, unsigned short, int post);
        void (*locate)(void *, void *, const MMActorMatrix *);
        int (*model_fetch)(void *, MMU32, MMU32, void **);
        int (*background)(void *, const MMPreviewAdmissionTag *, int virtual_scene);
        MMMovieSceneReader scene; /* Movie statuses, with negative host failures as below. */
        int (*actor_header)(void *, MMU32, MMI32 *first, MMI32 *last);
        int (*actor_load)(void *, MMU32, void **);
        void (*actor_release)(void *, void *);
        void (*actor_range)(void *, void *, MMI32 *first, MMI32 *last);
        int (*sync)(void *), (*seek)(void *, MMI32);
        int (*save_actor_error)(void *);
        void (*stats)(void *);
    } MMPreviewAdmissionServices;
    enum
    {
        MM_PREVIEW_ADMIT_OK,
        MM_PREVIEW_ADMIT_MEMORY,
        MM_PREVIEW_ADMIT_EXCEPTION,
        MM_PREVIEW_ADMIT_CACHE,
        MM_PREVIEW_ADMIT_VIRTUAL_BACKGROUND,
        MM_PREVIEW_ADMIT_SCENE_HEADER,
        MM_PREVIEW_ADMIT_START_MISSING,
        MM_PREVIEW_ADMIT_START_READ,
        MM_PREVIEW_ADMIT_ACTOR_CHILD,
        MM_PREVIEW_ADMIT_BACKGROUND,
        MM_PREVIEW_ADMIT_FRAME_MISSING,
        MM_PREVIEW_ADMIT_FRAME_READ,
        MM_PREVIEW_ADMIT_ACTOR_HEADER,
        MM_PREVIEW_ADMIT_OFFSTAGE,
        MM_PREVIEW_ADMIT_ACTOR_LOAD,
        MM_PREVIEW_ADMIT_EXTERNAL,
        MM_PREVIEW_ADMIT_UNSUPPORTED,
        MM_PREVIEW_ADMIT_DEPENDENCIES,
        MM_PREVIEW_ADMIT_POSE_RANGE,
        MM_PREVIEW_ADMIT_POSE
    };
    /* Synchronous, non-reentrant services never throw. Fallible effects return 1/0,
     * -1 for allocation exceptions, -2 for other exceptions. group_read uses 0 for
     * missing, 2 for malformed, 1 for success. Acquired groups and unadopted actors
     * are always released, including on exceptions. Native asset handles are owned
     * by clear; live owns adopted actors. publish only marshals completed state.
     * Selection memory failure clears the asset; ordinary early failures retain completed
     * prefixes, and final dependency failure clears and marks the selected chunk.
     * Other exceptions retain asset prefixes and are rethrown by the typed host.
     * Input kind is the host's classification of the original chunk type.
     * Pose exceptions retain state; ordinary pose-effect failure clears ready.
     * Admission is a transient borrowed record, not a second asset owner. */
    int mm_preview_admit(MMPreviewAdmission *, int preserve_framing, const MMPreviewAdmissionServices *);
    int mm_preview_admission_pose(MMPreviewAdmission *, MMU32 action, MMU32 cel, const MMPreviewAdmissionServices *);
    typedef struct MMPreviewStatsServices
    {
        void *context, *body, *model;
        MMU32 actors;
        int (*visible)(void *, MMU32);
        void *(*actor_body)(void *, MMU32);
        MMI32 (*body_parts)(void *, void *);
        void *(*part_model)(void *, void *, MMI32);
        MMU32 (*faces)(void *, void *);
    } MMPreviewStatsServices;
    void mm_preview_stats(const MMPreviewStatsServices *, MMU32 *parts, MMU32 *triangles);
#ifdef __cplusplus
}
#endif
#endif
