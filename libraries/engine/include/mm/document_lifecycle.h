/* Document registry, tree traversal and lifecycle transactions. */
#ifndef MM_ENGINE_DOCUMENT_LIFECYCLE_H
#define MM_ENGINE_DOCUMENT_LIFECYCLE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMDocumentNode
    {
        struct MMDocumentNode *parent, *sibling, *child;
        void *owner;
        int freeing, internal;
        MMI32 untitled;
    } MMDocumentNode;
    typedef struct MMDocumentRegistry
    {
        MMDocumentNode *first;
        MMI32 untitled;
    } MMDocumentRegistry;
    typedef struct MMDocumentTree
    {
        MMDocumentNode *root, *current;
        int state;
    } MMDocumentTree;
    enum
    {
        MM_DOC_TREE_SKIP = 1,
        MM_DOC_TREE_PRE = 2,
        MM_DOC_TREE_POST = 4,
        MM_DOC_TREE_ROOT = 8
    };
    enum
    {
        MM_DOC_FORCE = 2,
        MM_DOC_ASSUME_YES = 4
    };
    enum
    {
        MM_DOC_SAVE,
        MM_DOC_SAVE_AS,
        MM_DOC_SAVE_COPY
    };
    enum
    {
        MM_DOC_OPEN_VIEWS,
        MM_DOC_RECURSIVE_RELEASE,
        MM_DOC_CHILD_RETAINED,
        MM_DOC_CLIPBOARD_RELEASE,
        MM_DOC_CORRUPT_TREE,
        MM_DOC_NO_VIEWS,
        MM_DOC_BAD_SAVE
    };
    typedef struct MMDocumentLifecycleServices
    {
        void *context;
        MMI32 (*drop_reference)(void *, MMDocumentNode *);
        int (*retain)(void *, MMDocumentNode *), (*release)(void *, MMDocumentNode *);
        int (*destroy)(void *, MMDocumentNode *), (*dispose_undo)(void *, MMDocumentNode *);
        int (*views_present)(void *, MMDocumentNode *);
        MMI32 (*view_count)(void *, MMDocumentNode *);
        void *(*view)(void *, MMDocumentNode *, MMI32 index), *(*window)(void *, void *view);
        int (*release_view)(void *, void *);
        int (*close_views)(void *, MMDocumentNode *), (*dispose_views)(void *, MMDocumentNode *);
        int (*clipboard)(void *, MMDocumentNode *), (*clear_clipboard)(void *);
        int (*dirty)(void *, MMDocumentNode *), (*query_save)(void *, MMDocumentNode *, int force);
        int (*save)(void *, MMDocumentNode *), (*query_close)(void *, MMDocumentNode *, MMU32 flags);
        int (*match_name)(void *, MMDocumentNode *);
        int (*current_name)(void *, MMDocumentNode *), (*choose_name)(void *, MMDocumentNode *);
        int (*write_name)(void *, MMDocumentNode *, int adopt), (*update_name)(void *, MMDocumentNode *);
        void (*save_error)(void *), (*diagnostic)(void *, int);
    } MMDocumentLifecycleServices;
    /* Live borrowed nodes in an acyclic native document tree. Registration uses a
     * fresh node. Unlink retains its links. Iteration leaves node output unchanged
     * on miss and always publishes flags; skip applies after a pre visit. Services
     * are synchronous, 1/0 or -1/-2, and never throw across C. query_save uses native
     * no/yes/cancel values 0/1/2. Primitive refs/view access and diagnostics never
     * throw. Callback traversal keeps current nodes alive; re-linking is observed.
     * Destroy/release may free a node; C does not touch it afterward. Failures keep
     * native partial counters/owners/effects without added rollback. */
    void mm_document_register(MMDocumentRegistry *, MMDocumentNode *, void *owner, MMDocumentNode *anchor, int sibling);
    int mm_document_unlink(MMDocumentRegistry *, MMDocumentNode *);
    void mm_document_tree_init(MMDocumentTree *, MMDocumentNode *root);
    int mm_document_tree_next(MMDocumentTree *, MMDocumentNode **out, MMU32 *flags, MMU32 input);
    int mm_document_internal(MMDocumentNode *, const MMDocumentLifecycleServices *);
    int mm_document_release(MMDocumentNode *, const MMDocumentLifecycleServices *);
    int mm_document_finish(MMDocumentRegistry *, MMDocumentNode *, const MMDocumentLifecycleServices *);
    int mm_document_close_views(MMDocumentNode *, const MMDocumentLifecycleServices *);
    int mm_document_query_close(MMDocumentNode *, MMU32 flags, const MMDocumentLifecycleServices *);
    int mm_document_query_all(const MMDocumentRegistry *, MMU32 flags, const MMDocumentLifecycleServices *);
    int mm_document_query_window(MMDocumentNode *, void *window, const MMDocumentLifecycleServices *);
    int mm_document_find(const MMDocumentRegistry *, MMDocumentNode **out, const MMDocumentLifecycleServices *);
    int mm_document_save(MMDocumentNode *, int kind, const MMDocumentLifecycleServices *);
#ifdef __cplusplus
}
#endif
#endif
