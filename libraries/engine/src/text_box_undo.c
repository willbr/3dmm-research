/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_box_undo.h"
#define TRY(e) do { int status_ = (e); if (status_ < 0) return status_; } while (0)
#define CALL(op) p->call(p->context, op, &q)
static int failed(const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    TRY(CALL(MM_BOX_FLUSH)); TRY(CALL(MM_BOX_CLEAR_UNDO));
    return 0; /* clear may have destroyed the undo record */
}
int mm_text_box_undo_replay(MMTextBoxUndo *u, int undo, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxBounds oldBounds;
    MMTextBoxState *state;
    MMI32 first, end;
    MMU32 color;
    int result, story, kind = u->kind;
    void *text;
    TRY(CALL(MM_BOX_UNDO_SCENE)); result = CALL(MM_BOX_SWITCH_SCENE); TRY(result);
    if (!result) return failed(p);
    TRY(CALL(MM_BOX_UNDO_FRAME)); result = CALL(MM_BOX_SEEK_SCENE); TRY(result);
    if (!result) return failed(p);
    q.value = u->index; q.other = NULL; TRY(CALL(MM_BOX_SCENE_BOX)); text = q.object;
    if (!text) return failed(p);
    switch (kind)
    {
    case MM_BOX_UNDO_TYPE:
        TRY(CALL(MM_BOX_STORY)); story = q.value;
        q.value = u->story; TRY(CALL(MM_BOX_SET_TYPE)); u->story = story;
        break;
    case MM_BOX_UNDO_SIZE:
        TRY(CALL(MM_BOX_GET_BOUNDS)); oldBounds = q.bounds;
        q.bounds = u->bounds; TRY(CALL(MM_BOX_SET_BOUNDS));
        q.bounds = u->bounds; TRY(CALL(MM_BOX_POSITION_VIEW));
        TRY(CALL(MM_BOX_SELECT)); u->bounds = oldBounds;
        TRY(CALL(MM_BOX_FLUSH)); return 1;
    case MM_BOX_UNDO_HIDE:
        state = p->state(p->context, text); first = state->first; end = state->end;
        q.value = u->first; result = CALL(MM_BOX_GOTO); TRY(result);
        if (!result) return failed(p);
        result = CALL(MM_BOX_SHOW); TRY(result);
        if (!result) return failed(p);
        q.value = u->end; result = CALL(MM_BOX_GOTO); TRY(result);
        if (!result) return failed(p);
        TRY(CALL(MM_BOX_HIDE));
        TRY(CALL(MM_BOX_UNDO_FRAME)); TRY(CALL(MM_BOX_GOTO)); /* ordinary false is ignored */
        TRY(CALL(MM_BOX_SELECT)); u->first = first; u->end = end;
        TRY(CALL(MM_BOX_FLUSH)); return 1;
    case MM_BOX_UNDO_EDIT:
        TRY(CALL(MM_BOX_SELECT)); TRY(CALL(MM_BOX_FLUSH));
        q.object = u->edit; q.other = text; q.value = undo != 0;
        return CALL(MM_BOX_REPLAY_EDIT);
    default:
        TRY(CALL(MM_BOX_BACKGROUND)); color = q.word;
        q.word = u->color; q.flags = p->update; TRY(CALL(MM_BOX_SET_BACKGROUND)); u->color = color;
        break;
    }
    TRY(CALL(MM_BOX_SELECT)); TRY(CALL(MM_BOX_FLUSH)); return 1;
}
int mm_text_box_undo_dispose(MMTextBoxUndo *u, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMI32 scene;
    if (u->kind != MM_BOX_UNDO_EDIT) return 1;
    TRY(CALL(MM_BOX_UNDO_SCENE)); scene = q.value;
    TRY(CALL(MM_BOX_CURRENT_SCENE));
    if (q.value == scene)
    {
        q.value = u->index; q.other = NULL; TRY(CALL(MM_BOX_SCENE_BOX));
        if (q.object) TRY(CALL(MM_BOX_PARENT_CLEAR_UNDO));
    }
    q.object = u->edit;
    if (q.object) { TRY(CALL(MM_BOX_RELEASE)); u->edit = NULL; }
    return 1;
}
