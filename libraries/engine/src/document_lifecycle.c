#include "mm/document_lifecycle.h"
#include <string.h>
enum
{
    START,
    DOWN,
    LEFT,
    DONE
};
void mm_document_register(MMDocumentRegistry *r, MMDocumentNode *n, void *owner, MMDocumentNode *anchor, int sibling)
{
    memset(n, 0, sizeof(*n));
    n->owner = owner;
    if (!anchor)
    {
        n->sibling = r->first;
        r->first = n;
    }
    else if (sibling)
    {
        n->parent = anchor->parent;
        n->sibling = anchor->sibling;
        anchor->sibling = n;
    }
    else
    {
        n->parent = anchor;
        n->sibling = anchor->child;
        anchor->child = n;
    }
}
int mm_document_unlink(MMDocumentRegistry *r, MMDocumentNode *n)
{
    MMDocumentNode **p = n->parent ? &n->parent->child : &r->first;
    while (*p && *p != n)
        p = &(*p)->sibling;
    if (!*p)
        return 0;
    *p = n->sibling;
    return 1;
}
void mm_document_tree_init(MMDocumentTree *t, MMDocumentNode *root)
{
    t->root = root;
    t->current = 0;
    t->state = root ? START : DONE;
}
int mm_document_tree_next(MMDocumentTree *t, MMDocumentNode **out, MMU32 *flags, MMU32 input)
{
    MMDocumentNode *next;
    *flags = 0;
    switch (t->state)
    {
    case START:
        t->current = t->root;
        *flags |= MM_DOC_TREE_ROOT;
        goto check;
    case DOWN:
        if (!(input & MM_DOC_TREE_SKIP) && (next = t->current->child) != 0)
        {
            t->current = next;
            goto check;
        }
        /* fall through */
    case LEFT:
        if (t->current == t->root)
        {
            t->state = DONE;
            return 0;
        }
        next = t->current->sibling;
        if (next)
        {
            t->current = next;
        check:
            *flags |= MM_DOC_TREE_PRE;
            if (!t->current->child)
            {
                *flags |= MM_DOC_TREE_POST;
                t->state = LEFT;
            }
            else
                t->state = DOWN;
        }
        else
        {
            t->current = t->current->parent;
            *flags |= MM_DOC_TREE_POST;
            if (t->current == t->root)
            {
                t->state = DONE;
                *flags |= MM_DOC_TREE_ROOT;
            }
            else
                t->state = LEFT;
        }
        break;
    case DONE:
        return 0;
    }
    *out = t->current;
    return 1;
}
int mm_document_internal(MMDocumentNode *n, const MMDocumentLifecycleServices *s)
{
    return n->internal ? 1 : s->clipboard(s->context, n);
}
int mm_document_release(MMDocumentNode *n, const MMDocumentLifecycleServices *s)
{
    MMDocumentNode *child;
    int r;
    if (s->drop_reference(s->context, n) > 0)
        return 1;
    if (s->view_count(s->context, n))
        s->diagnostic(s->context, MM_DOC_OPEN_VIEWS);
    if (n->freeing)
        s->diagnostic(s->context, MM_DOC_RECURSIVE_RELEASE);
    n->freeing = 1;
    r = s->dispose_undo(s->context, n);
    if (r <= 0)
        return r;
    while ((child = n->child) != 0)
    {
        r = s->close_views(s->context, child);
        if (r <= 0)
            return r;
        if (child == n->child)
        {
            s->diagnostic(s->context, MM_DOC_CHILD_RETAINED);
            r = s->release(s->context, child);
            if (r <= 0)
                return r;
        }
    }
    return s->destroy(s->context, n);
}
int mm_document_finish(MMDocumentRegistry *registry, MMDocumentNode *n, const MMDocumentLifecycleServices *s)
{
    int r = s->clipboard(s->context, n);
    if (r < 0)
        return r;
    if (r)
    {
        s->diagnostic(s->context, MM_DOC_CLIPBOARD_RELEASE);
        r = s->retain(s->context, n);
        if (r <= 0)
            return r;
        r = s->retain(s->context, n);
        if (r <= 0)
            return r;
        r = s->clear_clipboard(s->context);
        if (r <= 0)
            return r;
    }
    if (!mm_document_unlink(registry, n))
        s->diagnostic(s->context, MM_DOC_CORRUPT_TREE);
    return s->dispose_views(s->context, n);
}
int mm_document_close_views(MMDocumentNode *n, const MMDocumentLifecycleServices *s)
{
    void *view, *window;
    int r;
    if (!s->views_present(s->context, n))
        return 1;
    r = s->retain(s->context, n);
    if (r <= 0)
        return r;
    while (s->view_count(s->context, n) > 0)
    {
        view = s->view(s->context, n, 0);
        window = s->window(s->context, view);
        r = s->release_view(s->context, window ? window : view);
        if (r <= 0)
            return r;
    }
    return s->release(s->context, n);
}
int mm_document_query_close(MMDocumentNode *n, MMU32 flags, const MMDocumentLifecycleServices *s)
{
    MMDocumentTree tree;
    MMU32 visit;
    int r;
    mm_document_tree_init(&tree, n);
    while (mm_document_tree_next(&tree, &n, &visit, 0))
    {
        if (!(visit & MM_DOC_TREE_POST))
            continue;
        r = s->dirty(s->context, n);
        if (r < 0)
            return r;
        if (!r)
            continue;
        if (!(flags & MM_DOC_ASSUME_YES))
        {
            r = s->query_save(s->context, n, (flags & MM_DOC_FORCE) != 0);
            if (r < 0)
                return r;
            if (!r)
                continue;
            if (r == 2)
                return 0;
        }
        r = s->save(s->context, n);
        if (r < 0)
            return r;
        if (!r && !(flags & MM_DOC_FORCE))
            return 0;
    }
    return 1;
}
int mm_document_query_all(const MMDocumentRegistry *registry, MMU32 flags, const MMDocumentLifecycleServices *s)
{
    MMDocumentNode *n;
    int r;
    for (n = registry->first; n; n = n->sibling)
    {
        r = s->query_close(s->context, n, flags);
        if (r <= 0)
            return r;
    }
    return 1;
}
int mm_document_query_window(MMDocumentNode *n, void *closing, const MMDocumentLifecycleServices *s)
{
    MMI32 i;
    void *view, *window;
    if (!s->views_present(s->context, n) || s->view_count(s->context, n) == 0)
    {
        s->diagnostic(s->context, MM_DOC_NO_VIEWS);
        return 1;
    }
    for (i = s->view_count(s->context, n); i-- > 0;)
    {
        view = s->view(s->context, n, i);
        window = s->window(s->context, view);
        if (window && window != closing)
            return 1;
    }
    return s->query_close(s->context, n, 0);
}
int mm_document_find(const MMDocumentRegistry *registry, MMDocumentNode **out, const MMDocumentLifecycleServices *s)
{
    MMDocumentNode *n;
    int r;
    *out = 0;
    for (n = registry->first; n; n = n->sibling)
    {
        r = s->match_name(s->context, n);
        if (r < 0)
            return r;
        if (r)
        {
            *out = n;
            return 1;
        }
    }
    return 0;
}
int mm_document_save(MMDocumentNode *n, int kind, const MMDocumentLifecycleServices *s)
{
    int r = 0;
    if (kind < MM_DOC_SAVE || kind > MM_DOC_SAVE_COPY)
    {
        s->diagnostic(s->context, MM_DOC_BAD_SAVE);
        return 0;
    }
    if (kind == MM_DOC_SAVE)
    {
        r = s->current_name(s->context, n);
        if (r < 0)
            return r;
    }
    if (!r)
    {
        r = s->choose_name(s->context, n);
        if (r <= 0)
            return r;
    }
    r = s->write_name(s->context, n, kind != MM_DOC_SAVE_COPY);
    if (r < 0)
        return r;
    if (!r)
    {
        s->save_error(s->context);
        return 0;
    }
    return kind == MM_DOC_SAVE_COPY ? 1 : s->update_name(s->context, n);
}
