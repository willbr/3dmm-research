#include "mm/document_undo.h"
#include <string.h>
static MMArray array(MMDocumentUndo *u)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &u->storage;
    a.width = sizeof(void *);
    a.count = u->count;
    a.free_count = -1;
    a.trash_unused = u->trash;
    return a;
}
static int result(int storage, const MMDocumentUndoServices *s)
{
    int r = s->status ? s->status(s->context) : 1;
    return r < 0 ? r : storage == MM_STORAGE_OK;
}
int mm_document_undo_init(MMDocumentUndo *u, const MMStorageAllocator *allocator, int trash)
{
    memset(u, 0, sizeof(*u));
    u->maximum = 10;
    u->trash = trash;
    if (mm_storage_init(&u->storage, allocator) != MM_STORAGE_OK)
        return 0;
    u->storage.min_grow[0] = 128;
    u->storage.min_grow[1] = 16;
    return 1;
}
int mm_document_undo_prepare(MMDocumentUndo *u, const MMDocumentUndoServices *s)
{
    MMArray a;
    int r;
    if (u->ready)
        return 1;
    a = array(u);
    r = result(mm_array_reserve(&a, 1, 0), s);
    if (r > 0)
        u->ready = 1;
    return r;
}
void *mm_document_undo_get(const MMDocumentUndo *u, MMU32 index)
{
    void *record;
    if (!u->ready || index >= u->count)
        return 0;
    memcpy(&record, u->storage.buffers[0].data + index * sizeof(void *), sizeof(record));
    return record;
}
MMI32 mm_document_undo_redo_count(const MMDocumentUndo *u)
{
    return u->ready ? (MMI32)u->count - u->done : 0;
}
int mm_document_undo_replay(MMDocumentUndo *u, int redo, const MMDocumentUndoServices *s)
{
    void *record;
    int r;
    if (!u->ready || (redo ? u->done >= (MMI32)u->count : u->done <= 0))
        return 0;
    record = mm_document_undo_get(u, (MMU32)(u->done - !redo));
    r = s->replay(s->context, record, redo);
    if (r <= 0)
        return r;
    u->done += redo ? 1 : -1;
    return 1;
}
static void *remove_at(MMDocumentUndo *u, MMU32 index)
{
    MMArray a = array(u);
    void *record = mm_document_undo_get(u, index);
    mm_array_delete(&a, index, 1);
    u->count = a.count;
    return record;
}
static int append(MMDocumentUndo *u, void *record, const MMDocumentUndoServices *s)
{
    MMArray a = array(u);
    int r = result(mm_array_insert(&a, a.count, &record, 1), s);
    u->count = a.count;
    return r;
}
int mm_document_undo_add(MMDocumentUndo *u, void *record, const MMDocumentUndoServices *s)
{
    void *old;
    int r;
    if (u->maximum == 0)
        return 1;
    r = mm_document_undo_prepare(u, s);
    if (r <= 0)
        return r;
    r = s->clear_redo(s->context);
    if (r <= 0)
        return r;
    if (u->maximum <= (MMI32)u->count)
    {
        old = remove_at(u, 0);
        r = s->release(s->context, old);
        if (r <= 0)
            return r;
    }
    for (;;)
    {
        r = append(u, record, s);
        if (r < 0)
            return r;
        if (r || !u->count)
            break;
        old = remove_at(u, 0);
        r = s->release(s->context, old);
        if (r <= 0)
            return r;
    }
    if (r)
    {
        r = s->retain(s->context, record);
        if (r <= 0)
            return r;
        r = 1;
    }
    u->done = (MMI32)u->count;
    if (s->validate)
        s->validate(s->context);
    return r;
}
int mm_document_undo_clear(MMDocumentUndo *u, const MMDocumentUndoServices *s)
{
    void *record;
    int r;
    if (!u->ready)
        return 1;
    while (u->count)
    {
        record = remove_at(u, u->count - 1);
        r = s->release(s->context, record);
        if (r <= 0)
            return r;
    }
    u->done = 0;
    return 1;
}
int mm_document_undo_clear_redo(MMDocumentUndo *u, const MMDocumentUndoServices *s)
{
    void *record;
    int r;
    if (!u->ready)
        return 1;
    while (u->done < (MMI32)u->count)
    {
        record = remove_at(u, u->count - 1);
        r = s->release(s->context, record);
        if (r <= 0)
            return r;
    }
    return 1;
}
int mm_document_undo_limit(MMDocumentUndo *u, MMI32 maximum, const MMDocumentUndoServices *s)
{
    void *record;
    MMI32 end;
    int r;
    u->maximum = maximum;
    if (!u->ready || (MMI32)u->count <= maximum)
        return 1;
    end = u->done > maximum ? u->done : maximum;
    while ((MMI32)u->count > end)
    {
        record = remove_at(u, u->count - 1);
        r = s->release(s->context, record);
        if (r <= 0)
            return r;
    }
    while ((MMI32)u->count > maximum)
    {
        if (u->done != (MMI32)u->count && s->diagnostic)
            s->diagnostic(s->context);
        record = remove_at(u, 0);
        --u->done;
        r = s->release(s->context, record);
        if (r <= 0)
            return r;
    }
    if (s->validate)
        s->validate(s->context);
    return 1;
}
int mm_document_undo_dispose(MMDocumentUndo *u, const MMDocumentUndoServices *s)
{
    int r = mm_document_undo_clear(u, s);
    if (r <= 0)
        return r;
    r = result(mm_storage_dispose(&u->storage), s);
    if (r > 0)
        u->ready = 0;
    return r;
}
