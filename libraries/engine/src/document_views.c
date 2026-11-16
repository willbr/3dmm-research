#include "mm/document_views.h"
#include "mm/wide.h"
#include <string.h>
static MMArray array(MMDocumentViews *v)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &v->storage;
    a.width = sizeof(void *);
    a.count = v->count;
    a.free_count = -1;
    a.trash_unused = v->trash;
    return a;
}
static int result(int storage, const MMDocumentViewServices *s)
{
    int r = s->status ? s->status(s->context) : 1;
    return r < 0 ? r : storage == MM_STORAGE_OK;
}
int mm_document_views_init(MMDocumentViews *v, const MMStorageAllocator *allocator, int trash)
{
    memset(v, 0, sizeof(*v));
    v->trash = trash;
    if (mm_storage_init(&v->storage, allocator) != MM_STORAGE_OK)
        return 0;
    v->storage.min_grow[0] = 128;
    v->storage.min_grow[1] = 16;
    return 1;
}
int mm_document_views_add(MMDocumentViews *v, void *view, const MMDocumentViewServices *s)
{
    MMArray a = array(v);
    int r;
    if (!v->ready)
    {
        r = result(mm_array_reserve(&a, 1, 0), s);
        if (r <= 0)
            return r;
        v->ready = 1;
    }
    r = result(mm_array_insert(&a, a.count, &view, 1), s);
    v->count = a.count;
    if (r < 0)
        return r;
    if (s->validate)
        s->validate(s->context);
    return r;
}
void *mm_document_views_get(const MMDocumentViews *v, MMU32 index)
{
    void *view;
    if (!v->ready || index >= v->count)
        return 0;
    memcpy(&view, v->storage.buffers[0].data + index * sizeof(void *), sizeof(view));
    return view;
}
int mm_document_views_find(const MMDocumentViews *v, void *view, MMI32 *index)
{
    MMU32 i;
    for (i = 0; i < v->count; ++i)
        if (mm_document_views_get(v, i) == view)
        {
            *index = (MMI32)i;
            return 1;
        }
    return 0;
}
void mm_document_views_remove(MMDocumentViews *v, void *view, const MMDocumentViewServices *s)
{
    MMI32 index;
    MMArray a = array(v);
    if (mm_document_views_find(v, view, &index))
    {
        mm_array_delete(&a, (MMU32)index, 1);
        v->count = a.count;
    }
    if (s->validate)
        s->validate(s->context);
}
int mm_document_views_first(MMDocumentViews *v, void *view, const MMDocumentViewServices *s)
{
    MMI32 index;
    MMArray a = array(v);
    if (!mm_document_views_find(v, view, &index))
    {
        if (s->missing)
            s->missing(s->context);
        return 0;
    }
    if (index)
        mm_array_move(&a, (MMU32)index, 0);
    return 1;
}
int mm_document_views_dispose(MMDocumentViews *v, const MMDocumentViewServices *s)
{
    int r = result(mm_storage_dispose(&v->storage), s);
    if (r > 0)
    {
        v->count = 0;
        v->ready = 0;
    }
    return r;
}
int mm_document_view_active(MMDocumentNode *n, void **out, const MMDocumentViewServices *s)
{
    void *view = s->get(s->context, n, 0);
    int r;
    *out = 0;
    if (!view)
        return 0;
    r = s->active(s->context, view);
    if (r > 0)
        *out = view;
    return r;
}
int mm_document_view_activate(MMDocumentNode *n, const MMDocumentViewServices *s)
{
    MMI32 i = 0;
    void *view, *window;
    while ((view = s->get(s->context, n, i++)) != 0)
        if ((window = s->window(s->context, view)) != 0)
            return s->activate_window(s->context, window);
    return 1;
}
int mm_document_titles(MMDocumentNode *n, const MMDocumentViewServices *s)
{
    MMDocumentTree t;
    MMU32 flags;
    MMI32 i;
    void *view, *window;
    int r;
    mm_document_tree_init(&t, n);
    while (mm_document_tree_next(&t, &n, &flags, 0))
    {
        if (!(flags & MM_DOC_TREE_PRE))
            continue;
        r = s->read_name(s->context, n);
        if (r <= 0)
            return r;
        for (i = 0; (view = s->get(s->context, n, i++)) != 0;)
            if ((window = s->window(s->context, view)) != 0)
            {
                r = s->set_title(s->context, window);
                if (r <= 0)
                    return r;
            }
    }
    return 1;
}
MMI32 mm_document_untitled(MMDocumentRegistry *registry, MMDocumentNode *n)
{
    if (!n->untitled)
    {
        registry->untitled = mm_signed_word((MMU32)registry->untitled + 1);
        n->untitled = registry->untitled;
    }
    return n->untitled;
}
int mm_document_name(MMDocumentRegistry *registry, MMDocumentNode *n, const MMDocumentViewServices *s)
{
    int r = s->clipboard(s->context, n);
    if (r < 0)
        return r;
    if (r)
        return s->emit_name(s->context, MM_DOC_NAME_CLIPBOARD, 0);
    r = s->current_name(s->context, n);
    if (r < 0)
        return r;
    return s->emit_name(s->context, r ? MM_DOC_NAME_FILE : MM_DOC_NAME_UNTITLED,
                        r ? 0 : mm_document_untitled(registry, n));
}
