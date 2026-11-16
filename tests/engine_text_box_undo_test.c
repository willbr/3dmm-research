#include "text_box_edit_test_host.h"
int main(void)
{
    BoxTestHost h, reference;
    MMTextBoxEditServices p;
    MMTextBoxUndo *u;
    int kind, failure, result, i;
    const int initial[] = {MM_BOX_UNDO_SCENE, MM_BOX_SWITCH_SCENE, MM_BOX_UNDO_FRAME, MM_BOX_SEEK_SCENE, MM_BOX_SCENE_BOX};
    for (kind = 0; kind < 5; ++kind)
    {
        p = box_setup(&h); u = &h.objects[2].undo; u->kind = kind; u->story = 1; u->color = 0x665544;
        u->bounds.left = 3; u->bounds.top = 4; u->bounds.right = 100; u->bounds.bottom = 90;
        u->first = 2; u->end = 9;
        CHECK(mm_text_box_undo_replay(u, 1, &p) == 1);
        for (i = 0; i < 5; ++i) CHECK(h.events[i].op == initial[i]);
        if (kind == MM_BOX_UNDO_TYPE) CHECK(h.objects[0].state.story == 1 && !u->story);
        if (kind == MM_BOX_UNDO_COLOR) CHECK(h.objects[0].background == 0x665544 && u->color == 0x112233);
        if (kind == MM_BOX_UNDO_TYPE) CHECK(!h.events[h.calls - 2].savedStory);
        if (kind == MM_BOX_UNDO_COLOR) CHECK(h.events[h.calls - 2].savedColor == 0x112233);
        if (kind == MM_BOX_UNDO_SIZE)
        {
            CHECK(h.objects[0].state.left == 3 && h.positioned.left == 3 && u->bounds.left == 10);
            CHECK(h.events[h.calls - 2].op == MM_BOX_SELECT);
            CHECK(h.events[h.calls - 2].savedLeft == 3);
        }
        if (kind == MM_BOX_UNDO_HIDE)
        {
            CHECK(u->first == 1 && u->end == 10 && h.objects[0].state.current == 7);
            CHECK(h.events[h.calls - 2].savedFirst == 2 && h.events[h.calls - 2].savedEnd == 9);
        }
        if (kind == MM_BOX_UNDO_EDIT) CHECK(h.mask == 1 && h.events[h.calls - 2].op == MM_BOX_FLUSH);
        p = box_setup(&reference); reference.objects[2].undo.kind = kind;
        CHECK(mm_text_box_undo_replay(&reference.objects[2].undo, 0, &p) == 1);
        for (failure = 1; failure <= reference.calls; ++failure)
        {
            p = box_setup(&h); h.objects[2].undo.kind = kind; h.failAt = failure;
            result = mm_text_box_undo_replay(&h.objects[2].undo, 0, &p);
            CHECK(result == -73 && h.calls == failure);
            CHECK(!memcmp(h.events, reference.events, failure * sizeof(BoxTestEvent)));
        }
    }
    for (i = 0; i < 3; ++i)
    {
        p = box_setup(&h); u = &h.objects[2].undo;
        if (i < 2) h.falseOp = i ? MM_BOX_SEEK_SCENE : MM_BOX_SWITCH_SCENE;
        else h.index = 1; /* missing saved text index */
        CHECK(!mm_text_box_undo_replay(u, 0, &p));
        CHECK(h.events[h.calls - 2].op == MM_BOX_FLUSH && h.events[h.calls - 1].op == MM_BOX_CLEAR_UNDO);
    }
    p = box_setup(&h); u = &h.objects[2].undo; u->kind = MM_BOX_UNDO_EDIT; h.falseOp = MM_BOX_REPLAY_EDIT;
    CHECK(!mm_text_box_undo_replay(u, 1, &p) && h.events[h.calls - 1].op == MM_BOX_REPLAY_EDIT);
    p = box_setup(&h); u = &h.objects[2].undo; u->kind = MM_BOX_UNDO_HIDE;
    h.falseOp = MM_BOX_GOTO; h.falseAt = 3;
    CHECK(mm_text_box_undo_replay(u, 0, &p) == 1 && h.events[h.calls - 1].op == MM_BOX_FLUSH);
    p = box_setup(&h); u = &h.objects[2].undo; u->kind = MM_BOX_UNDO_EDIT;
    CHECK(mm_text_box_undo_dispose(u, &p) == 1 && !u->edit && !h.objects[5].refs);
    CHECK(h.events[3].op == MM_BOX_PARENT_CLEAR_UNDO && h.events[4].op == MM_BOX_RELEASE);
    p = box_setup(&h); u = &h.objects[2].undo; u->kind = MM_BOX_UNDO_EDIT; h.currentScene = 3;
    CHECK(mm_text_box_undo_dispose(u, &p) == 1 && h.calls == 3 && h.events[2].op == MM_BOX_RELEASE);
    p = box_setup(&h); u = &h.objects[2].undo; u->kind = MM_BOX_UNDO_EDIT; h.index = 1;
    CHECK(mm_text_box_undo_dispose(u, &p) == 1 && h.calls == 4 && h.events[3].op == MM_BOX_RELEASE);
    /* Clearing movie undo is allowed to destroy the record running replay. */
    p = box_setup(&h); h.activeUndo = (MMTextBoxUndo *)calloc(1, sizeof(*h.activeUndo));
    CHECK(h.activeUndo != NULL); h.freeUndo = 1; h.falseOp = MM_BOX_SWITCH_SCENE;
    CHECK(!mm_text_box_undo_replay(h.activeUndo, 0, &p) && h.freed && !h.activeUndo);
    puts("C89 text-box undo replay, swap stages, ordinary/exception failures and self-destruction passed");
    return 0;
}
