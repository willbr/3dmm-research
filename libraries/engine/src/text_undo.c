#include "mm/text_undo.h"
#include "mm/wide.h"
void mm_text_undo_suspend(MMTextUndoControl *s)
{
    s->suspended = mm_signed_word((MMU32)s->suspended + 1);
}
void mm_text_undo_resume(MMTextUndoControl *s)
{
    s->suspended = mm_signed_word((MMU32)s->suspended - 1);
}
void mm_text_undo_bump(MMTextUndoControl *s)
{
    if (s->suspended <= 0)
        s->combine = mm_signed_word((MMU32)s->combine + 1);
}
static void release_pending(const MMTextUndoTransactionServices *s)
{
    if (s->control->pending)
    {
        s->release(s->context, s->control->pending);
        s->control->pending = 0;
    }
}
int mm_text_undo_setup(MMI32 first, MMI32 end, MMI32 inserted, const MMTextUndoTransactionServices *s)
{
    void *owned = 0;
    MMI32 before = s->control->suspended;
    int r, t;
    mm_text_undo_suspend(s->control);
    if (before > 0 || s->maximum(s->context) == 0)
        return 1;
    if (s->control->pending)
        s->diagnostic(s->context, MM_TEXT_UNDO_ACTIVE);
    if (first == end && inserted == 0)
        return 1;
    r = s->create(s->context, s->control->combine, first, end, inserted, &owned);
    if (r <= 0)
    {
        if (owned)
            s->release(s->context, owned);
        if (r < 0)
            return r;
        s->control->pending = 0;
        t = s->resume(s->context);
        return t < 0 ? t : 0;
    }
    s->control->pending = owned;
    return 1;
}
int mm_text_undo_cancel(const MMTextUndoTransactionServices *s)
{
    int r = s->resume(s->context);
    if (r <= 0)
        return r;
    if (s->control->suspended == 0)
        release_pending(s);
    return 1;
}
int mm_text_undo_commit(const MMTextUndoTransactionServices *s)
{
    void *previous;
    MMI32 done;
    int r = s->resume(s->context), combined = 0;
    if (r <= 0)
        return r;
    if (s->control->suspended != 0 || !s->control->pending)
        return 1;
    if (s->check_pending)
        s->check_pending(s->context, s->control->pending);
    done = s->done(s->context);
    if (done > 0)
    {
        previous = s->previous(s->context, done - 1);
        r = s->is_record(s->context, previous);
        if (r < 0)
            return r;
        if (r)
        {
            r = s->combine(s->context, previous, s->control->pending);
            if (r < 0)
                return r;
            combined = r;
        }
    }
    r = combined ? s->clear_redo(s->context) : s->add(s->context, s->control->pending);
    if (r < 0)
        return r;
    release_pending(s);
    return 1;
}
static int prepare(void *document, const MMTextUndoRecordServices *s)
{
    int r = s->internal(s->context, document);
    return r > 0 ? s->suspend(s->context, document) : r;
}
int mm_text_undo_record_create(void **out, MMI32 combine, void *document, MMI32 first, MMI32 end, MMI32 inserted,
                               const MMTextUndoRecordServices *s)
{
    void *owned = 0, *saved = 0;
    MMTextUndoRecord *record;
    MMI32 swap;
    int r;
    *out = 0;
    if (first > end)
    {
        swap = first;
        first = end;
        end = swap;
    }
    if (s->check_range)
        s->check_range(s->context, document, first);
    r = s->allocate(s->context, &owned);
    if (r <= 0)
        goto fail;
    record = s->record(s->context, owned);
    record->combine = combine;
    record->at = first;
    record->inserted = inserted;
    record->saved = 0;
    if (first < end)
    {
        r = s->document(s->context, &saved);
        record->saved = saved;
        if (r <= 0)
            goto fail;
        r = prepare(saved, s);
        if (r <= 0)
            goto fail;
        r = s->copy(s->context, saved, document, first, end - first, 0, 0);
        if (r <= 0)
            goto fail;
    }
    *out = owned;
    return 1;
fail:
    if (owned)
        s->release(s->context, owned);
    return r;
}
void mm_text_undo_record_dispose(MMTextUndoRecord *record, const MMTextUndoRecordServices *s)
{
    if (record->saved)
    {
        s->release(s->context, record->saved);
        record->saved = 0;
    }
}
int mm_text_undo_record_apply(MMTextUndoRecord *record, void *document, const MMTextUndoRecordServices *s)
{
    void *saved = 0;
    MMI32 inserted;
    int r, t;
    r = s->accept(s->context, document);
    if (r < 0)
        return r;
    if (!r || record->at < 0 || record->at >= s->characters(s->context, document) || record->inserted < 0 ||
        record->inserted >= s->characters(s->context, document) + 1 - record->at)
    {
        s->diagnostic(s->context, MM_TEXT_UNDO_WRONG_DOCUMENT);
        return 0;
    }
    if (record->inserted > 0)
    {
        r = s->document(s->context, &saved);
        if (r <= 0)
            goto exit;
        r = prepare(saved, s);
        if (r <= 0)
            goto exit;
        r = s->copy(s->context, saved, document, record->at, record->inserted, 0, 0);
        if (r < 0)
            goto exit;
        if (!r)
            goto fail;
    }
    r = s->suspend(s->context, document);
    if (r <= 0)
        goto exit;
    r = s->selection(s->context, document, MM_TEXT_UNDO_HIDE, 0, 0);
    if (r <= 0)
        goto exit;
    inserted = 0;
    if (!record->saved)
        r = s->remove(s->context, document, record->at, record->inserted);
    else
    {
        inserted = s->characters(s->context, record->saved) - 1;
        r = s->copy(s->context, document, record->saved, 0, inserted, record->at, record->inserted);
    }
    if (r < 0)
        goto exit;
    if (!r)
        goto fail;
    r = s->resume(s->context, document);
    if (r <= 0)
        goto exit;
    r = s->selection(s->context, document, MM_TEXT_UNDO_SELECT_QUIET, record->at, record->at + inserted);
    if (r <= 0)
        goto exit;
    r = s->selection(s->context, document, MM_TEXT_UNDO_SHOW, 0, 0);
    if (r <= 0)
        goto exit;
    r = s->selection(s->context, document, MM_TEXT_UNDO_SELECT, record->at + inserted, record->at + inserted);
    if (r <= 0)
        goto exit;
    mm_text_undo_record_dispose(record, s);
    record->saved = saved;
    record->inserted = inserted;
    return 1;
fail:
    if (saved)
    {
        s->release(s->context, saved);
        saved = 0;
    }
    t = s->resume(s->context, document);
    if (t < 0)
        r = t;
exit:
    if (saved)
        s->release(s->context, saved);
    return r;
}
int mm_text_undo_record_combine(MMTextUndoRecord *record, const MMTextUndoRecord *next,
                                const MMTextUndoRecordServices *s)
{
    MMI32 count;
    int r;
    if (next->combine != record->combine)
        return 0;
    if (!next->saved && record->at + record->inserted == next->at)
    {
        record->inserted += next->inserted;
        return 1;
    }
    if (next->inserted != 0 || record->inserted != 0)
        return 0;
    if (s->check_saved)
    {
        s->check_saved(s->context, record->saved);
        s->check_saved(s->context, next->saved);
    }
    count = s->characters(s->context, next->saved) - 1;
    if (next->at == record->at)
        return s->copy(s->context, record->saved, next->saved, 0, count, s->characters(s->context, record->saved) - 1,
                       0);
    if (next->at + count == record->at)
    {
        r = s->copy(s->context, record->saved, next->saved, 0, count, 0, 0);
        if (r <= 0)
            return r;
        record->at = next->at;
        return 1;
    }
    return 0;
}
