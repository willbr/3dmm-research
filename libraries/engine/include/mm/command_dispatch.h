/* Command queue and filter policy converted from Kauai cmd_core.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_COMMAND_DISPATCH_H
#define MM_ENGINE_COMMAND_DISPATCH_H
#include "collections.h"
#ifdef __cplusplus
extern "C" {
#endif

enum
{
    MM_COMMAND_SELF = 1,
    MM_COMMAND_UNTARGETED = 2,
    MM_COMMAND_OTHER = 4,
    MM_COMMAND_ALL = 7,
    MM_COMMAND_ARGUMENTS = 4
};

typedef struct MMQueuedCommand
{
    void *target;
    MMI32 id;
    void *group;
    MMI32 arguments[MM_COMMAND_ARGUMENTS];
} MMQueuedCommand;

typedef struct MMCommandDispatch
{
    MMCollectionStorage queue_storage, filter_storage;
    MMArray queue, filters;
    MMU32 next_filter;
    int dispatching;
} MMCommandDispatch;

typedef int (*MMCommandRelease)(void *context, void *group);

/* Stable-address owner of the queue and filters. Targets are borrowed; bury
 * them before destruction. Successful insertion transfers a group's owned
 * reference to the queue. Taking a command transfers it to the caller. Flush,
 * burial and disposal release the removed references through the supplied
 * callback. A release callback returns zero or a negative host error. */
int mm_commands_init(MMCommandDispatch *, const MMStorageAllocator *, int trash_unused);
int mm_commands_reserve(MMCommandDispatch *, MMU32 commands, MMU32 filters);
int mm_commands_dispose(MMCommandDispatch *, void *context, MMCommandRelease);
int mm_commands_insert(MMCommandDispatch *, const MMQueuedCommand *, int at_head);
int mm_commands_take(MMCommandDispatch *, MMQueuedCommand *);
int mm_commands_at(const MMCommandDispatch *, MMU32 index, MMQueuedCommand *);
int mm_commands_contains(const MMCommandDispatch *, MMI32 id);
int mm_commands_flush(MMCommandDispatch *, MMI32 id, void *context, MMCommandRelease);
int mm_commands_bury(MMCommandDispatch *, void *target, void *context, MMCommandRelease);

/* Key reads distinguish an empty queue (0) from a pending non-key (-1). */
int mm_commands_take_key(MMCommandDispatch *, MMI32 key_id, MMQueuedCommand *);
MMU32 mm_command_target_mask(const void *handler, const void *target);

typedef int (*MMCommandIdInUse)(void *context, MMI32 id);

/* last is an independent cursor, initially zero. Find a contiguous run of
 * dynamic (high-bit) IDs, restarting the run at occupied IDs and at wrap.
 * The owner registry must have an available run. The optional lookup
 * returns occupied (>0), free (0), or a negative host error. The cursor
 * advances through every probe, including on error. Return 1 on success,
 * 0 for invalid input, or the host error. Publish first only on success. */
int mm_command_id_allocate(MMU32 *last, MMU32 count, void *context, MMCommandIdInUse, MMI32 *first);

typedef struct MMCommandBinding
{
    MMI32 id;
    MMU32 mask;
} MMCommandBinding;

typedef struct MMCommandMap
{
    const struct MMCommandMap *base;
    const void *entries;
    size_t stride;
} MMCommandMap;

/* Trusted static tables. Each entry begins with MMCommandBinding, possibly
 * followed by native call data. ID zero terminates each table and can supply
 * a default. The default callback tests whether such an entry can execute.
 * Return the first compatible exact match, including explicit empty
 * bindings, or the most-derived compatible default. Result is borrowed. */
const void *mm_command_map_find(const MMCommandMap *, MMI32 id, MMU32 mask, int (*has_default)(const void *));

enum { MM_COMMAND_ENABLE_NONE, MM_COMMAND_ENABLE_EXPLICIT, MM_COMMAND_ENABLE_CALLBACK };
int mm_command_binding_enable(const MMCommandBinding *, int has_command, int has_enabler);

int mm_commands_add_filter(MMCommandDispatch *, void *handler, MMI32 level, MMU32 mask);
void mm_commands_remove_filter(MMCommandDispatch *, void *handler, MMI32 level);

typedef struct MMCommandRoute
{
    int (*prepare)(void *context);
    void *(*target)(void *context);
    int (*send)(void *context, void *handler);
    int (*finish)(void *context);
} MMCommandRoute;

/* Host keeps the currently dispatched command in its native representation.
 * Prepare returns zero for empty or positive to route the prepared command.
 * Send returns positive if handled. Handler callbacks may change the target
 * or update filters and the queue. The route reloads the target after each
 * send. Negative callback results abort and are propagated. */
int mm_commands_route(MMCommandDispatch *, void *context, const MMCommandRoute *);

typedef struct MMCommandEnable
{
    void *(*target)(void *context);
    int (*enable)(void *context, void *handler, MMU32 *flags);
} MMCommandEnable;

/* Enable queries visit all filters regardless of target mask and then the
 * target. Return positive with flags if answered, zero if unhandled, or a
 * negative host error. */
int mm_commands_enable(const MMCommandDispatch *, void *context, const MMCommandEnable *, MMU32 *flags);

#ifdef __cplusplus
}
#endif
#endif
