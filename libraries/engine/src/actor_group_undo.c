/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_group_undo.h"
#include "mm/actor_owner.h"
#include "mm/actor_undo.h"
#include "mm/movie_title.h"
#include "mm/scene_selection.h"
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

static MMActorUndoState *capture_actor(void *actor, void **undo, const MMActorOwnerServices *actors,
                                       const MMActorDragUndoServices *s)
{
    MMActorOwnerResult copy = {NULL, NULL, NULL};
    MMActorUndoState *state;
    *undo = NULL;
    if (!actor)
        return NULL;
    state = s->create_actor(s->context, undo);
    if (!state || !mm_actor_owner_duplicate(actor, 1, &copy, actors))
        return NULL;
    state->actor = copy.actor;
    state->actor_id = actors->owner(actors->context, actor)->runtime.actor;
    return state;
}

static void *capture_move(void *actor, const MMActorOwnerServices *actors, const MMActorDragUndoServices *s)
{
    void *undo;
    if (capture_actor(actor, &undo, actors, s))
        return undo;
    if (undo)
        s->release(s->context, undo);
    return NULL;
}

void mm_actor_time_undo_begin(void *actor, MMI32 frame, const MMActorOwnerServices *actors,
                             const MMActorDragUndoServices *s)
{
    void *undo;
    MMActorUndoState *state = capture_actor(actor, &undo, actors, s);
    if (state)
    {
        state->sooner_later = 1;
        state->last_frame = frame;
    }
    if (!state || !s->commit(s->context, undo))
    {
        s->clear(s->context);
        s->not_undoable(s->context);
    }
    if (undo)
        s->release(s->context, undo);
}

static void *capture_group(MMI32 count, const MMSceneSelectionServices *selection,
                           const MMActorOwnerServices *actors, const MMActorDragUndoServices *s)
{
    void *undo, *child;
    MMActorMoveGroup *group = s->create_group(s->context, &undo);
    MMI32 i;
    int added;
    if (!group)
        return NULL;
    for (i = 0; i < count; ++i)
    {
        child = capture_move(mm_scene_selection_at(i, selection), actors, s);
        if (!child)
            break;
        added = mm_actor_move_group_add(group, child, &s->composite);
        s->release(s->context, child);
        if (!added)
            break;
    }
    if (i == count)
        return undo;
    s->release(s->context, undo);
    return NULL;
}

void mm_actor_drag_undo_begin(MMActorDragUndo *drag, void *actor, int freeze_pivot,
                             const MMSceneSelectionServices *selection,
                             const MMActorOwnerServices *actors, const MMActorDragUndoServices *s)
{
    MMI32 count = selection ? mm_scene_selection_count(selection) : 1;
    void **slot = count >= 2 ? &drag->group : &drag->single;
    if (count >= 2)
    {
        if (freeze_pivot)
        {
            memset(&drag->pivot, 0, sizeof(drag->pivot));
            mm_scene_selection_centroid(&drag->pivot, selection);
        }
        *slot = capture_group(count, selection, actors, s);
    }
    else
        *slot = capture_move(actor, actors, s);
    if (!*slot)
    {
        s->clear(s->context);
        s->not_undoable(s->context);
    }
}

static void release_pending(void **slot, const MMActorDragUndoServices *s)
{
    if (*slot)
    {
        s->release(s->context, *slot);
        *slot = NULL;
    }
}

void mm_actor_drag_undo_commit(MMActorDragUndo *drag, int single_only, const MMActorDragUndoServices *s)
{
    void **slot = !single_only && drag->group ? &drag->group : &drag->single;
    if (!*slot)
        return;
    if (!s->commit(s->context, *slot))
    {
        s->not_undoable(s->context);
        s->clear(s->context);
    }
    release_pending(slot, s);
}

void mm_actor_drag_undo_discard(MMActorDragUndo *drag, const MMActorDragUndoServices *s)
{
    release_pending(&drag->single, s);
    release_pending(&drag->group, s);
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
