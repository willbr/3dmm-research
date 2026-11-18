/* Three-dimensional text generation converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_GENERATION_H
#define MM_ENGINE_TEXT_GENERATION_H
#include "template_create.h"
#include "action_definition.h"
#include "actor_math.h"
#ifdef __cplusplus
extern "C" {
#endif
enum MMTextShape
{
    MM_TEXT_NORMAL, MM_TEXT_ARCH_POSITIVE, MM_TEXT_CIRCLE_Y,
    MM_TEXT_LARGE_MIDDLE, MM_TEXT_ARCH_NEGATIVE, MM_TEXT_ARCH_Z,
    MM_TEXT_CIRCLE_Z, MM_TEXT_VERTICAL, MM_TEXT_GROW_RIGHT,
    MM_TEXT_GROW_LEFT, MM_TEXT_SHAPE_COUNT
};
enum MMTextAction
{
    MM_TEXT_REST, MM_TEXT_LETTER_ROT_X, MM_TEXT_LETTER_ROT_Y,
    MM_TEXT_LETTER_ROT_Z, MM_TEXT_SWING_X, MM_TEXT_SWING_Y,
    MM_TEXT_SWING_Z, MM_TEXT_PULSE, MM_TEXT_WORD_ROT_X,
    MM_TEXT_WORD_ROT_Y, MM_TEXT_WORD_ROT_Z, MM_TEXT_WAVE,
    MM_TEXT_REVEAL, MM_TEXT_WALK, MM_TEXT_HOP,
    MM_TEXT_STRETCH, MM_TEXT_ACTION_COUNT
};
typedef struct MMTextGenerated { void *material,*action; MMI32 action_id; } MMTextGenerated;
typedef struct MMTextGenerationServices
{
    void *context;
    MMTextGenerated *(*state)(void *,void *object);
    MMI32 (*length)(void *,void *object);
    MMI32 (*character)(void *,void *object,MMI32 index);
    MMI32 (*shape)(void *,void *object);
    MMTemplateState *(*template_state)(void *,void *object);
    MMPartIndexList *(*indices_new)(void *);
    MMTemplateCostumeTable *(*costumes_new)(void *);
    void *(*material_new)(void *);
    void (*retain)(void *,void *);
    void (*release)(void *,void *);
    void *(*font)(void *,void *object);
    MMI32 (*font_width)(void *,void *font,MMI32 character);
    MMI32 (*font_height)(void *,void *font,MMI32 character);
    MMI32 (*font_maximum)(void *,void *font);
    void *(*glyph)(void *,void *font,MMI32 character);
    MMActionTable *(*table_new)(void *);
    /* Successful creation consumes both tables. Failure leaves them owned
     * by the caller. */
    void *(*action_new)(void *,MMActionTable *cels,MMActionTable *matrices,MMU32 flags);
    int (*body_shape)(void *,void *body,const MMPartIndexView *parents,const MMPartIndexView *sets);
    void (*body_material)(void *,void *body,MMI32 set,void *material);
} MMTextGenerationServices;
/* Initialization builds all replacement lists/material references before
 * publishing any owner fields. Failure releases locals in native order and
 * leaves the existing owner untouched. Success replaces parent/set/costume/
 * material owners in order, then clears the cached action and publishes counts.
 * Strings are normalized by the caller. Byte characters and bounded scalar
 * sums are preconditions. Font/provider services return retained references.
 * Callbacks must not reenter or mutate owners. */
int mm_text_initialize_lists(void *,const MMTextGenerationServices *);
MMI32 mm_text_cel_count(MMI32 action);
MMI32 mm_text_cel_distance(MMI32 action);
MMU32 mm_text_action_flags(void);
MMActionTable *mm_text_build_cels(void *,MMI32 action,const MMTextGenerationServices *);
MMActionTable *mm_text_build_matrices(void *,MMI32 action,const MMTextGenerationServices *);
/* Evaluate one cel without allocating an action table. The writer receives
 * one matrix per character in order, starting at index zero. */
typedef void (*MMTextMatrixWrite)(void *,MMI32 index,const MMActorMatrix *);
int mm_text_evaluate_cel(void *,MMI32 action,MMI32 cel,MMTextMatrixWrite,void *target,
                         const MMTextGenerationServices *);
/* Cache misses release the old action BEFORE building; failure leaves a NULL
 * cache/id -1. Hits retain without rebuilding. ID publishes after retain. */
void *mm_text_fetch_action(void *,MMI32 action,const MMTextGenerationServices *);
void *mm_text_fetch_model(void *,MMI32 index,const MMTextGenerationServices *);
int mm_text_adjust_body(void *,void *body,const MMTextGenerationServices *);
int mm_text_default_costume(void *,void *body,const MMTextGenerationServices *);
/* Material then action release; cache ID is retained, as in native destruction. */
void mm_text_dispose(MMTextGenerated *,const MMTextGenerationServices *);
/* Math uses original 16.16/BAM constants, exclusive upper clamps and operation
 * order. Negative halves round downward and zero-angle rotations are retained.
 * Invalid action indices return zero count; other indices follow native bounds. */
void mm_text_apply_action(MMActorMatrix *,MMI32 action,MMI32 character,MMI32 cels,MMI32 cel,MMI32 x,MMI32 width);
void mm_text_apply_shape(MMActorMatrix *,MMI32 shape,MMI32 characters,MMI32 character,MMI32 x,MMI32 width,MMI32 y,MMI32 maximum_height,MMI32 total_height);
#ifdef __cplusplus
}
#endif
#endif
