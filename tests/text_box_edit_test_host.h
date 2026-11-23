#ifndef TEXT_BOX_EDIT_TEST_HOST_H
#define TEXT_BOX_EDIT_TEST_HOST_H
#include "mm/text_box_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { printf("box check failed at %d: %s\n", __LINE__, #e); return 1; } } while (0)
typedef struct TestBox
{
    MMTextBoxState state;
    MMTextBoxRuntime runtime;
    MMTextBoxUndo undo;
    MMTextBoxClipboard clipboard;
    int refs, suspended;
    MMI32 count, font, height, width;
    MMU32 background;
} TestBox;
typedef struct BoxTestEvent
{
    int op, object, other;
    MMI32 first, end, current;
    int story, savedStory;
    MMU32 savedColor;
    MMI32 savedLeft, savedFirst, savedEnd;
} BoxTestEvent;
typedef struct BoxTestHost
{
    TestBox objects[8];
    BoxTestEvent events[128];
    int calls, failAt, error, falseOp, falseAt, falseCount, nullOp, index, hasView, validView, hasBounds;
    int freeUndo, freed, mutate;
    MMI32 selectionFirst, selectionEnd, workspaceWidth, workspaceHeight, currentScene;
    MMTextBoxUndo *activeUndo;
    MMTextBoxBounds input, positioned;
    void *published;
    MMU32 mask, flags;
    MMTextCharacter character, difference;
} BoxTestHost;
static MMTextBoxState *box_state(void *v, void *p) { (void)v; return &((TestBox *)p)->state; }
static MMTextBoxRuntime *box_runtime(void *v, void *p) { (void)v; return &((TestBox *)p)->runtime; }
static MMTextBoxUndo *box_undo(void *v, void *p) { (void)v; return &((TestBox *)p)->undo; }
static MMTextBoxClipboard *box_clipboard(void *v, void *p) { (void)v; return &((TestBox *)p)->clipboard; }
static int box_step(BoxTestHost *h, int op)
{
    BoxTestEvent *e = &h->events[h->calls++];
    memset(e, 0, sizeof(*e));
    e->op = op; e->first = h->objects[0].state.first; e->end = h->objects[0].state.end;
    e->current = h->objects[0].state.current; e->story = h->objects[0].state.story;
    if (h->activeUndo)
    {
        e->savedStory = h->activeUndo->story; e->savedColor = h->activeUndo->color;
        e->savedLeft = h->activeUndo->bounds.left; e->savedFirst = h->activeUndo->first; e->savedEnd = h->activeUndo->end;
    }
    if (h->calls == h->failAt) return h->error;
    if (op == h->falseOp && ++h->falseCount == h->falseAt) return 0;
    return 1;
}
static int box_call(void *context, MMTextBoxEditOperation op, MMTextBoxEditRequest *q)
{
    BoxTestHost *h = context;
    TestBox *b;
    int status = box_step(h, op);
    if (status <= 0)
        return status;
    switch (op)
    {
    case MM_BOX_ALLOCATE: case MM_BOX_NEW_COPY:
        q->object = h->nullOp == (int)op ? NULL : &h->objects[1]; break;
    case MM_BOX_ALLOCATE_UNDO:
        h->objects[2].undo.kind = q->value;
        q->object = h->nullOp == (int)op ? NULL : &h->objects[2]; break;
    case MM_BOX_ALLOCATE_CLIPBOARD:
        q->object = h->nullOp == (int)op ? NULL : &h->objects[3]; break;
    case MM_BOX_INITIALIZE: if (h->mutate) h->input.left = -17; break;
    case MM_BOX_INPUT_BOUNDS: q->value = h->hasBounds; q->bounds = h->input; break;
    case MM_BOX_RELEASE: --((TestBox *)q->object)->refs; break;
    case MM_BOX_RETAIN: ++((TestBox *)q->object)->refs; break;
    case MM_BOX_DEFAULTS: b = (TestBox *)q->object; q->value = b->font; q->second = b->height; break;
    case MM_BOX_SET_DEFAULTS: b = (TestBox *)q->object; b->font = q->value; b->height = q->second; break;
    case MM_BOX_SET_WIDTH: ((TestBox *)q->object)->width = q->value; break;
    case MM_BOX_BACKGROUND: q->word = ((TestBox *)q->object)->background; break;
    case MM_BOX_SET_BACKGROUND: ((TestBox *)q->object)->background = q->word; h->flags = q->flags; break;
    case MM_BOX_UNDO_MAXIMUM: q->value = 1; break;
    case MM_BOX_SET_UNDO_LIMIT: break;
    case MM_BOX_PUBLISH: h->published = q->object; break;
    case MM_BOX_READ_TEXT: return q->chunk.type == MM_TEXT_ROOT && q->chunk.number == 7;
    case MM_BOX_SUSPEND_UNDO: ++((TestBox *)q->object)->suspended; break;
    case MM_BOX_RESUME_UNDO: --((TestBox *)q->object)->suspended; break;
    case MM_BOX_CHARACTERS: q->value = ((TestBox *)q->object)->count; break;
    case MM_BOX_REPLACE: ((TestBox *)q->object)->count = ((TestBox *)q->other)->count; break;
    case MM_BOX_SET_TYPE: ((TestBox *)q->object)->state.story = q->value; break;
    case MM_BOX_ADD_UNDO: ++((TestBox *)q->object)->refs; break;
    case MM_BOX_CLEAR_UNDO:
        if (h->freeUndo) { free(h->activeUndo); h->activeUndo = NULL; h->freed = 1; }
        break;
    case MM_BOX_SCENE_BOX: q->object = q->value == h->index ? &h->objects[0] : NULL; break;
    case MM_BOX_VIEW: case MM_BOX_ACTIVE_VIEW: q->object = h->hasView ? &h->objects[4] : NULL; break;
    case MM_BOX_CHECK_TEXT_VIEW: q->value = h->validView; break;
    case MM_BOX_APPLY_CHARACTER: h->character = q->character; h->difference = q->difference; break;
    case MM_BOX_SELECTION: q->value = h->selectionFirst; q->second = h->selectionEnd; break;
    case MM_BOX_FETCH_CHARACTER:
        memset(&q->character, 0, sizeof(q->character)); q->character.font = q->value < 2 ? 3 : 4;
        q->character.height = q->value < 4 ? 12 : 17; q->character.style = q->value < 6 ? 3 : 0;
        q->second = q->value + 2; break;
    case MM_BOX_PUBLISH_CHARACTER: h->character = q->character; break;
    case MM_BOX_PUBLISH_MASK: h->mask = q->word; break;
    case MM_BOX_WORKSPACE: q->value = h->workspaceWidth; q->second = h->workspaceHeight; break;
    case MM_BOX_DUPLICATE: q->object = &h->objects[1]; break;
    case MM_BOX_GET_BOUNDS:
        b = (TestBox *)q->object; q->bounds.left = b->state.left; q->bounds.top = b->state.top;
        q->bounds.right = b->state.right; q->bounds.bottom = b->state.bottom; break;
    case MM_BOX_SET_BOUNDS:
        b = (TestBox *)q->object; b->state.left = q->bounds.left; b->state.top = q->bounds.top;
        b->state.right = q->bounds.right; b->state.bottom = q->bounds.bottom; break;
    case MM_BOX_UNDO_SCENE: q->value = 2; break;
    case MM_BOX_UNDO_FRAME: q->value = 7; break;
    case MM_BOX_CURRENT_SCENE: q->value = h->currentScene; break;
    case MM_BOX_STORY: q->value = ((TestBox *)q->object)->state.story; break;
    case MM_BOX_POSITION_VIEW: h->positioned = q->bounds; break;
    case MM_BOX_GOTO: ((TestBox *)q->object)->state.current = q->value; break;
    case MM_BOX_REPLAY_EDIT: h->mask = q->value; break;
    default: break;
    }
    return 1;
}
static MMTextBoxEditServices box_setup(BoxTestHost *h)
{
    MMTextBoxEditServices p;
    int i;
    memset(h, 0, sizeof(*h)); memset(&p, 0, sizeof(p));
    h->falseOp = h->nullOp = -1; h->falseAt = 1; h->error = -73;
    h->hasView = h->validView = h->hasBounds = 1;
    h->input.left = 10; h->input.top = 20; h->input.right = 190; h->input.bottom = 100;
    h->workspaceWidth = 640; h->workspaceHeight = 480; h->selectionEnd = 8;
    h->currentScene = 2;
    for (i = 0; i < 8; ++i)
    {
        h->objects[i].refs = 1; h->objects[i].count = i ? 1 : 9;
        h->objects[i].state.first = 1; h->objects[i].state.end = 10; h->objects[i].state.current = 7;
        h->objects[i].state.left = 10; h->objects[i].state.top = 20;
        h->objects[i].state.right = 190; h->objects[i].state.bottom = 100;
    }
    h->objects[0].font = 3; h->objects[0].height = 19; h->objects[0].background = 0x112233;
    h->activeUndo = &h->objects[2].undo;
    h->objects[2].undo.edit = &h->objects[5];
    h->objects[3].clipboard.text = &h->objects[0];
    p.context = h; p.call = box_call; p.state = box_state; p.runtime = box_runtime;
    p.undo = box_undo; p.clipboard = box_clipboard; p.black = 0x01000000; p.clear = 0xffffffffU; p.update = 8;
    return p;
}
#endif
