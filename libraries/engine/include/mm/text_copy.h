/* Rich-document/property copying and embedded-object edit policy. */
#ifndef MM_ENGINE_TEXT_COPY_H
#define MM_ENGINE_TEXT_COPY_H
#include "text_replace.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_COPY_GROW,
        MM_TEXT_COPY_ARGUMENT
    };
    enum
    {
        MM_TEXT_OBJECT_NOT_AT,
        MM_TEXT_OBJECT_RESERVED_INSERT
    };
    typedef struct MMTextCopyServices
    {
        void *context;
        MMTextReplaceRichServices target;
        MMTextPropertyServices source;
        MMI32 (*source_characters)(void *);
        MMGroup (*source_arguments)(void *);
        void *(*source_stream)(void *);
        int (*check_tail)(void *, MMI32 cp, MMI32 inserted, MMI32 deleted);
        void (*check_properties)(void *, MMI32 from, MMI32 at, MMI32 source_span, MMI32 target_span);
        void (*warning)(void *, int);
    } MMTextCopyServices;
    typedef struct MMTextObjectServices
    {
        void *context;
        unsigned unit;
        MMTextReplaceRichServices target;
        MMGroup (*arguments)(void *);
        int (*allocate)(void *, MMI32 bytes, void **owned);
        void (*free)(void *, void *owned);
        void (*publish_at)(void *, MMI32), (*publish_bytes)(void *, MMI32);
        int (*dispatch)(void *, int draw, MMI32 index, unsigned kind);
        void (*diagnostic)(void *, int);
    } MMTextObjectServices;
    /* Valid native ranges, distinct source/destination documents. Copy clears while
     * traversing the live destination map, retaining its index-shift/skip behavior.
     * It clips source ranges and extends the last source value to the destination
     * end. Defaults are not rebased. Ordinary reserve/import failures warn/continue.
     * Property import reads size before locking; object import locks before size.
     * Locks always release after destination interning, including exception results.
     * Other partial edits/references retain native non-atomic behavior. */
    int mm_text_copy_properties(MMI32 from, MMI32 at, MMI32 source_span, MMI32 target_span, unsigned first_kind,
                                unsigned end_kind, const MMTextCopyServices *);
    int mm_text_copy_document(MMI32 from, MMI32 count, MMI32 at, MMI32 deleted, MMU32 flags,
                              const MMTextCopyServices *);
    /* Object queries concern built-in kind 192. Miss publishes the next/end position
     * but leaves size/data outputs alone. Allocation failure still reports found;
     * the allocation service publishes its output (normally null) directly to the
     * caller. Any owner returned on an exception remains caller-owned. If the row
     * disappears or shrinks during successful allocation, discard the new
     * buffer and report the already found position with null data. */
    int mm_text_object_fetch(MMI32 cp, MMI32 *at, void **owned, MMI32 *bytes, const MMTextObjectServices *);
    int mm_text_object_dispatch(MMI32 cp, int draw, const MMTextObjectServices *);
    /* Insert retains legacy staged-reference behavior: reservation failure releases
     * the object reference but not a successfully generated character batch; failed
     * virtual replacement releases that batch before the object/undo cancellation.
     * A reference never acquired is represented by zero, avoiding native undefined
     * uninitialized-output reads. Successful insertion's ordinary reserved-insert
     * failure diagnoses/continues. Exception results preserve other partial effects.
     * Services are synchronous, 1/0 or -1/-2, with no exceptions crossing C; primitive
     * collection access, locks, publication, checks and diagnostics never throw. */
    int mm_text_object_insert(const void *, MMI32 bytes, MMI32 cp, MMI32 deleted, MMU32 flags,
                              const MMTextObjectServices *);
    int mm_text_object_apply(const void *, MMI32 bytes, MMI32 cp, MMU32 flags, const MMTextObjectServices *);
    enum
    {
        MM_TEXT_CLIPBOARD_PLAIN,
        MM_TEXT_CLIPBOARD_RICH,
        MM_TEXT_CLIPBOARD_STREAM
    };
    typedef enum MMTextClipboardOperation
    {
        MM_TEXT_CLIPBOARD_SELECTION,
        MM_TEXT_CLIPBOARD_ALLOCATE,
        MM_TEXT_CLIPBOARD_SUSPEND,
        MM_TEXT_CLIPBOARD_RESUME,
        MM_TEXT_CLIPBOARD_COPY,
        MM_TEXT_CLIPBOARD_PUBLISH,
        MM_TEXT_CLIPBOARD_RELEASE,
        MM_TEXT_CLIPBOARD_CLEAR,
        MM_TEXT_CLIPBOARD_HAS_FORMAT,
        MM_TEXT_CLIPBOARD_ACQUIRE,
        MM_TEXT_CLIPBOARD_SIZE,
        MM_TEXT_CLIPBOARD_HIDE,
        MM_TEXT_CLIPBOARD_SHOW,
        MM_TEXT_CLIPBOARD_BUMP_UNDO,
        MM_TEXT_CLIPBOARD_IS_RICH,
        MM_TEXT_CLIPBOARD_PLAIN_STYLE,
        MM_TEXT_CLIPBOARD_PASTE_PLAIN,
        MM_TEXT_CLIPBOARD_PASTE_RICH,
        MM_TEXT_CLIPBOARD_INVALIDATE,
        MM_TEXT_CLIPBOARD_CARET
    } MMTextClipboardOperation;
    typedef struct MMTextClipboardRequest
    {
        void *document;
        MMI32 anchor, focus, first, inserted, deleted;
        int format;
    } MMTextClipboardRequest;
    typedef struct MMTextClipboardServices
    {
        void *context;
        int kind;
        int (*call)(void *, MMTextClipboardOperation, MMTextClipboardRequest *);
    } MMTextClipboardServices;

    /* ALLOCATE and ACQUIRE return one owned document on success. PUBLISH
     * transfers the copy or writes null on ordinary creation failure.
     * SELECTION fills anchor/focus. SIZE fills inserted with the entire
     * source length, including the document's terminal character in text
     * views. HAS_FORMAT and ACQUIRE receive format. Copy and paste use first
     * and inserted/deleted counts; CLEAR uses anchor/focus and CARET uses
     * first. Results are 1/0 or negative errors with native partial effects. */
    int mm_text_clipboard_copy(int create, const MMTextClipboardServices *);
    int mm_text_clipboard_clear(const MMTextClipboardServices *);
    int mm_text_clipboard_paste(int standard, int apply, const MMTextClipboardServices *);
#ifdef __cplusplus
}
#endif
#endif
