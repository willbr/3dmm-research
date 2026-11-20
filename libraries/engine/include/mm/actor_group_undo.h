/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_GROUP_UNDO_H
#define MM_ENGINE_ACTOR_GROUP_UNDO_H
#include "actor_motion.h"
#include "movie_undo.h"
#include "rollcall_storage.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorMoveGroup
{
    MMMovieUndoContext *base;
    MMCollectionStorage storage;
    MMU32 count;
    int trash_unused;
} MMActorMoveGroup;

typedef struct MMActorMoveGroupServices
{
    void *context;
    void (*retain)(void *, void *child);
    void (*release)(void *, void *child);
    void (*child_context)(void *, void *child, void *movie, MMI32 scene, MMI32 frame);
    int (*undo_child)(void *, void *child, void *document);
} MMActorMoveGroupServices;

typedef struct MMActorRenameGroup
{
    MMMovieUndoContext *base;
    MMRollCallTable names;
} MMActorRenameGroup;

typedef struct MMActorRenameGroupServices
{
    void *context;
    int (*find_name)(void *, void *movie, MMI32 actor, const void **text, MMU32 *count);
    int (*rename)(void *, void *movie, MMI32 actor, const void *text, MMU32 count);
} MMActorRenameGroupServices;

/* The groups own their buffers and borrow the initialized base context. Init
 * does not allocate. Move groups also own one reference per non-NULL child.
 * Storage callbacks and reference callbacks cannot reenter group operations. */
int mm_actor_move_group_init(MMActorMoveGroup *, MMMovieUndoContext *,
                            const MMStorageAllocator *, int trash_unused);
void *mm_actor_move_group_child(const MMActorMoveGroup *, MMU32 index);
int mm_actor_move_group_add(MMActorMoveGroup *, void *child, const MMActorMoveGroupServices *);
void mm_actor_move_group_dispose(MMActorMoveGroup *, const MMActorMoveGroupServices *);
/* Reread the count each iteration and skip NULL rows. A failed child undo may
 * destroy the group. Return before accessing its state again. */
int mm_actor_move_group_toggle(const MMActorMoveGroup *, void *document,
                              const MMActorMoveGroupServices *);

struct MMActorOwnerServices;
struct MMActorUndoState;
struct MMSceneSelectionServices;
typedef struct MMActorDragUndo
{
    void *single, *group;
    MMActorPoint pivot;
} MMActorDragUndo;

typedef struct MMActorDragUndoServices
{
    void *context;
    MMActorMoveGroupServices composite;
    struct MMActorUndoState *(*create_actor)(void *, void **undo);
    MMActorMoveGroup *(*create_group)(void *, void **undo);
    int (*commit)(void *, void *undo);
    void (*release)(void *, void *undo);
    void (*clear)(void *);
    void (*not_undoable)(void *);
} MMActorDragUndoServices;

/* Start with an empty zero-initialized record. Selection is optional for a
 * single-actor drag. For a group, optionally freeze its visible centroid,
 * falling back to the origin. Publish the undo only after every snapshot has
 * been captured. Failed capture releases the completed prefix and clears undo.
 * Commit releases only the selected pending handle. Discard releases both. */
void mm_actor_drag_undo_begin(MMActorDragUndo *, void *actor, int freeze_pivot,
                             const struct MMSceneSelectionServices *,
                             const struct MMActorOwnerServices *, const MMActorDragUndoServices *);
void mm_actor_drag_undo_commit(MMActorDragUndo *, int single_only, const MMActorDragUndoServices *);
void mm_actor_drag_undo_discard(MMActorDragUndo *, const MMActorDragUndoServices *);
/* Capture and register the primary actor at the start of Sooner/Later. The
 * same undo holds all shifts in this interaction. On failure clear history,
 * report the edit as non-undoable and release any local handle. */
void mm_actor_time_undo_begin(void *actor, MMI32 frame, const struct MMActorOwnerServices *,
                             const MMActorDragUndoServices *);

int mm_actor_rename_group_init(MMActorRenameGroup *, MMMovieUndoContext *, unsigned unit,
                              int encoding, const MMStorageAllocator *, int trash_unused);
int mm_actor_rename_group_add(MMActorRenameGroup *, MMI32 actor, const void *text, MMU32 count);
void mm_actor_rename_group_dispose(MMActorRenameGroup *);
/* Borrowed roll-call names expire on mutation. Copy the current name before
 * renaming. Skip absent actors, stop on failed rename and preserve the old
 * entry when saving the exchanged name runs out of memory. */
int mm_actor_rename_group_toggle(MMActorRenameGroup *, const MMActorRenameGroupServices *);
#ifdef __cplusplus
}
#endif
#endif
