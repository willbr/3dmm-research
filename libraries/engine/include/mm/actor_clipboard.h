/* Actor clipboard policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_CLIPBOARD_H
#define MM_ENGINE_ACTOR_CLIPBOARD_H
#include "actor_copy.h"
#include "string_value.h"
#include "string_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorClipboard
{
    void *actor;
    int route_only;
    unsigned unit;
    unsigned short name[MM_STRING_MAX + 2];
} MMActorClipboard;
typedef struct MMActorClipboardServices
{
    void *context;
    void *(*create)(void *, MMActorClipboard **storage);
    void (*release_clipboard)(void *,void *clipboard);
    void (*source_name)(void *, void *source, const MMStringValue *name);
    void (*copy_prefix)(void *, void *source, const MMStringValue *prefix);
    void (*name_too_long)(void *);
    int (*paste_route)(void *,void *movie,void *actor);
    int (*paste_actor)(void *,void *movie,void *actor);
    int (*rename)(void *, void *movie, MMI32 actor, const void *name, MMI32 count);
    void (*remove)(void *,void *movie,MMI32 actor);
} MMActorClipboardServices;
/* The document factory returns initialized storage with a native code unit of
 * one or two bytes. The name callbacks fill borrowed scratch strings.
 * Prefix length stops at its first zero; comparison is raw and case-sensitive.
 * String assignment/append preserve counted embedded zeros and native partial
 * truncation. Name operations and state assignment cannot reenter this call.
 *
 * Creation assigns the copied actor only after naming succeeds. In particular,
 * native name-overflow failure releases the empty clipboard but leaks the local
 * copied actor, preserving legacy failure behavior.
 * Creation requires !route_only || !entire. The shared actor copy cleans up
 * its own failed output. Paste duplicates through the shared reset path.
 * Clipboard release destroys its stored actor via dispose below.
 */
void mm_actor_clipboard_init(MMActorClipboard *, unsigned unit);
void *mm_actor_clipboard_create(void *source, int route_only, int entire, int postponed_tweaks,
                                const MMActorCopyServices *, const MMActorClipboardServices *);
void mm_actor_clipboard_dispose(MMActorClipboard *, const MMActorOwnerServices *);
/* Borrow the stored native code units and their count, including embedded zeros. */
const void *mm_actor_clipboard_name(const MMActorClipboard *, MMI32 *count);
/* Route paste returns directly. Full paste duplicates, pastes, then renames.
 * Rename failure removes the new actor before releasing the duplicate. IDs are
 * queried separately for rename and removal, retaining native callback order.
 * The stored actor/name remain borrowed and unchanged throughout paste. */
int mm_actor_clipboard_paste(const MMActorClipboard *, void *movie, const MMActorOwnerServices *,
                             const MMActorClipboardServices *);
#ifdef __cplusplus
}
#endif
#endif
