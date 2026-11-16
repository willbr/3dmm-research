/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_box_runtime.h"
#include "mm/wide.h"
#define TRY(e) do { int status_ = (e); if (status_ < 0) return status_; } while (0)
static int call(const MMTextBoxRuntimeServices *s, MMTextBoxOperation op, MMTextBoxRequest *q)
{
    return s->call(s->context, op, q);
}
int mm_text_box_visible_range(MMI32 first, MMI32 end, MMI32 frame)
{
    return frame >= first && frame < end;
}
int mm_text_box_visible(const MMTextBoxState *s)
{
    return mm_text_box_visible_range(s->first, s->end, s->current);
}
int mm_text_box_scroll_frame(int story, MMI32 first, MMI32 frame)
{
    return frame == first && !story;
}
int mm_text_box_attach(MMTextBoxState *s, MMTextBoxRuntime *r, void *scene, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    q.object = scene;
    TRY(call(p, MM_TBOX_SCENE_FRAME, &q)); s->current = q.value;
    q.object = scene;
    TRY(call(p, MM_TBOX_SCENE_FRAME, &q)); s->first = q.value;
    s->end = 2147483647L; r->scene = scene;
    return 1;
}
int mm_text_box_dirty(MMTextBoxRuntime *r, int dirty, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    if (r->scene)
    {
        q.value = dirty != 0;
        TRY(call(p, MM_TBOX_DIRTY, &q));
    }
    return 1;
}
int mm_text_box_set_type(MMTextBoxState *s, int story, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    s->story = story != 0;
    TRY(call(p, MM_TBOX_VIEW, &q));
    if (q.object) TRY(call(p, MM_TBOX_INVALIDATE, &q));
    return 1;
}
int mm_text_box_attach_mouse(const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    TRY(call(p, MM_TBOX_VIEW, &q));
    if (q.object) TRY(call(p, MM_TBOX_ATTACH_MOUSE, &q));
    return 1;
}
int mm_text_box_set_bounds(MMTextBoxState *s, MMTextBoxRuntime *r, const MMTextBoxBounds *bounds,
                           const MMTextBoxRuntimeServices *p)
{
    if (s->left != bounds->left || s->top != bounds->top || s->right != bounds->right || s->bottom != bounds->bottom)
    {
        TRY(mm_text_box_dirty(r, 1, p));
        s->left = bounds->left; s->top = bounds->top; s->right = bounds->right; s->bottom = bounds->bottom;
    }
    return 1;
}
int mm_text_box_lifetime(const MMTextBoxState *s, MMTextBoxLifetime *out, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    out->published = 0;
    if (s->end < s->first) return 0;
    out->first = s->first; out->published = 1;
    if (s->end == 2147483647L)
    {
        TRY(call(p, MM_TBOX_SCENE_LAST, &q)); out->last = q.value;
    }
    else out->last = mm_signed_word((MMU32)s->end - 1U);
    out->published = 3;
    return 1;
}
int mm_text_box_select(MMTextBoxRuntime *r, int selected, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    selected = selected != 0;
    if (selected == r->selected) return 1;
    r->selected = selected;
    TRY(call(p, MM_TBOX_VIEW_COUNT, &q));
    if (q.value > 0)
    {
        if (q.value != 1)
        {
            q.value = MM_TBOX_MULTIPLE_VIEWS;
            TRY(call(p, MM_TBOX_DIAGNOSTIC, &q));
        }
        TRY(call(p, MM_TBOX_VIEW, &q));
        q.value = selected;
        TRY(call(p, MM_TBOX_ACTIVATE, &q));
        TRY(call(p, MM_TBOX_INVALIDATE, &q));
    }
    return 1;
}
int mm_text_box_goto(MMTextBoxState *s, MMI32 frame, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    void *view = NULL;
    TRY(call(p, MM_TBOX_VIEW_COUNT, &q));
    if (q.value != 0)
    {
        TRY(call(p, MM_TBOX_VIEW, &q)); view = q.object;
    }
    s->current = frame;
    if (mm_text_box_visible(s) && !view)
    {
        TRY(call(p, MM_TBOX_CREATE_VIEW, &q));
        if (!q.object) return 0;
        TRY(call(p, MM_TBOX_PLAYING, &q));
        if (q.value) TRY(call(p, MM_TBOX_DEACTIVATE, &q));
    }
    if (!mm_text_box_visible(s) && view)
    {
        q.object = view;
        TRY(call(p, MM_TBOX_RELEASE_VIEW, &q));
    }
    return 1;
}
int mm_text_box_show(MMTextBoxState *s, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    q.value = 1;
    TRY(call(p, MM_TBOX_DIRTY, &q));
    if (s->end < s->current)
    {
        if (s->current >= 2147483647L)
        {
            q.value = MM_TBOX_FRAME_OVERFLOW;
            TRY(call(p, MM_TBOX_DIAGNOSTIC, &q));
        }
        s->end = mm_signed_word((MMU32)s->current + 1U);
    }
    else if (s->current < s->first) s->first = s->current;
    return mm_text_box_goto(s, s->current, p);
}
int mm_text_box_hide(MMTextBoxState *s, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    int result;
    s->end = s->current;
    q.value = 1;
    TRY(call(p, MM_TBOX_DIRTY, &q));
    result = mm_text_box_goto(s, s->current, p);
    TRY(result);
    if (!result)
    {
        q.value = MM_TBOX_GOTO_FAILED;
        TRY(call(p, MM_TBOX_DIAGNOSTIC, &q));
    }
    TRY(call(p, MM_TBOX_IS_SELECTED, &q));
    if (q.value) TRY(call(p, MM_TBOX_DESELECT, &q));
    return 1;
}
int mm_text_box_needs_scroll(const MMTextBoxState *s, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    if (!mm_text_box_scroll_frame(s->story, s->first, s->current)) return 0;
    TRY(call(p, MM_TBOX_VIEW, &q));
    TRY(call(p, MM_TBOX_NEEDS_SCROLL, &q));
    return q.value != 0;
}
int mm_text_box_scroll(const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    TRY(call(p, MM_TBOX_VIEW, &q));
    TRY(call(p, MM_TBOX_SCROLL, &q));
    return 1;
}
int mm_text_box_text_selected(const MMTextBoxState *s, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxRequest q;
    if (!mm_text_box_visible(s)) return 0;
    TRY(call(p, MM_TBOX_VIEW, &q));
    TRY(call(p, MM_TBOX_TEXT_SELECTED, &q));
    return q.value != 0;
}
