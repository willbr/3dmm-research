#include "text_box_edit_test_host.h"
#include "mm/movie_archive_edit.h"
#include "scene_member_fixture.h"

enum { BOX_TEST_READ_HEADER = -2 };
typedef struct BoxInput
{
    BoxTestHost *host;
    MMMemoryStream stream;
    unsigned char bytes[MM_TEXT_BOX_HEADER_BYTES];
} BoxInput;

static int read_header(void *context, MMU32 type, MMU32 number, MMReadStream *out)
{
    BoxInput *input = context;
    int status = box_step(input->host, BOX_TEST_READ_HEADER);
    if (status <= 0)
        return status;
    if (type != MM_TEXT_BOX || number != 1 ||
        !mm_text_box_encode(&input->host->objects[0].state, 0x0303, 0, input->bytes))
        return 0;
    mm_memory_stream_init(&input->stream, input->bytes, sizeof(input->bytes));
    *out = input->stream.stream;
    return 1;
}

static MMU32 box_count(void *context)
{
    (void)context;
    return 1;
}

static int box_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    (void)context;
    if (position != 0)
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->type = MM_TEXT_BOX;
    entry->number = 1;
    entry->child_count = 1;
    return 1;
}

static int box_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    (void)context;
    if (parent != 0 || position != 0)
        return 0;
    child->type = MM_TEXT_ROOT;
    child->number = 7;
    child->slot = 0;
    return 1;
}

static int read_document(BoxTestHost *h, const MMTextBoxEditServices *p)
{
    BoxInput input;
    MMMovieArchiveEditor archive = {0};
    input.host = h;
    archive.context = &input;
    archive.read = read_header;
    archive.edit.count = box_count;
    archive.edit.entry = box_entry;
    archive.edit.child = box_child;
    return mm_text_box_read_document(&h->objects[6], 1, &archive, p);
}

static int invoke(int kind, BoxTestHost *h, const MMTextBoxEditServices *p)
{
    void *text = &h->objects[0];
    switch (kind)
    {
    case 0: return mm_text_box_create(&h->objects[6], 1, p);
    case 1: return read_document(h, p);
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

typedef struct ViewClipboardTest
{
    MMTextBoxClipboardOperation calls[12];
    int count, fail, exception;
    int actor_refs, document_refs, duplicate_refs;
    int actor, route, text_selected, box_selected, can_paste_text, error;
} ViewClipboardTest;

static int clipboard_call(void *context, MMTextBoxClipboardOperation operation, MMTextBoxClipboardRequest *q)
{
    ViewClipboardTest *t = context;
    int *refs;
    t->calls[t->count++] = operation;
    if ((int)operation == t->exception)
        return -73;
    if ((int)operation == t->fail)
    {
        if (operation == MM_BOX_CLIP_CREATE)
            q->object = NULL;
        return 0;
    }
    switch (operation)
    {
    case MM_BOX_CLIP_DUPLICATE:
        q->object = &t->duplicate_refs;
        ++t->duplicate_refs;
        break;
    case MM_BOX_CLIP_CREATE:
        q->object = &t->document_refs;
        ++t->document_refs;
        break;
    case MM_BOX_CLIP_PUBLISH:
        ++t->document_refs;
        break;
    case MM_BOX_CLIP_GET_ACTOR:
        if (!t->actor)
            return 0;
        q->object = &t->actor_refs;
        ++t->actor_refs;
        break;
    case MM_BOX_CLIP_RELEASE:
        refs = q->object;
        --*refs;
        if (refs == &t->document_refs && !*refs)
            --t->duplicate_refs;
        break;
    case MM_BOX_CLIP_ROUTE_ONLY: return t->route;
    case MM_BOX_CLIP_TEXT_SELECTED: return t->text_selected;
    case MM_BOX_CLIP_BOX_SELECTED: return t->box_selected;
    case MM_BOX_CLIP_EMPTY: return !t->can_paste_text;
    case MM_BOX_CLIP_CAN_PASTE_TEXT: return t->can_paste_text;
    case MM_BOX_CLIP_HAS_BOX: return 0;
    case MM_BOX_CLIP_DIAGNOSTIC:
        t->error = q->action;
        break;
    default:
        break;
    }
    return 1;
}

static void clipboard_init(ViewClipboardTest *t)
{
    memset(t, 0, sizeof(*t));
    t->fail = t->exception = t->error = -1;
}

static int clipboard_view(void)
{
    static const MMTextBoxClipboardOperation cut[] = {
        MM_BOX_CLIP_DUPLICATE, MM_BOX_CLIP_CREATE, MM_BOX_CLIP_HIDE,
        MM_BOX_CLIP_PUBLISH, MM_BOX_CLIP_RELEASE, MM_BOX_CLIP_CUE
    };
    ViewClipboardTest t;
    MMTextBoxClipboardServices s;
    s.context = &t;
    s.call = clipboard_call;

    clipboard_init(&t);
    t.fail = MM_BOX_CLIP_CREATE;
    CHECK(!mm_text_box_clipboard_click(MM_BOX_CLIP_CUT, 0, &s));
    CHECK(t.count == 3 && t.calls[2] == MM_BOX_CLIP_RELEASE && !t.duplicate_refs && !t.document_refs);
    clipboard_init(&t);
    t.fail = MM_BOX_CLIP_HIDE;
    CHECK(!mm_text_box_clipboard_click(MM_BOX_CLIP_CUT, 0, &s));
    CHECK(t.count == 4 && t.calls[3] == MM_BOX_CLIP_RELEASE && !t.duplicate_refs && !t.document_refs);
    clipboard_init(&t);
    CHECK(mm_text_box_clipboard_click(MM_BOX_CLIP_CUT, 0, &s) == 1);
    CHECK(t.count == 6 && !memcmp(t.calls, cut, sizeof(cut)) && t.duplicate_refs == 1 && t.document_refs == 1);

    clipboard_init(&t);
    t.actor = t.route = 1;
    CHECK(mm_text_box_clipboard_command(MM_BOX_CLIP_PASTE_TOOL, &s) == 1);
    CHECK(t.count == 4 && t.calls[2] == MM_BOX_CLIP_DIAGNOSTIC && t.error == MM_BOX_CLIP_UNSUPPORTED &&
          t.calls[3] == MM_BOX_CLIP_RELEASE && !t.actor_refs);
    t.count = t.route = 0;
    CHECK(mm_text_box_clipboard_command(MM_BOX_CLIP_PASTE_TOOL, &s) == 1);
    CHECK(t.count == 4 && t.calls[2] == MM_BOX_CLIP_FORWARD && t.calls[3] == MM_BOX_CLIP_RELEASE && !t.actor_refs);

    clipboard_init(&t);
    t.text_selected = 1;
    t.fail = MM_BOX_CLIP_EDIT_TEXT;
    CHECK(!mm_text_box_clipboard_click(MM_BOX_CLIP_COPY, 1, &s));
    CHECK(t.count == 4 && t.calls[1] == MM_BOX_CLIP_CUE && t.calls[3] == MM_BOX_CLIP_COLLAPSE);
    t.count = 0;
    t.exception = MM_BOX_CLIP_EDIT_TEXT;
    CHECK(mm_text_box_clipboard_click(MM_BOX_CLIP_COPY, 1, &s) == -73 && t.count == 3);

    clipboard_init(&t);
    CHECK(mm_text_box_clipboard_enable(MM_BOX_CLIP_COPY, &s) == MM_BOX_CLIP_USE_PARENT && !t.count);
    CHECK(mm_text_box_clipboard_enable(MM_BOX_CLIP_COPY_TOOL, &s) == MM_BOX_CLIP_ENABLED && !t.count);
    CHECK(mm_text_box_clipboard_enable(MM_BOX_CLIP_PASTE, &s) == MM_BOX_CLIP_DISABLED);
    t.count = 0;
    t.box_selected = t.can_paste_text = 1;
    CHECK(mm_text_box_clipboard_enable(MM_BOX_CLIP_PASTE_TOOL, &s) == MM_BOX_CLIP_ENABLED);
    CHECK(t.count == 3 && t.calls[2] == MM_BOX_CLIP_CAN_PASTE_TEXT);
    return 0;
}

static const MMSceneOwner *box_scene_owner(void *context, void *text)
{
    (void)context;
    return ((TestBox *)text)->runtime.scene;
}

static int live_roster(void)
{
    BoxTestHost h;
    MMTextBoxEditServices p = box_setup(&h);
    MMSceneOwner owner;
    SceneMemberMemory memory = {0};
    MMI32 index = -1;
    p.scene_owner = box_scene_owner;
    CHECK(!mm_text_box_index(&h.objects[0], &index, &p) && index == -1 && !h.calls);
    init_scene_members(&owner, &memory);
    h.objects[0].runtime.scene = &owner;
    CHECK(mm_scene_member_append(&owner, MM_SCENE_TEXTS, &h.objects[3]));
    CHECK(!invoke(3, &h, &p) && !invoke(8, &h, &p) && !h.calls);
    CHECK(mm_scene_member_append(&owner, MM_SCENE_TEXTS, &h.objects[0]));
    CHECK(mm_text_box_index(&h.objects[0], &index, &p) == 1 && index == 1);
    CHECK(invoke(3, &h, &p) == 1 && h.objects[2].undo.index == 1 && h.objects[0].state.story);
    return 0;
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
    p = box_setup(&h);
    CHECK(invoke(1, &h, &p) == 1 && h.published == &h.objects[1]);
    CHECK(h.objects[1].state.first == 1 && h.objects[1].state.current == 0 &&
          h.objects[1].runtime.scene == &h.objects[6]);
    p = box_setup(&h); h.falseOp = BOX_TEST_READ_HEADER;
    CHECK(!invoke(1, &h, &p) && h.calls == 3 && h.events[2].op == MM_BOX_DIAGNOSTIC);
    p = box_setup(&h); h.objects[0].state.child = 4;
    CHECK(!invoke(1, &h, &p) && !h.published && h.calls == 3 && h.events[2].op == MM_BOX_DIAGNOSTIC);
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
        p = box_setup(&h); h.falseOp = i ? MM_BOX_ADD : MM_BOX_REPLACE;
        CHECK(!invoke(14, &h, &p));
        CHECK(h.events[h.calls - (i ? 2 : 1)].op == MM_BOX_CLEAR_UNDO);
        CHECK(!h.objects[1].refs);
    }
    p = box_setup(&h); h.objects[1].state.left = (-2147483647L - 1); h.objects[1].state.right = 2147483647L;
    CHECK(invoke(14, &h, &p) == 1 && !h.objects[1].state.right);
    p = box_setup(&h); h.objects[1].state.left = 2147483647L; h.objects[1].state.right = (-2147483647L - 1);
    CHECK(invoke(14, &h, &p) == 1 && h.objects[1].state.left == 177 && h.objects[1].state.right == 178);
    CHECK(!clipboard_view());
    CHECK(!live_roster());
    puts("C89 text-box factories, transactions, property selection, clipboard commands and failures passed");
    return 0;
}
