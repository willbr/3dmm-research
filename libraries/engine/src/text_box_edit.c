/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_box_edit.h"
#include "mm/wide.h"
#include <string.h>
#define TRY(e) do { int status_ = (e); if (status_ < 0) return status_; } while (0)
#define CALL(op) p->call(p->context, op, &q)
static int publish(void *object, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q; q.object = object; return CALL(MM_BOX_PUBLISH);
}
static int release(void *object, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q; q.object = object;
    return object ? CALL(MM_BOX_RELEASE) : 1;
}
int mm_text_box_create(void *scene, int story, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxState *state;
    void *box;
    int result;
    TRY(publish(NULL, p));
    TRY(CALL(MM_BOX_ALLOCATE)); box = q.object;
    if (!box) return 0;
    result = CALL(MM_BOX_INITIALIZE); TRY(result);
    if (!result) { TRY(release(box, p)); return 0; }
    TRY(CALL(MM_BOX_INPUT_BOUNDS));
    if (q.value)
    {
        state = p->state(p->context, box);
        state->left = q.bounds.left; state->top = q.bounds.top;
        state->right = q.bounds.right; state->bottom = q.bounds.bottom;
        q.value = mm_signed_word((MMU32)state->right - (MMU32)state->left - 10U);
        q.object = box; TRY(CALL(MM_BOX_SET_WIDTH));
    }
    p->runtime(p->context, box)->scene = scene;
    p->state(p->context, box)->story = story != 0;
    q.object = box; q.word = p->clear; q.flags = 0;
    TRY(CALL(MM_BOX_SET_BACKGROUND));
    if (scene)
    {
        q.object = box; q.other = scene; TRY(CALL(MM_BOX_UNDO_MAXIMUM));
        if (q.value != 1) { q.value = MM_BOX_UNDO_LIMIT; TRY(CALL(MM_BOX_DIAGNOSTIC)); }
    }
    q.object = box; q.value = 1; TRY(CALL(MM_BOX_SET_UNDO_LIMIT));
    return publish(box, p);
}
int mm_text_box_read_document(void *scene, MMU32 number, const struct MMMovieArchiveEditor *archive,
                              const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxState metadata;
    MMTextChunk chunk;
    void *box;
    int result;
    TRY(publish(NULL, p));
    result = mm_text_box_read(&metadata, number, &chunk, archive); TRY(result);
    if (!result) { q.value = MM_BOX_BAD_FILE; TRY(CALL(MM_BOX_DIAGNOSTIC)); return 0; }
    TRY(CALL(MM_BOX_ALLOCATE)); box = q.object;
    if (!box) return 0;
    q.chunk = chunk; result = CALL(MM_BOX_READ_TEXT); TRY(result);
    if (!result) { TRY(release(box, p)); return 0; }
    p->runtime(p->context, box)->scene = scene;
    *p->state(p->context, box) = metadata;
    return publish(box, p);
}
int mm_text_box_duplicate(void *source, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxState *state = p->state(p->context, source);
    void *copy;
    MMI32 characters;
    int result;
    q.bounds.left = state->left; q.bounds.top = state->top;
    q.bounds.right = state->right; q.bounds.bottom = state->bottom;
    TRY(CALL(MM_BOX_NEW_COPY)); copy = q.object;
    TRY(publish(copy, p));
    if (!copy) return 0;
    q.object = copy; TRY(CALL(MM_BOX_SUSPEND_UNDO));
    q.object = source; TRY(CALL(MM_BOX_CHARACTERS)); characters = q.value;
    q.object = copy; TRY(CALL(MM_BOX_CHARACTERS));
    q.second = q.value; q.value = characters; q.other = source;
    result = CALL(MM_BOX_REPLACE); TRY(result);
    if (!result) { TRY(release(copy, p)); TRY(publish(NULL, p)); return 0; }
    q.object = source; TRY(CALL(MM_BOX_DEFAULTS));
    q.object = copy; TRY(CALL(MM_BOX_SET_DEFAULTS));
    TRY(CALL(MM_BOX_RESUME_UNDO));
    q.object = source; TRY(CALL(MM_BOX_BACKGROUND));
    q.object = copy; q.flags = 0; TRY(CALL(MM_BOX_SET_BACKGROUND));
    q.value = p->state(p->context, source)->story != 0; TRY(CALL(MM_BOX_SET_TYPE));
    return 1;
}
int mm_text_box_index(void *text, MMI32 *out, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMI32 index = 0;
    for (;;)
    {
        q.other = text; q.value = index; TRY(CALL(MM_BOX_SCENE_BOX));
        if (q.object == text) { *out = index; return 1; }
        index = mm_signed_word((MMU32)index + 1U);
    }
}
static int record(void *text, int kind, MMU32 value, void **out, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxUndo *undo;
    MMTextBoxState *state;
    MMI32 index;
    q.value = kind; TRY(CALL(MM_BOX_ALLOCATE_UNDO)); *out = q.object;
    if (!q.object) return 0;
    undo = p->undo(p->context, q.object);
    undo->kind = kind;
    if (kind == MM_BOX_UNDO_TYPE) undo->story = !value;
    if (kind == MM_BOX_UNDO_HIDE)
    {
        state = p->state(p->context, text); undo->end = state->end; undo->first = state->first;
    }
    TRY(mm_text_box_index(text, &index, p)); undo->index = index;
    if (kind == MM_BOX_UNDO_COLOR)
    {
        q.object = text; TRY(CALL(MM_BOX_BACKGROUND)); undo->color = q.word;
    }
    q.object = *out; q.other = text;
    { int result = CALL(MM_BOX_ADD_UNDO); TRY(result);
      if (!result) { TRY(release(*out, p)); return 0; } }
    TRY(release(*out, p));
    return 1;
}
int mm_text_box_change(void *text, int kind, MMU32 value, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    void *undo;
    int result;
    if (kind == MM_BOX_UNDO_TYPE && (p->state(p->context, text)->story != 0) == (value != 0)) return 1;
    result = record(text, kind, value, &undo, p); TRY(result);
    if (!result) return 0;
    q.object = text;
    if (kind == MM_BOX_UNDO_TYPE) { q.value = value != 0; return CALL(MM_BOX_SET_TYPE); }
    q.word = value; q.flags = p->update; return CALL(MM_BOX_SET_BACKGROUND);
}
int mm_text_box_show_edit(void *text, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    void *undo;
    int result = record(text, MM_BOX_UNDO_HIDE, 0, &undo, p); TRY(result);
    if (!result) return 0;
    q.object = text; result = CALL(MM_BOX_SHOW); TRY(result);
    if (result) { q.object = text; TRY(CALL(MM_BOX_CLEAR_UNDO)); return 1; }
    return 0;
}
int mm_text_box_hide_edit(void *text, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxState *state = p->state(p->context, text);
    void *undo;
    int result;
    if (state->current <= state->first) { q.object = text; return CALL(MM_BOX_REMOVE); }
    result = record(text, MM_BOX_UNDO_HIDE, 0, &undo, p); TRY(result);
    if (!result) return 0;
    q.object = text; TRY(CALL(MM_BOX_HIDE)); return 1;
}
int mm_text_box_wrap_undo(void *edit, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    void *record;
    TRY(publish(NULL, p));
    q.value = MM_BOX_UNDO_EDIT; TRY(CALL(MM_BOX_ALLOCATE_UNDO)); record = q.object;
    if (!record) return 0;
    p->undo(p->context, record)->kind = MM_BOX_UNDO_EDIT;
    p->undo(p->context, record)->edit = edit;
    q.object = edit; TRY(CALL(MM_BOX_RETAIN));
    return publish(record, p);
}
int mm_text_box_add_edit_undo(void *text, void *edit, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxUndo *undo;
    void *record;
    MMI32 index;
    int result;
    q.value = MM_BOX_UNDO_EDIT; TRY(CALL(MM_BOX_ALLOCATE_UNDO)); record = q.object;
    if (!record) return 0;
    undo = p->undo(p->context, record); undo->kind = MM_BOX_UNDO_EDIT; undo->edit = edit;
    q.object = edit; TRY(CALL(MM_BOX_RETAIN));
    TRY(mm_text_box_index(text, &index, p)); undo->index = index;
    q.object = record; q.other = text; result = CALL(MM_BOX_ADD_UNDO); TRY(result);
    if (!result)
    {
        q.object = text; TRY(CALL(MM_BOX_CLEAR_UNDO)); TRY(release(record, p)); return 0;
    }
    q.object = text; q.other = edit; result = CALL(MM_BOX_PARENT_ADD_UNDO); TRY(result);
    TRY(release(record, p));
    return result;
}
int mm_text_box_clear_edit_undo(void *text, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    q.object = text; TRY(CALL(MM_BOX_PARENT_CLEAR_UNDO));
    return CALL(MM_BOX_CLEAR_UNDO);
}
int mm_text_box_apply_selection(void *text, int kind, MMU32 value, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    q.object = text; TRY(CALL(MM_BOX_VIEW));
    if (!q.object) return 0;
    if (kind != MM_BOX_TEXT_COLOR)
    {
        q.value = 1;
        TRY(CALL(MM_BOX_CHECK_TEXT_VIEW));
        if (!q.value) { q.value = MM_BOX_BAD_TEXT_VIEW; TRY(CALL(MM_BOX_DIAGNOSTIC)); }
    }
    memset(&q.character, 0, sizeof(q.character)); q.character.foreground = q.character.background = p->black;
    q.difference = q.character;
    switch (kind)
    {
    case MM_BOX_TEXT_COLOR: q.character.foreground = value; break;
    case MM_BOX_TEXT_HEIGHT: q.character.height = mm_signed_word(value); q.difference.height = mm_signed_word(~value); break;
    case MM_BOX_TEXT_STYLE: q.character.style = value; q.difference.style = ~value; break;
    default: q.character.font = mm_signed_word(value); q.difference.font = mm_signed_word(~value); break;
    }
    return CALL(MM_BOX_APPLY_CHARACTER);
}
int mm_text_box_fetch_selection(void *text, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextCharacter first;
    MMI32 cp, end, next;
    MMU32 mask = 15;
    q.object = text; TRY(CALL(MM_BOX_ACTIVE_VIEW));
    if (q.object)
    {
        q.value = 0;
        TRY(CALL(MM_BOX_CHECK_TEXT_VIEW));
        if (!q.value) { q.value = MM_BOX_BAD_TEXT_VIEW; TRY(CALL(MM_BOX_DIAGNOSTIC)); }
        else
        {
            TRY(CALL(MM_BOX_SELECTION)); cp = q.value; end = q.second;
            if (cp > end) { MMI32 swap = cp; cp = end; end = swap; }
            q.word = mask; TRY(CALL(MM_BOX_PUBLISH_MASK));
            q.object = text; q.value = cp; TRY(CALL(MM_BOX_FETCH_CHARACTER));
            first = q.character; next = q.second; TRY(CALL(MM_BOX_PUBLISH_CHARACTER));
            while (next < end && mask)
            {
                q.object = text; q.value = next; TRY(CALL(MM_BOX_FETCH_CHARACTER)); next = q.second;
                if ((mask & 1) && first.font != q.character.font) mask ^= 1;
                if ((mask & 2) && first.height != q.character.height) mask ^= 2;
                if ((mask & 4) && ((first.style ^ q.character.style) & 1)) mask ^= 4;
                if ((mask & 8) && ((first.style ^ q.character.style) & 2)) mask ^= 8;
                q.word = mask; TRY(CALL(MM_BOX_PUBLISH_MASK));
            }
            return 1;
        }
    }
    q.word = 0; return CALL(MM_BOX_PUBLISH_MASK);
}
int mm_text_box_clipboard_create(void *text, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    TRY(publish(NULL, p)); TRY(CALL(MM_BOX_ALLOCATE_CLIPBOARD));
    if (!q.object) return 0;
    p->clipboard(p->context, q.object)->text = text;
    return publish(q.object, p);
}
int mm_text_box_clipboard_dispose(MMTextBoxClipboard *clipboard, const MMTextBoxEditServices *p)
{
    TRY(release(clipboard->text, p)); clipboard->text = NULL; return 1;
}
int mm_text_box_clipboard_paste(const MMTextBoxClipboard *clipboard, const MMTextBoxEditServices *p)
{
    MMTextBoxEditRequest q;
    MMTextBoxBounds bounds;
    void *text;
    MMI32 right, bottom;
    int result;
    TRY(CALL(MM_BOX_WORKSPACE)); right = q.value; bottom = q.second;
    q.object = clipboard->text; result = CALL(MM_BOX_DUPLICATE); TRY(result); text = q.object;
    if (!result) { TRY(CALL(MM_BOX_CLEAR_UNDO)); return 0; }
    q.object = text; TRY(CALL(MM_BOX_GET_BOUNDS)); bounds = q.bounds;
    bounds.right = mm_signed_word((MMU32)bounds.right - (MMU32)bounds.left + 177U);
    bounds.bottom = mm_signed_word((MMU32)bounds.bottom - (MMU32)bounds.top + 78U);
    bounds.left = 177; bounds.top = 78;
    if (bounds.right > right) bounds.right = right;
    if (bounds.bottom > bottom) bounds.bottom = bottom;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) memset(&bounds, 0, sizeof(bounds));
    q.bounds = bounds; TRY(CALL(MM_BOX_SET_BOUNDS));
    result = CALL(MM_BOX_ADD); TRY(result);
    if (!result) { TRY(CALL(MM_BOX_CLEAR_UNDO)); TRY(release(text, p)); return 0; }
    TRY(CALL(MM_BOX_SELECT)); TRY(CALL(MM_BOX_ATTACH_MOUSE));
    return release(text, p);
}

#define CLIP(op) s->call(s->context, op, &q)
static int edit_clipboard(int action, int inline_text, const MMTextBoxClipboardServices *s)
{
    MMTextBoxClipboardRequest q;
    void *duplicate;
    int result;

    q.object = NULL;
    q.action = action;
    q.inline_text = inline_text;
    switch (action)
    {
    case MM_BOX_CLIP_CUT:
    case MM_BOX_CLIP_COPY:
        if (inline_text)
        {
            result = CLIP(MM_BOX_CLIP_TEXT_SELECTED);
            if (result <= 0)
                return result < 0 ? result : 1;
            TRY(CLIP(MM_BOX_CLIP_CUE));
            return CLIP(MM_BOX_CLIP_EDIT_TEXT);
        }
        result = CLIP(MM_BOX_CLIP_DUPLICATE);
        if (result <= 0)
            return result;
        duplicate = q.object;
        result = CLIP(MM_BOX_CLIP_CREATE);
        TRY(result);
        if (!result)
        {
            q.object = duplicate;
            TRY(CLIP(MM_BOX_CLIP_RELEASE));
            return 0;
        }
        if (action == MM_BOX_CLIP_CUT)
        {
            result = CLIP(MM_BOX_CLIP_HIDE);
            TRY(result);
            if (!result)
            {
                TRY(CLIP(MM_BOX_CLIP_RELEASE));
                return 0;
            }
        }
        TRY(CLIP(MM_BOX_CLIP_PUBLISH));
        TRY(CLIP(MM_BOX_CLIP_RELEASE));
        return CLIP(MM_BOX_CLIP_CUE);

    case MM_BOX_CLIP_PASTE:
        if (inline_text)
        {
            result = CLIP(MM_BOX_CLIP_HAS_ACTOR);
            TRY(result);
            if (!result)
            {
                result = CLIP(MM_BOX_CLIP_HAS_BOX);
                TRY(result);
            }
            if (!result)
            {
                TRY(CLIP(MM_BOX_CLIP_CUE));
                return CLIP(MM_BOX_CLIP_EDIT_TEXT);
            }
            q.action = MM_BOX_CLIP_UNSUPPORTED;
            return CLIP(MM_BOX_CLIP_DIAGNOSTIC);
        }
        result = CLIP(MM_BOX_CLIP_GET_BOX);
        TRY(result);
        if (!result)
        {
            q.action = MM_BOX_CLIP_NOTHING;
            return CLIP(MM_BOX_CLIP_DIAGNOSTIC);
        }
        result = CLIP(MM_BOX_CLIP_PASTE_BOX);
        TRY(result);
        TRY(CLIP(MM_BOX_CLIP_RELEASE));
        TRY(CLIP(MM_BOX_CLIP_CUE));
        return result;

    default:
        q.action = MM_BOX_CLIP_BAD_TOOL;
        return CLIP(MM_BOX_CLIP_DIAGNOSTIC);
    }
}

int mm_text_box_clipboard_command(int command, const MMTextBoxClipboardServices *s)
{
    MMTextBoxClipboardRequest q;
    int result, inline_text;

    q.object = NULL;
    q.action = command;
    q.inline_text = 0;
    if (command == MM_BOX_CLIP_PASTE || command == MM_BOX_CLIP_PASTE_TOOL)
    {
        result = CLIP(MM_BOX_CLIP_GET_ACTOR);
        TRY(result);
        if (result)
        {
            result = CLIP(MM_BOX_CLIP_ROUTE_ONLY);
            TRY(result);
            if (result)
            {
                q.action = MM_BOX_CLIP_UNSUPPORTED;
                TRY(CLIP(MM_BOX_CLIP_DIAGNOSTIC));
            }
            else
                TRY(CLIP(MM_BOX_CLIP_FORWARD));
            TRY(CLIP(MM_BOX_CLIP_RELEASE));
            return 1;
        }
    }
    switch (command)
    {
    case MM_BOX_CLIP_CUT:
    case MM_BOX_CLIP_COPY:
        inline_text = CLIP(MM_BOX_CLIP_TEXT_SELECTED);
        TRY(inline_text);
        break;
    case MM_BOX_CLIP_PASTE:
        result = CLIP(MM_BOX_CLIP_HAS_BOX);
        TRY(result);
        inline_text = !result;
        break;
    default:
        return 0;
    }
    TRY(edit_clipboard(command, inline_text, s));
    return 1;
}

int mm_text_box_clipboard_enable(int command, const MMTextBoxClipboardServices *s)
{
    MMTextBoxClipboardRequest q = {0};
    int result;

    q.action = command;
    switch (command)
    {
    case MM_BOX_CLIP_CUT_TOOL:
    case MM_BOX_CLIP_COPY_TOOL:
        return MM_BOX_CLIP_ENABLED;
    case MM_BOX_CLIP_PASTE:
    case MM_BOX_CLIP_PASTE_TOOL:
        result = CLIP(MM_BOX_CLIP_BOX_SELECTED);
        TRY(result);
        if (result)
        {
            result = CLIP(MM_BOX_CLIP_EMPTY);
            TRY(result);
            if (!result)
            {
                result = CLIP(MM_BOX_CLIP_CAN_PASTE_TEXT);
                TRY(result);
                if (result)
                    return MM_BOX_CLIP_ENABLED;
            }
        }
        result = CLIP(MM_BOX_CLIP_HAS_BOX);
        TRY(result);
        return result ? MM_BOX_CLIP_ENABLED : MM_BOX_CLIP_DISABLED;
    default:
        return MM_BOX_CLIP_USE_PARENT;
    }
}

int mm_text_box_clipboard_click(int action, int inline_text, const MMTextBoxClipboardServices *s)
{
    MMTextBoxClipboardRequest q = {0};
    int result = edit_clipboard(action, inline_text, s);

    TRY(result);
    if (inline_text && action == MM_BOX_CLIP_COPY)
    {
        q.action = action;
        q.inline_text = inline_text;
        TRY(CLIP(MM_BOX_CLIP_COLLAPSE));
    }
    return result;
}
#undef CLIP
