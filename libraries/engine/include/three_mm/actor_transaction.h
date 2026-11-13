/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_TRANSACTION_H
#define THREE_MM_ENGINE_ACTOR_TRANSACTION_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
enum
{
    THREE_MM_TRANSACTION_ACTION,THREE_MM_TRANSACTION_ADD,THREE_MM_TRANSACTION_NORMALIZE,
    THREE_MM_TRANSACTION_COSTUME,THREE_MM_TRANSACTION_REMOVE
};
enum { THREE_MM_TRANSACTION_BAD_LIFETIME,THREE_MM_TRANSACTION_SEEK_FAILED };
typedef struct ThreeMMActorTransactionServices
{
    void *context;
    int (*duplicate)(void *,void **snapshot);
    int (*apply)(void *);
    void (*restore)(void *,void *snapshot);
    int (*create_undo)(void *,void *snapshot);
    void (*release)(void *,void *snapshot);
} ThreeMMActorTransactionServices;
typedef struct ThreeMMActorTransactionResult
{
    void *snapshot,*context;
    void (*publish)(void *,void *snapshot);
} ThreeMMActorTransactionResult;
typedef struct ThreeMMActorDeleteResult
{
    int alive,trashed;
    void *context;
    void (*publish)(void *,int alive,int trashed);
} ThreeMMActorDeleteResult;
typedef struct ThreeMMActorDeleteState
{
    ThreeMMI32 frame,add_event,next_event;
    int scene_present;
} ThreeMMActorDeleteState;
typedef struct ThreeMMActorDeleteServices
{
    void *context;
    void (*read)(void *,ThreeMMActorDeleteState *);
    ThreeMMI32 (*add_frame)(void *,ThreeMMI32 event);
    ThreeMMI32 (*event_count)(void *);
    int (*delete_subroute)(void *);
    int (*seek)(void *,ThreeMMI32 frame);
    int (*delete_forward)(void *,int all,int wants_alive,ThreeMMI32 original_event);
    int (*lifetime)(void *);
    void (*sound_frame)(void *);
    void (*diagnostic)(void *,int fault);
} ThreeMMActorDeleteServices;
/* Apply is the selected C-backed core edit. Creation registers undo and resets
 * its snapshot only on success. Action alone can transfer the local snapshot
 * reference through result; failure leaves that result untouched.
 * Preserves legacy quirks: costume continues after failed duplication (snapshot
 * callbacks can receive NULL), and failed removal restores without releasing
 * the local snapshot. Those paths are not repaired by this conversion.
 * Callbacks cannot reenter this transaction. */
int three_mm_actor_transaction_apply(int kind,ThreeMMActorTransactionResult *,const ThreeMMActorTransactionServices *);
/* Enabled retail BUG1870 deletion policy. Failed deletion/seeks keep partial
 * state, report seek failure and release without restoring. Failed undo creation
 * restores but does not undo prior alive-output writes. Lifetime failure reports
 * a diagnostic and continues. Only duplication failure trashes the alive output;
 * the native observer supplies its platform's debug trash representation.
 * Core deletion/seek callbacks may change state, which is read back explicitly. */
int three_mm_actor_transaction_delete(int all,ThreeMMActorDeleteResult *,const ThreeMMActorTransactionServices *,
    const ThreeMMActorDeleteServices *);
#ifdef __cplusplus
}
#endif
#endif
