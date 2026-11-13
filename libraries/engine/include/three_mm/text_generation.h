/* Three-dimensional text generation converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_TEXT_GENERATION_H
#define THREE_MM_ENGINE_TEXT_GENERATION_H
#include "template_create.h"
#include "actor_math.h"
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMTextShape
{
    THREE_MM_TEXT_NORMAL, THREE_MM_TEXT_ARCH_POSITIVE, THREE_MM_TEXT_CIRCLE_Y,
    THREE_MM_TEXT_LARGE_MIDDLE, THREE_MM_TEXT_ARCH_NEGATIVE, THREE_MM_TEXT_ARCH_Z,
    THREE_MM_TEXT_CIRCLE_Z, THREE_MM_TEXT_VERTICAL, THREE_MM_TEXT_GROW_RIGHT,
    THREE_MM_TEXT_GROW_LEFT, THREE_MM_TEXT_SHAPE_COUNT
};
enum ThreeMMTextAction
{
    THREE_MM_TEXT_REST, THREE_MM_TEXT_LETTER_ROT_X, THREE_MM_TEXT_LETTER_ROT_Y,
    THREE_MM_TEXT_LETTER_ROT_Z, THREE_MM_TEXT_SWING_X, THREE_MM_TEXT_SWING_Y,
    THREE_MM_TEXT_SWING_Z, THREE_MM_TEXT_PULSE, THREE_MM_TEXT_WORD_ROT_X,
    THREE_MM_TEXT_WORD_ROT_Y, THREE_MM_TEXT_WORD_ROT_Z, THREE_MM_TEXT_WAVE,
    THREE_MM_TEXT_REVEAL, THREE_MM_TEXT_WALK, THREE_MM_TEXT_HOP,
    THREE_MM_TEXT_STRETCH, THREE_MM_TEXT_ACTION_COUNT
};
typedef struct ThreeMMTextGenerated { void *material,*action; ThreeMMI32 action_id; } ThreeMMTextGenerated;
typedef struct ThreeMMTextGenerationServices
{
    void *context;
    ThreeMMTextGenerated *(*state)(void *,void *object);
    ThreeMMI32 (*length)(void *,void *object);
    ThreeMMI32 (*character)(void *,void *object,ThreeMMI32 index);
    ThreeMMI32 (*shape)(void *,void *object);
    void (*template_read)(void *,void *object,ThreeMMTemplateState *);
    void (*template_write)(void *,void *object,const ThreeMMTemplateState *);
    void *(*list_new)(void *,ThreeMMI32 entry,ThreeMMI32 capacity);
    void (*list_count)(void *,void *list,ThreeMMI32 count);
    void (*list_put)(void *,void *list,ThreeMMI32 index,const void *data);
    void *(*group_new)(void *,ThreeMMI32 fixed,ThreeMMI32 rows,ThreeMMI32 variable);
    int (*group_add)(void *,void *group,const void *fixed,ThreeMMI32 bytes,const void *variable);
    int (*allocate)(void *,ThreeMMI32 bytes,void **out);
    void (*free_memory)(void *,void **);
    void *(*material_new)(void *);
    void (*retain)(void *,void *);
    void (*release)(void *,void *);
    void *(*font)(void *,void *object);
    ThreeMMI32 (*font_width)(void *,void *font,ThreeMMI32 character);
    ThreeMMI32 (*font_height)(void *,void *font,ThreeMMI32 character);
    ThreeMMI32 (*font_maximum)(void *,void *font);
    void *(*glyph)(void *,void *font,ThreeMMI32 character);
    void *(*action_new)(void *,void *cels,void *matrices,ThreeMMU32 flags);
    int (*body_shape)(void *,void *body,void *parents,void *sets);
    void (*body_material)(void *,void *body,ThreeMMI32 set,void *material);
} ThreeMMTextGenerationServices;
/* Initialization builds all replacement lists/material references before
 * publishing any owner fields. Failure releases locals in native order and
 * leaves the existing owner untouched. Success replaces parent/set/costume/
 * material owners in order, then clears the cached action and publishes counts.
 * Strings are normalized by the caller; byte characters and native short cel
 * indices, positive cel counts and bounded scalar sums are preconditions.
 * list_count is guaranteed by the capacity supplied to list_new. Font/provider
 * services return retained references. Callbacks must not reenter/mutate owners. */
int three_mm_text_initialize_lists(void *,const ThreeMMTextGenerationServices *);
void *three_mm_text_build_indices(void *,int sets,const ThreeMMTextGenerationServices *);
void *three_mm_text_build_costumes(const ThreeMMTextGenerationServices *);
ThreeMMI32 three_mm_text_cel_count(ThreeMMI32 action);
void *three_mm_text_build_cels(void *,ThreeMMI32 action,const ThreeMMTextGenerationServices *);
void *three_mm_text_build_matrices(void *,ThreeMMI32 action,const ThreeMMTextGenerationServices *);
void *three_mm_text_build_action(void *,ThreeMMI32 action,const ThreeMMTextGenerationServices *);
/* Cache misses release the old action BEFORE building; failure leaves a NULL
 * cache/id -1. Hits retain without rebuilding. ID publishes after retain. */
void *three_mm_text_fetch_action(void *,ThreeMMI32 action,const ThreeMMTextGenerationServices *);
void *three_mm_text_fetch_model(void *,ThreeMMI32 index,const ThreeMMTextGenerationServices *);
int three_mm_text_adjust_body(void *,void *body,const ThreeMMTextGenerationServices *);
int three_mm_text_default_costume(void *,void *body,const ThreeMMTextGenerationServices *);
/* Material then action release; cache ID is retained, as in native destruction. */
void three_mm_text_dispose(ThreeMMTextGenerated *,const ThreeMMTextGenerationServices *);
/* Math uses original 16.16/BAM constants, exclusive upper clamps and operation
 * order. Negative halves round downward and zero-angle rotations are retained.
 * Invalid action indices return zero count; other indices follow native bounds. */
void three_mm_text_apply_action(ThreeMMActorMatrix *,ThreeMMI32 action,ThreeMMI32 character,ThreeMMI32 cels,ThreeMMI32 cel,ThreeMMI32 x,ThreeMMI32 width);
void three_mm_text_apply_shape(ThreeMMActorMatrix *,ThreeMMI32 shape,ThreeMMI32 characters,ThreeMMI32 character,ThreeMMI32 x,ThreeMMI32 width,ThreeMMI32 y,ThreeMMI32 maximum_height,ThreeMMI32 total_height);
#ifdef __cplusplus
}
#endif
#endif
