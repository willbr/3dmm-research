/* Actor clipboard policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_CLIPBOARD_H
#define THREE_MM_ENGINE_ACTOR_CLIPBOARD_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorClipboard
{
    void *actor;
    int route_only;
} ThreeMMActorClipboard;
typedef struct ThreeMMActorClipboardServices
{
    void *context,*name,*prefix;
    unsigned unit;
    void *(*create)(void *);
    void (*release_clipboard)(void *,void *clipboard);
    int (*copy)(void *,void *source,int route_only,int entire,void **actor);
    void (*source_name)(void *,void *source,void *name);
    void (*copy_prefix)(void *,void *source,void *prefix);
    const void *(*text)(void *name);
    void (*assign_name)(void *clipboard,void *name);
    int (*append_space)(void *clipboard);
    int (*append_name)(void *clipboard,void *name);
    void (*name_too_long)(void *);
    void (*set)(void *clipboard,void *actor,int route_only);
    void (*release_actor)(void *,void *actor);
    int (*duplicate)(void *,void *actor,int reset,void **copy);
    int (*paste_route)(void *,void *movie,void *actor);
    int (*paste_actor)(void *,void *movie,void *actor);
    ThreeMMI32 (*actor_id)(void *actor);
    int (*rename)(void *,void *movie,ThreeMMI32 actor,void *name);
    void (*remove)(void *,void *movie,ThreeMMI32 actor);
} ThreeMMActorClipboardServices;
/* Name/prefix are borrowed scratch strings, valid for the entire creation.
 * text exposes 256 native one/two-byte units including a terminating zero.
 * Prefix length stops at its first zero; comparison is raw and case-sensitive.
 * String assignment/append preserve counted embedded zeros and native partial
 * truncation. Name operations and state assignment cannot reenter this call.
 *
 * Creation assigns the copied actor only after naming succeeds. In particular,
 * native name-overflow failure releases the empty clipboard but leaks the local
 * copied actor. This known legacy failure is preserved, not silently repaired
 * during conversion; test it with a nonallocating reference-count fixture.
 * Creation requires !route_only || !entire. copy cleans up its own failed
 * output. The controller passes reset=true to duplicate.
 * Clipboard release destroys its stored actor via dispose below.
 */
void three_mm_actor_clipboard_init(ThreeMMActorClipboard *);
void *three_mm_actor_clipboard_create(void *source,int route_only,int entire,const ThreeMMActorClipboardServices *);
void three_mm_actor_clipboard_dispose(ThreeMMActorClipboard *,const ThreeMMActorClipboardServices *);
/* Route paste returns directly. Full paste duplicates, pastes, then renames.
 * Rename failure removes the new actor before releasing the duplicate. IDs are
 * queried separately for rename and removal, retaining native callback order.
 * The stored actor/name remain borrowed and unchanged throughout paste. */
int three_mm_actor_clipboard_paste(const ThreeMMActorClipboard *,void *name,void *movie,const ThreeMMActorClipboardServices *);
#ifdef __cplusplus
}
#endif
#endif
