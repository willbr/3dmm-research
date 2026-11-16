#include "text_box_edit_test_host.h"
static int invoke(int kind, BoxTestHost *h, const MMTextBoxEditServices *p)
{
    void *text = &h->objects[0];
    switch (kind)
    {
    case 0: return mm_text_box_create(&h->objects[6], 1, p);
    case 1: return mm_text_box_read_document(&h->objects[6], 1, p);
    case 2: return mm_text_box_duplicate(text, p);
    case 3: return mm_text_box_change(text, MM_BOX_UNDO_TYPE, 1, p);
    case 4: return mm_text_box_change(text, MM_BOX_UNDO_COLOR, 0x776655, p);
    case 5: return mm_text_box_show_edit(text, p);
    case 6: return mm_text_box_hide_edit(text, p);
    case 7: return mm_text_box_wrap_undo(&h->objects[5], p);
    case 8: return mm_text_box_add_edit_undo(text, &h->objects[5], p);
    case 9: return mm_text_box_clear_edit_undo(text, p);
    case 10: return mm_text_box_apply_selection(text, MM_BOX_TEXT_HEIGHT, 23, p);
    case 11: return mm_text_box_fetch_selection(text, p);
    case 12: return mm_text_box_clipboard_create(text, p);
    case 13: return mm_text_box_clipboard_dispose(&h->objects[3].clipboard, p);
    default: return mm_text_box_clipboard_paste(&h->objects[3].clipboard, p);
    }
}
int main(void)
{
    BoxTestHost h, reference;
    MMTextBoxEditServices p;
    int kind, failure, i, result, width, height;
    p = box_setup(&h); h.mutate = 1;
    CHECK(mm_text_box_create(&h.objects[6], 1, &p) == 1);
    CHECK(h.published == &h.objects[1] && h.objects[1].width == 197);
    CHECK(h.objects[1].runtime.scene == &h.objects[6] && h.objects[1].state.story == 1);
    CHECK(h.objects[1].background == p.clear && !h.flags);
    p = box_setup(&h); h.falseOp = MM_BOX_INITIALIZE;
    CHECK(!invoke(0, &h, &p) && !h.published && !h.objects[1].refs);
    p = box_setup(&h); h.falseOp = MM_BOX_READ_METADATA;
    CHECK(!invoke(1, &h, &p) && h.calls == 3 && h.events[2].op == MM_BOX_DIAGNOSTIC);
    p = box_setup(&h); h.falseOp = MM_BOX_READ_TEXT;
    CHECK(!invoke(1, &h, &p) && !h.objects[1].refs && !h.published);
    p = box_setup(&h);
    CHECK(invoke(2, &h, &p) == 1 && h.published == &h.objects[1]);
    CHECK(h.objects[1].font == 3 && h.objects[1].height == 19 && h.objects[1].count == 9 && !h.objects[1].suspended);
    p = box_setup(&h); h.falseOp = MM_BOX_REPLACE;
    CHECK(!invoke(2, &h, &p) && !h.published && !h.objects[1].refs && h.objects[1].suspended == 1);
    p = box_setup(&h); h.falseOp = MM_BOX_ADD_UNDO;
    CHECK(!invoke(3, &h, &p) && h.objects[0].state.story == 0 && !h.objects[2].refs);
    p = box_setup(&h); h.objects[0].state.story = 1;
    CHECK(invoke(3, &h, &p) == 1 && !h.calls);
    p = box_setup(&h); h.falseOp = MM_BOX_SHOW;
    CHECK(!invoke(5, &h, &p) && h.events[h.calls - 1].op == MM_BOX_SHOW);
    p = box_setup(&h); h.objects[0].state.current = 1;
    CHECK(invoke(6, &h, &p) == 1 && h.calls == 1 && h.events[0].op == MM_BOX_REMOVE);
    p = box_setup(&h); h.falseOp = MM_BOX_ADD_UNDO;
    CHECK(!invoke(8, &h, &p) && h.events[h.calls - 2].op == MM_BOX_CLEAR_UNDO && !h.objects[2].refs);
    p = box_setup(&h); h.falseOp = MM_BOX_PARENT_ADD_UNDO;
    CHECK(!invoke(8, &h, &p) && h.objects[2].refs == 1);
    for (kind = 0; kind < 4; ++kind)
    {
        p = box_setup(&h);
        CHECK(mm_text_box_apply_selection(&h.objects[0], kind, 23, &p) == 1);
        CHECK(h.character.background == p.black && h.difference.foreground == p.black);
        if (kind == MM_BOX_TEXT_HEIGHT) CHECK(h.character.height == 23 && h.difference.height == -24);
        if (kind == MM_BOX_TEXT_STYLE) CHECK(h.character.style == 23 && h.difference.style == ~23U);
        if (kind == MM_BOX_TEXT_FONT) CHECK(h.character.font == 23 && h.difference.font == -24);
        if (kind == MM_BOX_TEXT_COLOR) CHECK(h.character.foreground == 23);
    }
    p = box_setup(&h); h.selectionFirst = 8; h.selectionEnd = 0;
    CHECK(invoke(11, &h, &p) == 1 && !h.mask && h.character.font == 3 && h.character.height == 12);
    p = box_setup(&h); h.validView = 0; h.mask = 99;
    CHECK(invoke(11, &h, &p) == 1 && !h.mask);
    p = box_setup(&h); h.hasView = 0;
    CHECK(!invoke(10, &h, &p) && h.calls == 1);
    /* Every callback error leaves precisely the successful observable prefix. */
    for (kind = 0; kind < 15; ++kind)
    {
        p = box_setup(&reference); CHECK(invoke(kind, &reference, &p) >= 0);
        for (failure = 1; failure <= reference.calls; ++failure)
        {
            p = box_setup(&h); h.failAt = failure;
            result = invoke(kind, &h, &p);
            CHECK(result == -73 && h.calls == failure);
            CHECK(!memcmp(h.events, reference.events, failure * sizeof(BoxTestEvent)));
        }
    }
    /* Paste shifts the whole rectangle to (177,78), then intersects the client
     * workspace. Independent dimensions check clipping and empty normalization. */
    for (width = -20; width <= 700; width += 40) for (height = -20; height <= 500; height += 40)
    {
        p = box_setup(&h); h.workspaceWidth = width; h.workspaceHeight = height;
        CHECK(invoke(14, &h, &p) == 1);
        if (width <= 177 || height <= 78)
            CHECK(!h.objects[1].state.left && !h.objects[1].state.right && !h.objects[1].state.bottom);
        else
        {
            CHECK(h.objects[1].state.left == 177 && h.objects[1].state.top == 78);
            CHECK(h.objects[1].state.right == (width < 357 ? width : 357));
            CHECK(h.objects[1].state.bottom == (height < 158 ? height : 158));
        }
    }
    for (i = 0; i < 2; ++i)
    {
        p = box_setup(&h); h.falseOp = i ? MM_BOX_ADD : MM_BOX_DUPLICATE;
        CHECK(!invoke(14, &h, &p));
        CHECK(h.events[h.calls - (i ? 2 : 1)].op == MM_BOX_CLEAR_UNDO);
        if (i) CHECK(!h.objects[1].refs);
    }
    p = box_setup(&h); h.objects[1].state.left = (-2147483647L - 1); h.objects[1].state.right = 2147483647L;
    CHECK(invoke(14, &h, &p) == 1 && !h.objects[1].state.right);
    p = box_setup(&h); h.objects[1].state.left = 2147483647L; h.objects[1].state.right = (-2147483647L - 1);
    CHECK(invoke(14, &h, &p) == 1 && h.objects[1].state.left == 177 && h.objects[1].state.right == 178);
    puts("C89 text-box factories, transactions, property selection, clipboard geometry and failures passed");
    return 0;
}
