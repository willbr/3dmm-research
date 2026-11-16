/* Generic document undo stack with C-owned collection storage. */
#ifndef MM_ENGINE_DOCUMENT_UNDO_H
#define MM_ENGINE_DOCUMENT_UNDO_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMDocumentUndo
    {
        MMCollectionStorage storage;
        MMU32 count;
        MMI32 done, maximum;
        int ready, trash;
    } MMDocumentUndo;
    typedef struct MMDocumentUndoServices
    {
        void *context;
        int (*replay)(void *, void *record, int redo);
        int (*retain)(void *, void *), (*release)(void *, void *);
        int (*clear_redo)(void *);
        int (*status)(void *);
        void (*validate)(void *), (*diagnostic)(void *);
    } MMDocumentUndoServices;
    /* Initialize once; do not copy this owning state. Valid, unlocked storage and
     * native-range limits/indices are required. Allocator callbacks never throw.
     * Services return 1/0 or -1/-2; ownership primitives return 1 or an exception.
     * status reports a captured allocator exception before ordinary OOM handling.
     * Get/validate/diagnostic never throw. Dispatch/ownership hooks may re-enter;
     * counts/limits are read live, including done +/- 1 after successful replay.
     * Ordinary append failure evicts oldest records and retries. Exceptions keep
     * partial storage/reference effects and never start ordinary eviction retry. */
    int mm_document_undo_init(MMDocumentUndo *, const MMStorageAllocator *, int trash);
    int mm_document_undo_prepare(MMDocumentUndo *, const MMDocumentUndoServices *);
    void *mm_document_undo_get(const MMDocumentUndo *, MMU32 index);
    MMI32 mm_document_undo_redo_count(const MMDocumentUndo *);
    int mm_document_undo_replay(MMDocumentUndo *, int redo, const MMDocumentUndoServices *);
    int mm_document_undo_add(MMDocumentUndo *, void *borrowed_record, const MMDocumentUndoServices *);
    int mm_document_undo_clear(MMDocumentUndo *, const MMDocumentUndoServices *);
    int mm_document_undo_clear_redo(MMDocumentUndo *, const MMDocumentUndoServices *);
    int mm_document_undo_limit(MMDocumentUndo *, MMI32 maximum, const MMDocumentUndoServices *);
    int mm_document_undo_dispose(MMDocumentUndo *, const MMDocumentUndoServices *);
#ifdef __cplusplus
}
#endif
#endif
