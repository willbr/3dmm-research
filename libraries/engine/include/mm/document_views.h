/* Borrowed document view storage, activation and title policy. */
#ifndef MM_ENGINE_DOCUMENT_VIEWS_H
#define MM_ENGINE_DOCUMENT_VIEWS_H
#include "collections.h"
#include "document_lifecycle.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMDocumentViews
    {
        MMCollectionStorage storage;
        MMU32 count;
        int ready, trash;
    } MMDocumentViews;
    enum
    {
        MM_DOC_NAME_CLIPBOARD,
        MM_DOC_NAME_FILE,
        MM_DOC_NAME_UNTITLED
    };
    typedef struct MMDocumentViewServices
    {
        void *context;
        int (*status)(void *);
        void (*validate)(void *), (*missing)(void *);
        void *(*get)(void *, MMDocumentNode *, MMI32 index);
        int (*active)(void *, void *view);
        void *(*window)(void *, void *view);
        int (*activate_window)(void *, void *window);
        int (*read_name)(void *, MMDocumentNode *), (*set_title)(void *, void *window);
        int (*clipboard)(void *, MMDocumentNode *), (*current_name)(void *, MMDocumentNode *);
        int (*emit_name)(void *, int kind, MMI32 untitled);
    } MMDocumentViewServices;
    /* Initialize once; do not copy owning storage. Entries are borrowed; duplicates
     * are retained and never gain/release references. Find/remove/front use the
     * first match. Miss leaves the find output unchanged. Services return 1/0 or
     * -1/-2 without throwing across C. Allocator status precedes ordinary failure
     * handling. Primitive borrowed view/window access and diagnostics never throw.
     * Title traversal reads live views after name/title hooks; callbacks keep the
     * current document alive. Exception results preserve partial storage/output/
     * title/counter effects without rollback. */
    int mm_document_views_init(MMDocumentViews *, const MMStorageAllocator *, int trash);
    int mm_document_views_add(MMDocumentViews *, void *view, const MMDocumentViewServices *);
    int mm_document_views_find(const MMDocumentViews *, void *view, MMI32 *index);
    void *mm_document_views_get(const MMDocumentViews *, MMU32 index);
    void mm_document_views_remove(MMDocumentViews *, void *view, const MMDocumentViewServices *);
    int mm_document_views_first(MMDocumentViews *, void *view, const MMDocumentViewServices *);
    int mm_document_views_dispose(MMDocumentViews *, const MMDocumentViewServices *);
    int mm_document_view_active(MMDocumentNode *, void **out, const MMDocumentViewServices *);
    int mm_document_view_activate(MMDocumentNode *, const MMDocumentViewServices *);
    int mm_document_titles(MMDocumentNode *, const MMDocumentViewServices *);
    MMI32 mm_document_untitled(MMDocumentRegistry *, MMDocumentNode *);
    int mm_document_name(MMDocumentRegistry *, MMDocumentNode *, const MMDocumentViewServices *);

    enum
    {
        MM_DOC_COMMAND_UNKNOWN,
        MM_DOC_COMMAND_SAVE,
        MM_DOC_COMMAND_CUT,
        MM_DOC_COMMAND_COPY,
        MM_DOC_COMMAND_CLEAR,
        MM_DOC_COMMAND_PASTE,
        MM_DOC_COMMAND_PASTE_SPECIAL,
        MM_DOC_COMMAND_UNDO,
        MM_DOC_COMMAND_REDO
    };
    typedef struct MMDocumentEditServices
    {
        void *context;
        int (*dirty)(void *), (*has_history)(void *, int redo);
        int (*copy)(void *, void **owned);
        int (*clipboard_empty)(void *), (*clipboard_is_self)(void *);
        int (*publish)(void *, void *), (*release)(void *, void *);
        int (*clear)(void *), (*paste)(void *, int apply, int command);
        void (*unknown)(void *);
    } MMDocumentEditServices;
    /* Copy with no output probes the selection. Successful copy with an
     * output transfers one reference; publish retains it. Callbacks return
     * 1/0 or a negative error. Errors preserve the completed prefix. */
    int mm_document_edit_enabled(int command, const MMDocumentEditServices *);
    int mm_document_edit_clipboard(int command, const MMDocumentEditServices *);
#ifdef __cplusplus
}
#endif
#endif
