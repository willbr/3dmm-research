/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TRANSACTION_H
#define MM_ENGINE_ACTOR_TRANSACTION_H
#include "actor_undo.h"
#include "actor_costume_edit.h"
#include "actor_stage_edit.h"
#include "actor_transform_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_TRANSACTION_BAD_LIFETIME,MM_TRANSACTION_SEEK_FAILED };
typedef struct MMActorTransactionServices
{
    MMActorOwnerServices actors;
    MMActorUndoCreateServices undo;
} MMActorTransactionServices;
typedef struct MMActorDeleteResult
{
    int alive,trashed;
    void *context;
    void (*publish)(void *,int alive,int trashed);
} MMActorDeleteResult;
typedef struct MMActorDeleteServices
{
    void *context;
    MMActorSubrouteEditServices subroute;
    void (*sound_frame)(void *);
    void (*diagnostic)(void *,int fault);
} MMActorDeleteServices;
/* Each edit clones the actor first. Failed edits or undo registration restore
 * the actor and release the local snapshot. Sound requests playback on undo.
 * Callbacks cannot reenter. Edit services must be bound to the supplied actor. */
int mm_actor_transaction_stage(void *actor,int entrance,
    const MMActorStageEditServices *,const MMActorTransactionServices *);
int mm_actor_transaction_normalize(void *actor,MMU32 flags,
    const MMActorTransformEditServices *,const MMActorTransactionServices *);
int mm_actor_transaction_costume(void *actor,MMI32 set,const MMTag *,MMI32 costume,MMI32 custom,
    const MMActorCostumeEditServices *,const MMActorTransactionServices *);
int mm_actor_transaction_sound(void *actor,const MMTag *,MMI32 loop,MMI32 queue,int motion,
    MMI32 volume,MMI32 style,MMU32 external_child,const MMActorSoundEditServices *,
    const MMActorTransactionServices *);
/* On success, an optional snapshot output receives a local reference to the
 * registered and reset undo actor. Failure leaves the output untouched. */
int mm_actor_transaction_action(void *actor,MMI32 action,MMI32 cel,int frozen,MMU32 external_child,
    void **snapshot,const MMActorMotionServices *,const MMActorActionEditServices *,
    const MMActorTransactionServices *);
/* Enabled retail BUG1870 deletion policy. Failed deletion/seeks keep partial
 * state, report seek failure and release without restoring. Failed undo creation
 * restores but does not undo prior alive-output writes. Lifetime failure reports
 * a diagnostic and continues. Only duplication failure trashes the alive output;
 * the native observer supplies its platform's debug trash representation.
 * The same C route editor and seek controls serve deletion and stage edits.
 * Subroute services must be bound to the supplied actor. */
int mm_actor_transaction_delete(void *actor,int all,MMActorDeleteResult *,const MMActorTransactionServices *,
    const MMActorDeleteServices *);
#ifdef __cplusplus
}
#endif
#endif
