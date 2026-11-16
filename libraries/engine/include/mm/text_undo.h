/* Text undo counters, pending transactions and rich snapshot records. */
#ifndef MM_ENGINE_TEXT_UNDO_H
#define MM_ENGINE_TEXT_UNDO_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMTextUndoControl
    {
        MMI32 suspended, combine;
        void *pending;
    } MMTextUndoControl;
    typedef struct MMTextUndoRecord
    {
        MMI32 combine, at, inserted;
        void *saved;
    } MMTextUndoRecord;
    enum
    {
        MM_TEXT_UNDO_ACTIVE,
        MM_TEXT_UNDO_WRONG_DOCUMENT
    };
    enum
    {
        MM_TEXT_UNDO_HIDE,
        MM_TEXT_UNDO_SHOW,
        MM_TEXT_UNDO_SELECT_QUIET,
        MM_TEXT_UNDO_SELECT
    };
    typedef struct MMTextUndoTransactionServices
    {
        void *context;
        MMTextUndoControl *control;
        MMI32 (*maximum)(void *), (*done)(void *);
        int (*resume)(void *);
        int (*create)(void *, MMI32 combine, MMI32 first, MMI32 end, MMI32 inserted, void **owned);
        void (*release)(void *, void *);
        void *(*previous)(void *, MMI32 index);
        int (*is_record)(void *, void *), (*combine)(void *, void *previous, void *pending);
        int (*clear_redo)(void *), (*add)(void *, void *);
        void (*check_pending)(void *, void *), (*diagnostic)(void *, int);
    } MMTextUndoTransactionServices;
    typedef struct MMTextUndoRecordServices
    {
        void *context;
        int (*allocate)(void *, void **owned);
        MMTextUndoRecord *(*record)(void *, void *);
        int (*document)(void *, void **owned);
        void (*release)(void *, void *);
        MMI32 (*characters)(void *, void *);
        int (*accept)(void *, void *);
        int (*internal)(void *, void *), (*suspend)(void *, void *), (*resume)(void *, void *);
        int (*copy)(void *, void *destination, void *source, MMI32 from, MMI32 count, MMI32 at, MMI32 deleted);
        int (*remove)(void *, void *, MMI32 at, MMI32 count);
        int (*selection)(void *, void *, int operation, MMI32 first, MMI32 end);
        void (*check_saved)(void *, void *), (*diagnostic)(void *, int);
        void (*check_range)(void *, void *document, MMI32 first);
    } MMTextUndoRecordServices;
    void mm_text_undo_suspend(MMTextUndoControl *);
    void mm_text_undo_resume(MMTextUndoControl *);
    void mm_text_undo_bump(MMTextUndoControl *);
    /* Synchronous services return 1/0 or -1/-2 and never throw across C.
     * Primitive getters, checks, diagnostics and release never throw. Owned
     * factory outputs are valid even on failure; construction releases them.
     * Controls/records are live shared state, not snapshots. Empty/nested/disabled
     * setup still suspends. Commit ignores ordinary stack-add failure, and releases
     * pending only after combine/clear-redo or add returns normally. */
    int mm_text_undo_setup(MMI32 first, MMI32 end, MMI32 inserted, const MMTextUndoTransactionServices *);
    int mm_text_undo_cancel(const MMTextUndoTransactionServices *);
    int mm_text_undo_commit(const MMTextUndoTransactionServices *);
    /* Factory ranges sort before construction. Saved documents become internal
     * with undo suspended before copying. Replay snapshots replaced text, applies
     * the old snapshot, restores selection and only then swaps record ownership.
     * Ordinary snapshot-copy failure retains the native early resume (even before
     * target suspension); ordinary document-factory failure does not resume.
     * Exceptions retain text/counter/record effects without added resume/rollback,
     * but release private construction/replay owners before native rethrow. */
    int mm_text_undo_record_create(void **owned, MMI32 combine, void *document, MMI32 first, MMI32 end, MMI32 inserted,
                                   const MMTextUndoRecordServices *);
    void mm_text_undo_record_dispose(MMTextUndoRecord *, const MMTextUndoRecordServices *);
    int mm_text_undo_record_apply(MMTextUndoRecord *, void *document, const MMTextUndoRecordServices *);
    int mm_text_undo_record_combine(MMTextUndoRecord *, const MMTextUndoRecord *, const MMTextUndoRecordServices *);
#ifdef __cplusplus
}
#endif
#endif
