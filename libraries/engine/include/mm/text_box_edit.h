/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_BOX_EDIT_H
#define MM_ENGINE_TEXT_BOX_EDIT_H
#include "text_box_runtime.h"
#include "text_format.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_BOX_UNDO_TYPE, MM_BOX_UNDO_SIZE, MM_BOX_UNDO_HIDE, MM_BOX_UNDO_EDIT, MM_BOX_UNDO_COLOR };
enum { MM_BOX_TEXT_COLOR, MM_BOX_TEXT_HEIGHT, MM_BOX_TEXT_STYLE, MM_BOX_TEXT_FONT };
enum { MM_BOX_BAD_FILE, MM_BOX_UNDO_LIMIT, MM_BOX_BAD_TEXT_VIEW };
typedef struct MMTextBoxUndo
{
    int kind, story;
    MMI32 index, first, end;
    MMU32 color;
    MMTextBoxBounds bounds;
    void *edit; /* retained wrapped document undo record */
} MMTextBoxUndo;
typedef struct MMTextBoxClipboard { void *text; } MMTextBoxClipboard;
typedef enum MMTextBoxEditOperation
{
    MM_BOX_ALLOCATE, MM_BOX_INITIALIZE, MM_BOX_RELEASE, MM_BOX_RETAIN,
    MM_BOX_INPUT_BOUNDS, MM_BOX_DEFAULTS, MM_BOX_SET_DEFAULTS, MM_BOX_SET_WIDTH,
    MM_BOX_BACKGROUND, MM_BOX_SET_BACKGROUND, MM_BOX_UNDO_MAXIMUM, MM_BOX_SET_UNDO_LIMIT,
    MM_BOX_PUBLISH, MM_BOX_READ_TEXT, MM_BOX_DIAGNOSTIC,
    MM_BOX_NEW_COPY, MM_BOX_SUSPEND_UNDO, MM_BOX_RESUME_UNDO, MM_BOX_CHARACTERS, MM_BOX_REPLACE,
    MM_BOX_SET_TYPE, MM_BOX_ALLOCATE_UNDO, MM_BOX_ADD_UNDO, MM_BOX_PARENT_ADD_UNDO,
    MM_BOX_CLEAR_UNDO, MM_BOX_PARENT_CLEAR_UNDO, MM_BOX_SHOW, MM_BOX_HIDE, MM_BOX_REMOVE,
    MM_BOX_SCENE_BOX, MM_BOX_VIEW, MM_BOX_CHECK_TEXT_VIEW, MM_BOX_APPLY_CHARACTER,
    MM_BOX_ACTIVE_VIEW, MM_BOX_SELECTION, MM_BOX_FETCH_CHARACTER, MM_BOX_PUBLISH_CHARACTER,
    MM_BOX_PUBLISH_MASK, MM_BOX_ALLOCATE_CLIPBOARD, MM_BOX_WORKSPACE, MM_BOX_DUPLICATE,
    MM_BOX_SET_BOUNDS, MM_BOX_ADD, MM_BOX_SELECT, MM_BOX_ATTACH_MOUSE,
    MM_BOX_UNDO_SCENE, MM_BOX_UNDO_FRAME, MM_BOX_CURRENT_SCENE, MM_BOX_SWITCH_SCENE,
    MM_BOX_SEEK_SCENE, MM_BOX_STORY, MM_BOX_GET_BOUNDS, MM_BOX_POSITION_VIEW,
    MM_BOX_GOTO, MM_BOX_FLUSH, MM_BOX_REPLAY_EDIT
} MMTextBoxEditOperation;
typedef struct MMTextBoxEditRequest
{
    void *object, *other;
    MMI32 value, second;
    MMU32 word, flags;
    MMTextBoxBounds bounds;
    MMTextChunk chunk;
    MMTextCharacter character, difference;
} MMTextBoxEditRequest;
typedef struct MMTextBoxEditServices
{
    void *context;
    /* Synchronous native primitives: 1/0 for success/ordinary false, negative
     * for errors. Never unwind across C. Queries fill their named request
     * fields and leave others unchanged. Allocation returns object or null;
     * PUBLISH writes the caller's output immediately (including null).
     * Metadata/runtime/undo/clipboard views below are borrowed plain C storage;
     * view access itself never throws. Ordinary failure cleanup is operation
     * specific. Exceptions retain the original completed prefix, with no newly
     * introduced rollback or owner release.
     * Request field conventions:
     * - document/view/undo operations receive object; ADD_UNDO receives the
     *   record in object and text in other; REPLACE receives destination/source
     *   in object/other and source/destination counts in value/second;
     * - INPUT_BOUNDS returns presence in value and bounds; DEFAULTS and
     *   SET_DEFAULTS use value/second for font/height; BACKGROUND uses word;
     *   SET_BACKGROUND also receives flags; SET_WIDTH/UNDO_LIMIT use value;
     * - READ_TEXT receives the resolved rich-text chunk; NEW_COPY receives bounds;
     * - SCENE_BOX receives index in value, text in other for document calls;
     *   VIEW/ACTIVE_VIEW return object; CHECK_TEXT_VIEW receives value=1 for
     *   a text-box view, 0 for a generic text view, then returns a boolean;
     * - SELECTION returns value/second; FETCH_CHARACTER receives cp in value
     *   and returns character/end in character/second; APPLY_CHARACTER receives
     *   character/difference; publication uses character or word (mask);
     * - WORKSPACE returns width/height in value/second; ALLOCATE_UNDO receives
     *   kind in value; UNDO_FRAME/UNDO_SCENE/CURRENT_SCENE return value;
     *   SWITCH_SCENE/SEEK_SCENE/GOTO receive value; REPLAY_EDIT receives record,
     *   document and undo boolean in object/other/value. */
    int (*call)(void *, MMTextBoxEditOperation, MMTextBoxEditRequest *);
    MMTextBoxState *(*state)(void *, void *);
    MMTextBoxRuntime *(*runtime)(void *, void *);
    MMTextBoxUndo *(*undo)(void *, void *);
    MMTextBoxClipboard *(*clipboard)(void *, void *);
    MMU32 black, clear, update;
} MMTextBoxEditServices;
/* Factory bounds are read through INPUT_BOUNDS after initialization. All
 * constructors publish null first, then the completed owner. Duplicate instead
 * publishes the new document before suspension/replacement and nulls on failure. */
int mm_text_box_create(void *scene, int story, const MMTextBoxEditServices *);
/* Read the archive header and resolve its child before allocating the document. */
int mm_text_box_read_document(void *scene, MMU32 number, const struct MMMovieArchiveEditor *,
                              const MMTextBoxEditServices *);
int mm_text_box_duplicate(void *source, const MMTextBoxEditServices *);
int mm_text_box_index(void *text, MMI32 *index, const MMTextBoxEditServices *);
/* Change kind is UNDO_TYPE or UNDO_COLOR. Register the matching record before
 * applying; selection kinds below are TEXT_COLOR/HEIGHT/STYLE/FONT.
 * A hide at/before first delegates to scene removal without a hide record.
 * show clears movie undo only on success, retaining the original behavior. */
int mm_text_box_change(void *text, int kind, MMU32 value, const MMTextBoxEditServices *);
int mm_text_box_show_edit(void *text, const MMTextBoxEditServices *);
int mm_text_box_hide_edit(void *text, const MMTextBoxEditServices *);
int mm_text_box_add_edit_undo(void *text, void *record, const MMTextBoxEditServices *);
int mm_text_box_clear_edit_undo(void *text, const MMTextBoxEditServices *);
int mm_text_box_apply_selection(void *text, int kind, MMU32 value, const MMTextBoxEditServices *);
int mm_text_box_fetch_selection(void *text, const MMTextBoxEditServices *);
int mm_text_box_wrap_undo(void *record, const MMTextBoxEditServices *);
int mm_text_box_clipboard_create(void *adopted_text, const MMTextBoxEditServices *);
int mm_text_box_clipboard_dispose(MMTextBoxClipboard *, const MMTextBoxEditServices *);
int mm_text_box_clipboard_paste(const MMTextBoxClipboard *, const MMTextBoxEditServices *);

enum
{
    MM_BOX_CLIP_UNKNOWN,
    MM_BOX_CLIP_CUT,
    MM_BOX_CLIP_COPY,
    MM_BOX_CLIP_PASTE,
    MM_BOX_CLIP_CUT_TOOL,
    MM_BOX_CLIP_COPY_TOOL,
    MM_BOX_CLIP_PASTE_TOOL
};
enum { MM_BOX_CLIP_USE_PARENT, MM_BOX_CLIP_ENABLED, MM_BOX_CLIP_DISABLED };
enum { MM_BOX_CLIP_UNSUPPORTED, MM_BOX_CLIP_NOTHING, MM_BOX_CLIP_BAD_TOOL };
typedef enum MMTextBoxClipboardOperation
{
    MM_BOX_CLIP_BOX_SELECTED,
    MM_BOX_CLIP_TEXT_SELECTED,
    MM_BOX_CLIP_EMPTY,
    MM_BOX_CLIP_CAN_PASTE_TEXT,
    MM_BOX_CLIP_HAS_ACTOR,
    MM_BOX_CLIP_HAS_BOX,
    MM_BOX_CLIP_GET_ACTOR,
    MM_BOX_CLIP_GET_BOX,
    MM_BOX_CLIP_ROUTE_ONLY,
    MM_BOX_CLIP_RELEASE,
    MM_BOX_CLIP_DUPLICATE,
    MM_BOX_CLIP_CREATE,
    MM_BOX_CLIP_HIDE,
    MM_BOX_CLIP_PUBLISH,
    MM_BOX_CLIP_PASTE_BOX,
    MM_BOX_CLIP_EDIT_TEXT,
    MM_BOX_CLIP_COLLAPSE,
    MM_BOX_CLIP_CUE,
    MM_BOX_CLIP_FORWARD,
    MM_BOX_CLIP_DIAGNOSTIC
} MMTextBoxClipboardOperation;
typedef struct MMTextBoxClipboardRequest
{
    void *object;
    int action, inline_text;
} MMTextBoxClipboardRequest;
typedef struct MMTextBoxClipboardServices
{
    void *context;
    int (*call)(void *, MMTextBoxClipboardOperation, MMTextBoxClipboardRequest *);
} MMTextBoxClipboardServices;

/* Synchronous callbacks return 1/0 or a negative error. Successful GET and
 * DUPLICATE return an owned object. CREATE receives the duplicate, adopts it
 * on success, and returns the owned clipboard document. On ordinary failure
 * the duplicate remains caller-owned. PUBLISH retains its document. EDIT_TEXT
 * and CUE receive the logical action; DIAGNOSTIC receives its code in action.
 * Negative results preserve the completed native prefix. */
int mm_text_box_clipboard_command(int command, const MMTextBoxClipboardServices *);
int mm_text_box_clipboard_enable(int command, const MMTextBoxClipboardServices *);
int mm_text_box_clipboard_click(int action, int inline_text, const MMTextBoxClipboardServices *);
#ifdef __cplusplus
}
#endif
#endif
