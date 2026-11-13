/* Actor route paste converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_ROUTE_PASTE_H
#define THREE_MM_ENGINE_ACTOR_ROUTE_PASTE_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_ROUTE_PASTE_EMPTY,THREE_MM_ROUTE_PASTE_INSERT_FAILED,THREE_MM_ROUTE_PASTE_NO_ACTION };
typedef struct ThreeMMActorRoutePasteServices
{
    ThreeMMActorSubrouteEditServices subroute;
    int (*reserve_points)(void *route,ThreeMMU32 count);
    ThreeMMI32 (*distance)(void *,const ThreeMMActorPoint *);
    void (*diagnostic)(void *,int fault);
} ThreeMMActorRoutePasteServices;
/* Valid independent clipboard and actor tables/cursors are required. Preserves
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
int three_mm_actor_route_paste(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,void *clipboard_route,int legacy_static_mode,const ThreeMMActorMotionServices *,
    const ThreeMMActorRoutePasteServices *);
#ifdef __cplusplus
}
#endif
#endif
