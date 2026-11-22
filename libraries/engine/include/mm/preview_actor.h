/* Preview actor ownership and playback control over the shared actor evaluators. */
#ifndef MM_ENGINE_PREVIEW_ACTOR_H
#define MM_ENGINE_PREVIEW_ACTOR_H
#include "actor_timeline.h"
#include "actor_pose.h"
#include "actor_effect.h"
#include "actor_track.h"
#include "actor_sound.h"
#include "body_geometry.h"
#include "resource_cache.h"
#include "string_value.h"
#include "wide.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewActorShape
    {
        float rotation[9], scale[3];
    } MMPreviewActorShape;
    typedef struct MMPreviewActor
    {
        MMActorHeader header;
        MMU32 number;
        MMActorState state, orientation;
        MMPreviewActorShape shape;
        MMI32 original[3];
        void *tmpl, *body, *route, *events, *material_override;
        MMWide draw_order, *draw_clock;
        int inactive, orientation_reset, sound_display;
        MMI32 event_source;
    } MMPreviewActor;
    typedef struct MMPreviewActorDefinition
    {
        MMActorHeader header;
        MMU32 number;
        float point[3];
    } MMPreviewActorDefinition;
    typedef struct MMPreviewActorServices
    {
        void *context;
        MMActorTimelineServices timeline;
        int (*fault)(void *);
        int (*header)(void *, MMU32, MMActorHeader *);
        int (*route)(void *, MMU32, void **);
        int (*events)(void *, MMU32, void **);
        int (*fetch)(void *, const MMActorHeader *, void **);
        int (*bind)(void *, void *), (*body_create)(void *, void *, void **);
        int (*prop)(void *, void *), (*text)(void *, void *);
        void (*release_body)(void *, void *), (*release_template)(void *, void *);
        /* With tags supplied, load transfers typed PATH/GGAE ownership.
         * Services without tags supply borrowed evaluator tables. */
        const MMTagServices *tags;
        const MMStorageAllocator *track_allocator;
        int trash_unused;
        void (*rest)(void *, void *, unsigned short angles[3]);
        MMI32 (*scalar)(float);
        float (*scalar_float)(MMI32);
        void (*locate)(void *, void *, const MMActorBodyPose *);
        int (*pose)(void *, void *, void *, MMI32 action, MMI32 cel);
        int (*apply)(void *, MMI32 index);
        void (*clear_cues)(void *), (*clear_motion)(void *);
        int (*default_costume)(void *, void *, void *), (*motion_cues)(void *);
        int (*duplicate_body)(void *, void *original, void **draft);
        int (*adjust_text_body)(void *, void *text, void *body);
        int (*fetch_material)(void *, MMI32 source, MMU32 number, void **);
        void (*release_material)(void *, void *);
        int (*apply_material)(void *, void *body, void *material);
    } MMPreviewActorServices;
    /* Unique owning template/body handles. The track handles can also own
     * their tables when the services supply tags. Effects are synchronous
     * and do not throw: 1/0 success/failure, -1 allocation, -2 other exception.
     * Timeline resolvers report exceptions through fault after the shared evaluator
     * returns. Completed state/table/resource prefixes remain on failure, as in the
     * native provider. Disposal releases body before template. Resource output
     * handles must be published before a provider reports failure if ownership has
     * transferred. The native host transports exceptions after C returns. */
    void mm_preview_actor_init(MMPreviewActor *);
    void mm_preview_actor_dispose(MMPreviewActor *, const MMPreviewActorServices *);
    /* Record an entrance against the borrowed scene clock, with unsigned
     * ordering of the two words. Hides leave the last entrance alone. */
    void mm_preview_actor_shown(MMPreviewActor *);
    int mm_preview_actor_load(MMPreviewActor *, MMU32 number, const MMChunkAccess *, int require_template_target,
                              const MMPreviewActorServices *);
    /* Create a new actor with owned C tables. Supply the native tag services. */
    int mm_preview_actor_create(MMPreviewActor *, const MMPreviewActorDefinition *, const MMPreviewActorServices *);
    /* Borrow typed handles from an actor with owned tracks. */
    MMActorTrack mm_preview_actor_track(const MMPreviewActor *);

    typedef struct MMPreviewActorInfo
    {
        MMU32 number, template_type, template_number;
        MMI32 actor, source, first, last, action, cel;
        int has_lifetime, prop, text, visible;
        MMActorPoint position;
    } MMPreviewActorInfo;
    void mm_preview_actor_info(const MMPreviewActor *, MMPreviewActorInfo *);
    typedef struct MMPreviewActorText
    {
        MMStringValue name;
        MMI32 shape, font_source;
        MMU32 font_number;
    } MMPreviewActorText;
    /* The text view borrows the template's counted name. Failure leaves it untouched. */
    int mm_preview_actor_text(const MMPreviewActor *, MMPreviewActorText *);
    struct MMTemplateOwner;
    typedef int MMPreviewActorCelCount(struct MMTemplateOwner *, MMI32, void *);
    MMI32 mm_preview_actor_action_count(const MMPreviewActor *);
    MMI32 mm_preview_actor_action_cels(const MMPreviewActor *, MMI32, MMPreviewActorCelCount *);

    struct MMBodyRuntimeServices;
    struct MMMaterialOwner;
    typedef struct MMPreviewCostumeCatalog
    {
        const struct MMBodyRuntimeServices *body;
        MMI32 (*count)(struct MMTemplateOwner *, MMI32 set);
        MMI32 (*id)(struct MMTemplateOwner *, MMI32 set, MMI32 index);
        struct MMMaterialOwner *(*fetch)(struct MMTemplateOwner *, MMI32 id);
        int (*accessory)(struct MMTemplateOwner *, MMI32 set);
    } MMPreviewCostumeCatalog;
    typedef struct MMPreviewCostumeChoice
    {
        MMI32 set, id;
    } MMPreviewCostumeChoice;
    typedef struct MMPreviewCostumeEntry
    {
        MMPreviewCostumeChoice choice;
        int selected, accessory;
    } MMPreviewCostumeEntry;
    typedef int MMPreviewCostumeVisit(void *, const MMPreviewCostumeEntry *);
    /* A null visitor validates the catalogue without publishing rows. */
    int mm_preview_actor_costumes(const MMPreviewActor *, const MMPreviewCostumeCatalog *,
                                 void *, MMPreviewCostumeVisit *);
    typedef struct MMPreviewCostumeEdits
    {
        const void *context;
        size_t count;
        void (*get)(const void *, size_t, MMPreviewCostumeChoice *);
    } MMPreviewCostumeEdits;
    typedef int MMPreviewCostumeApply(void *, const MMPreviewCostumeChoice *);
    /* Validate the catalogue, then apply choices in input order. Failure retains
     * the completed prefix. A null effect applies the costume to the live body. */
    int mm_preview_actor_edit_costumes(MMPreviewActor *, const MMPreviewCostumeCatalog *,
                                      const MMPreviewCostumeEdits *, void *, MMPreviewCostumeApply *);

    typedef struct MMPreviewActorDraft
    {
        MMPreviewActor original;
        MMActorSounds motion;
        MMWide draw_clock;
        int active;
    } MMPreviewActorDraft;
    /* Use an actor with owned tracks and a zero-initialized draft. A successful begin installs a
     * separate body, track, and motion list until end restores the original. */
    int mm_preview_actor_begin_draft(MMPreviewActor *, MMActorSounds *, MMPreviewActorDraft *,
                                     const MMPreviewActorServices *);
    void mm_preview_actor_end_draft(MMPreviewActor *, MMActorSounds *, MMPreviewActorDraft *,
                                    const MMPreviewActorServices *);

    typedef struct MMPreviewEventReplacement
    {
        void *context;
        /* Borrow the cache owning the requested root track for this call. */
        int (*resolve)(void *, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **);
        void *decode_context;
        MMBlockTransform decode;
        int (*save_costume)(void *);
        void (*restore_costume)(void *);
    } MMPreviewEventReplacement;
    /* Replace a preview's owned track events and replay its current frame.
     * Failure restores both original tables, evaluation state and costume.
     * Cue and motion callbacks retain their replay side effects. */
    int mm_preview_actor_replace_track(MMPreviewActor *, MMI32 source, const MMPreviewEventReplacement *,
                                       const MMPreviewActorServices *);
    /* Fetch/bind a text template, adjust a body draft, then pose it. Restore
     * the entire actor and release the draft on failure, including pose errors. */
    int mm_preview_actor_replace_template(MMPreviewActor *, MMI32 source, MMU32 number,
                                          const MMPreviewActorServices *);
    /* Hold an owned text material independently of the body, for reapplication
     * after seeks. A failed fetch or apply retains the previous override. */
    int mm_preview_actor_set_material(MMPreviewActor *, MMI32 source, MMU32 number,
                                     const MMPreviewActorServices *);
    int mm_preview_actor_seek(MMPreviewActor *, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_replay_track(MMPreviewActor *, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_position(MMPreviewActor *, int select_cel, const MMPreviewActorServices *);
    struct MMPreviewOrbit;
    /* A placement edit moves the tracked anchor while preserving the camera fit. */
    int mm_preview_actor_translate(MMPreviewActor *, const float delta[3], struct MMPreviewOrbit *, MMI32 object,
                                    const MMPreviewActorServices *);
    int mm_preview_actor_offset(MMPreviewActor *, const float offset[3], struct MMPreviewOrbit *, MMI32 object,
                                 const MMPreviewActorServices *);
    enum MMPreviewShapeResult
    {
        MM_PREVIEW_SHAPE_FAILED, MM_PREVIEW_SHAPE_OK, MM_PREVIEW_SHAPE_SCALE, MM_PREVIEW_SHAPE_ROTATION,
        MM_PREVIEW_SHAPE_REFLECTION, MM_PREVIEW_SHAPE_AXES
    };
    /* Validate before editing. An optional orbit tracks the selected body's
     * center after a successful pose update. Negative service errors propagate. */
    int mm_preview_actor_set_shape(MMPreviewActor *, const MMPreviewActorShape *, struct MMPreviewOrbit *,
                                   MMI32 object, const MMBodyGeometryServices *, const MMPreviewActorServices *);
    void mm_preview_actor_shape(const MMPreviewActor *, MMPreviewActorShape *, float left[9], float right[9],
                                const MMPreviewActorServices *);
    typedef struct MMPreviewTrackPose
    {
        MMI32 node, offset, route_frame;
        MMI32 position[3], translation[3];
        float rotation[9];
    } MMPreviewTrackPose;
    /* Read the live route cursor and recover world rotation from the posed root.
     * A collapsed scale fails after publishing the cursor and any earlier rows. */
    int mm_preview_actor_track_pose(const MMPreviewActor *, MMPreviewTrackPose *, const MMPreviewActorServices *);
    /* Read the visible body's local bounds and place their eight corners in
     * world space. An absent or offstage actor leaves the output untouched. */
    int mm_preview_actor_bounds(const MMPreviewActor *, float corners[24], const MMBodyGeometryServices *,
                                float (*scalar_float)(MMI32));

    enum MMPreviewActionResult
    {
        MM_PREVIEW_ACTION_FAILED, MM_PREVIEW_ACTION_OK, MM_PREVIEW_ACTION_RANGE,
        MM_PREVIEW_ACTION_MEMORY, MM_PREVIEW_ACTION_POSE, MM_PREVIEW_ACTION_FRAME
    };
    typedef struct MMPreviewIsolatedView
    {
        void *context;
        const MMBodyGeometryServices *geometry;
        MMBodyModelBounds *model_bounds; /* Used for focused costume sets. */
        int (*render)(void *, const float corners[24], int crop);
    } MMPreviewIsolatedView;
    /* Render with a duplicate body. Restore the live body before releasing the
     * duplicate. An empty optional clip is filled from up to 24 evenly spaced
     * cels and the requested pose, and remains reusable after a render failure. */
    int mm_preview_actor_action_render(MMPreviewActor *, MMI32 action, MMI32 cel, MMI32 count,
                                       float clip[24], int *clip_valid, const MMPreviewActorServices *,
                                       const MMPreviewIsolatedView *);

    enum MMPreviewCostumeResult
    {
        MM_PREVIEW_COSTUME_FAILED, MM_PREVIEW_COSTUME_OK, MM_PREVIEW_COSTUME_PREPARE,
        MM_PREVIEW_COSTUME_APPLY, MM_PREVIEW_COSTUME_NO_SET
    };
    /* Synchronous isolated render. Capture the original costume and pose,
     * frame the union of the requested set's variants, and restore the
     * original state on every path after capture. Host callbacks do not throw. */
    int mm_preview_actor_costume_render(MMPreviewActor *, MMI32 set, const MMPreviewCostumeEdits *,
                                        const MMPreviewCostumeCatalog *, const MMPreviewActorServices *,
                                        const MMPreviewIsolatedView *);
#ifdef __cplusplus
}
#endif
#endif
