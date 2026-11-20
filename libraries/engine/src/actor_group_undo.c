/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_group_undo.h"
#include "mm/movie_title.h"
#include <string.h>

int mm_actor_move_group_init(MMActorMoveGroup *group, MMMovieUndoContext *base,
                            const MMStorageAllocator *allocator, int trash_unused)
{
    if (mm_storage_init(&group->storage, allocator) != MM_STORAGE_OK)
        return 0;
    group->base = base;
    group->count = 0;
    group->trash_unused = trash_unused;
    group->storage.min_grow[0] = 128;
    return 1;
}

void *mm_actor_move_group_child(const MMActorMoveGroup *group, MMU32 index)
{
    void *child;
    if (index >= group->count)
        return NULL;
    memcpy(&child, group->storage.buffers[0].data + (size_t)index * sizeof(child), sizeof(child));
    return child;
}

int mm_actor_move_group_add(MMActorMoveGroup *group, void *child, const MMActorMoveGroupServices *s)
{
    MMArray array = {NULL, sizeof(child), 0, -1, 0, 0};
    array.storage = &group->storage;
    array.count = group->count;
    array.trash_unused = group->trash_unused;
    if (child)
        s->retain(s->context, child);
    if (mm_array_add(&array, &child, NULL) != MM_STORAGE_OK)
    {
        if (child)
            s->release(s->context, child);
        return 0;
    }
    group->count = array.count;
    return 1;
}

void mm_actor_move_group_dispose(MMActorMoveGroup *group, const MMActorMoveGroupServices *s)
{
    MMU32 i;
    void *child;
    for (i = 0; i < group->count; ++i)
    {
        child = mm_actor_move_group_child(group, i);
        if (child)
            s->release(s->context, child);
    }
    mm_storage_dispose(&group->storage);
    group->count = 0;
}

int mm_actor_move_group_toggle(const MMActorMoveGroup *group, void *document,
                              const MMActorMoveGroupServices *s)
{
    const MMMovieUndoContext base = *group->base;
    MMU32 i;
    void *child;
    for (i = 0; i < group->count; ++i)
    {
        child = mm_actor_move_group_child(group, i);
        if (!child)
            continue;
        s->child_context(s->context, child, base.movie, base.scene, base.frame);
        if (!s->undo_child(s->context, child, document))
            return 0;
    }
    return 1;
}

int mm_actor_rename_group_init(MMActorRenameGroup *group, MMMovieUndoContext *base, unsigned unit,
                             int encoding, const MMStorageAllocator *allocator, int trash_unused)
{
    if (mm_rollcall_table_init(&group->names, sizeof(MMI32), unit, encoding, trash_unused,
                               allocator) != MM_STORAGE_OK)
        return 0;
    group->base = base;
    return 1;
}

int mm_actor_rename_group_add(MMActorRenameGroup *group, MMI32 actor, const void *text, MMU32 count)
{
    return mm_rollcall_table_add(&group->names, text, count, &actor);
}

void mm_actor_rename_group_dispose(MMActorRenameGroup *group)
{
    mm_rollcall_table_dispose(&group->names);
}

int mm_actor_rename_group_toggle(MMActorRenameGroup *group, const MMActorRenameGroupServices *s)
{
    void *movie = group->base->movie;
    MMRollCallTable *names = &group->names;
    MMU32 i, count = names->count;
    MMMovieTitle current;
    mm_movie_title_init(&current, names->unit);
    for (i = 0; i < count; ++i)
    {
        MMI32 actor;
        const void *stored, *text;
        MMU32 stored_count, text_count;
        if (!mm_rollcall_table_extra(names, i, &actor) ||
            !mm_rollcall_table_name(names, i, &stored, &stored_count))
            return 0;
        if (!s->find_name(s->context, movie, actor, &text, &text_count))
            continue;
        if (!mm_movie_title_set(&current, text, text_count) ||
            !s->rename(s->context, movie, actor, stored, stored_count))
            return 0;
        mm_rollcall_table_rename(names, i, mm_movie_title_text(&current), current.count);
    }
    return 1;
}
