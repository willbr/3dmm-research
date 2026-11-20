/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TRANSACTION_H
#define MM_ENGINE_ACTOR_TRANSACTION_H
#include "actor_undo.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_TRANSACTION_BAD_LIFETIME,MM_TRANSACTION_SEEK_FAILED };
typedef struct MMActorTransactionServices
{
    void *context;
    int (*apply)(void *);
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
/* Clone before applying the edit. Failure to apply or register undo restores
 * the actor and releases the local snapshot. Sound requests playback on undo.
 * Callbacks cannot reenter. */
int mm_actor_transaction_apply(void *actor,int sound,const MMActorTransactionServices *);
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
