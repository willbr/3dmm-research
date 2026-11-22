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
    typedef struct MMPreviewBudgetServices
    {
        void *context;
        MMChunkAccess chunks;
        int (*header)(void *, const MMChunkEntry *, unsigned char bytes[8]);
    } MMPreviewBudgetServices;
    enum
    {
        MM_PREVIEW_BUDGET_OK,
        MM_PREVIEW_BUDGET_HEADER,
        MM_PREVIEW_BUDGET_LIMIT
    };
    /* Reads packed headers before size checks. failed changes only for a
     * limit failure. Limits are inclusive: 64 MiB per stored/decoded chunk and
     * 512 MiB total decoded bytes. Getters borrow immutable native metadata. */
    int mm_preview_budget(const MMPreviewBudgetServices *, MMChunkEntry *failed);
    struct MMArchiveReader;
    int mm_preview_archive_budget(const struct MMArchiveReader *, MMChunkEntry *failed);
    typedef struct MMPreviewAdmission
    {
        MMPreviewLive *live;
        MMU32 type, number, detail;
        int cache, mark_chunk;
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
        int (*template_fetch)(void *, MMU32, MMU32, struct MMTemplateOwner **);
        int (*template_bind)(void *, void *);
        int (*body_create)(void *, struct MMTemplateOwner *, struct MMBodyOwner **);
        int (*prop)(void *, void *);
        MMI32 (*actions)(void *, void *);
        int (*cels)(void *, void *, MMU32, MMI32 *);
        MMChunkAccess action_chunks;
        MMPreviewActionPublish *publish_action;
        int (*pose)(void *, void *tmpl, void *body, MMU32 action, MMU32 cel);
        void (*rest_pose)(void *, struct MMTemplateOwner *, struct MMBodyOwner *);
        int (*model_fetch)(void *, MMU32, MMU32, struct MMModelOwner **);
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
     * are always released, including on exceptions. Asset fetches transfer
     * one reference through their output even on incomplete loads. clear
     * releases the live owner's assets. publish only marshals completed state.
     * Selection memory failure clears the asset; ordinary early failures retain completed
     * prefixes, and final dependency failure clears and marks the selected chunk.
     * Other exceptions retain asset prefixes and are rethrown by the typed host.
     * Pose exceptions retain state; ordinary pose-effect failure clears ready.
     * Admission is a transient borrowed record, not a second asset owner. */
    int mm_preview_admit(MMPreviewAdmission *, int preserve_framing, const MMPreviewAdmissionServices *);
    int mm_preview_admission_pose(MMPreviewAdmission *, MMU32 action, MMU32 cel, const MMPreviewAdmissionServices *);
#ifdef __cplusplus
}
#endif
#endif
