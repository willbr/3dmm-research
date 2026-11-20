/* Actor route paste converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_ROUTE_PASTE_H
#define MM_ENGINE_ACTOR_ROUTE_PASTE_H
#include "actor_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_ROUTE_PASTE_EMPTY,MM_ROUTE_PASTE_INSERT_FAILED,MM_ROUTE_PASTE_NO_ACTION };
typedef struct MMActorRoutePasteServices
{
    MMActorSubrouteEditServices subroute;
    int legacy_static_mode;
    MMI32 (*distance)(void *,const MMActorPoint *);
    void (*diagnostic)(void *,int fault);
} MMActorRoutePasteServices;
/* Valid independent clipboard and actor tables/cursors are required. The
 * subroute services must be bound to the actor's path and event storage. Preserves
 * deletion at the current node (not current+1), later-subroute index adjustment,
 * partial changes on failure and no internal undo. Split-point distance is
 * initialized to epsilon before the shared splitter, replacing the previously
 * uninitialized distance with deterministic moving-point treatment. It is
 * removed/overwritten before the resulting route is observed.
 * The source clipboard is unchanged. Callbacks cannot reenter this operation.
 * legacy_static_mode selects the optional STATIC build policy: query flags and
 * add floating step only for a static action; normal builds always add it.
 * Reserved point insertion failure diagnoses and continues as native AssertDo
 * did. Other insertion/step failures return immediately with preceding edits. */
int mm_actor_route_paste(MMActorOwner *, const MMActorPath *clipboard_route, const MMActorRoutePasteServices *);
#ifdef __cplusplus
}
#endif
#endif
