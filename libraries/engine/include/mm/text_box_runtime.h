/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_BOX_RUNTIME_H
#define MM_ENGINE_TEXT_BOX_RUNTIME_H
#include "text_store.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMTextBoxRuntime
{
    void *scene; /* borrowed; attachment publishes this after the frame fields */
    int selected;
} MMTextBoxRuntime;
typedef struct MMTextBoxBounds { MMI32 left, top, right, bottom; } MMTextBoxBounds;
typedef struct MMTextBoxLifetime
{
    MMI32 first, last;
    unsigned published; /* bit 1: first, bit 2: last, including partial failure */
} MMTextBoxLifetime;
typedef enum MMTextBoxOperation
{
    MM_TBOX_VIEW_COUNT, MM_TBOX_VIEW, MM_TBOX_CREATE_VIEW, MM_TBOX_PLAYING,
    MM_TBOX_DEACTIVATE, MM_TBOX_RELEASE_VIEW, MM_TBOX_DIRTY, MM_TBOX_SCENE_FRAME,
    MM_TBOX_SCENE_LAST, MM_TBOX_IS_SELECTED, MM_TBOX_DESELECT, MM_TBOX_INVALIDATE,
    MM_TBOX_ACTIVATE, MM_TBOX_ATTACH_MOUSE, MM_TBOX_NEEDS_SCROLL, MM_TBOX_SCROLL,
    MM_TBOX_TEXT_SELECTED, MM_TBOX_DIAGNOSTIC
} MMTextBoxOperation;
enum { MM_TBOX_MULTIPLE_VIEWS, MM_TBOX_FRAME_OVERFLOW, MM_TBOX_GOTO_FAILED };
typedef struct MMTextBoxRequest { void *object; MMI32 value; } MMTextBoxRequest;
typedef struct MMTextBoxRuntimeServices
{
    void *context;
    /* Return 1 for a completed operation or a negative exception/error code.
     * Ordinary false query results use value; constructor failure uses null. No callback may unwind across C. Queries put their
     * result in value (count/playing/frame/last/selection/scroll/text-selection).
     * VIEW returns a borrowed view in object; CREATE_VIEW returns a tree-owned
     * border in object, or null on ordinary failure. View operations receive
     * object. DIRTY/ACTIVATE receive value; SCENE_FRAME receives the new scene
     * in object, while other scene operations use the live runtime scene.
     * DIAGNOSTIC receives a diagnostic enum in value. Other fields are unchanged.
     * Callbacks may mutate state; policy reads live values at the native stages. */
    int (*call)(void *, MMTextBoxOperation, MMTextBoxRequest *);
} MMTextBoxRuntimeServices;
int mm_text_box_visible_range(MMI32 first, MMI32 end, MMI32 frame);
int mm_text_box_visible(const MMTextBoxState *);
int mm_text_box_scroll_frame(int story, MMI32 first, MMI32 frame);
int mm_text_box_attach(MMTextBoxState *, MMTextBoxRuntime *, void *scene, const MMTextBoxRuntimeServices *);
int mm_text_box_dirty(MMTextBoxRuntime *, int dirty, const MMTextBoxRuntimeServices *);
int mm_text_box_set_type(MMTextBoxState *, int story, const MMTextBoxRuntimeServices *);
int mm_text_box_set_bounds(MMTextBoxState *, MMTextBoxRuntime *, const MMTextBoxBounds *, const MMTextBoxRuntimeServices *);
int mm_text_box_lifetime(const MMTextBoxState *, MMTextBoxLifetime *, const MMTextBoxRuntimeServices *);
int mm_text_box_select(MMTextBoxRuntime *, int selected, const MMTextBoxRuntimeServices *);
int mm_text_box_goto(MMTextBoxState *, MMI32 frame, const MMTextBoxRuntimeServices *);
int mm_text_box_show(MMTextBoxState *, const MMTextBoxRuntimeServices *);
int mm_text_box_hide(MMTextBoxState *, const MMTextBoxRuntimeServices *);
int mm_text_box_attach_mouse(const MMTextBoxRuntimeServices *);
int mm_text_box_needs_scroll(const MMTextBoxState *, const MMTextBoxRuntimeServices *);
int mm_text_box_scroll(const MMTextBoxRuntimeServices *);
int mm_text_box_text_selected(const MMTextBoxState *, const MMTextBoxRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif
