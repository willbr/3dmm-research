/* Actor clipboard policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_clipboard.h"
#include "mm/movie_edit.h"
#include "mm/scene_content.h"
#include <string.h>

static MMI32 name_count(const void *buffer, unsigned unit)
{
    return unit == 1 ? *(const unsigned char *)buffer : *(const unsigned short *)buffer;
}

static MMStringValue name_value(void *buffer, unsigned unit)
{
    MMStringValue value = {0};
    value.buffer = buffer;
    value.unit = unit;
    return value;
}

void mm_actor_clipboard_init(MMActorClipboard *c, unsigned unit)
{
    memset(c, 0, sizeof(*c));
    c->unit = unit;
}

const void *mm_actor_clipboard_name(const MMActorClipboard *c, MMI32 *count)
{
    *count = name_count(c->name, c->unit);
    return (const unsigned char *)c->name + c->unit;
}

static int copy_name(MMActorClipboard *c, void *source, const MMActorClipboardServices *s)
{
    unsigned short name[MM_STRING_MAX + 2], prefix[MM_STRING_MAX + 2];
    const unsigned char narrow_space = ' ';
    const unsigned short wide_space = ' ', zero = 0;
    const unsigned unit = c->unit;
    const MMStringValue original = name_value(name, unit);
    const MMStringValue label = name_value(prefix, unit);
    const MMStringValue stored = name_value(c->name, unit);
    const unsigned char *text = (const unsigned char *)name + unit;
    const unsigned char *start = (const unsigned char *)prefix + unit;
    const void *space = unit == 1 ? (const void *)&narrow_space : (const void *)&wide_space;
    MMI32 count, limit, length = 0;

    mm_string_value_clear(&original);
    mm_string_value_clear(&label);
    s->source_name(s->context, source, &original);
    s->copy_prefix(s->context, source, &label);
    count = name_count(name, unit);
    limit = name_count(prefix, unit);
    while (length < limit && memcmp(start + length * unit, &zero, unit))
        ++length;
    if (count >= length && !memcmp(text, start, length * unit))
    {
        mm_string_value_copy(&stored, name);
        return 1;
    }
    mm_string_value_copy(&stored, prefix);
    return mm_string_value_append(&stored, space, 1) && mm_string_value_append(&stored, text, count);
}

void *mm_actor_clipboard_create(void *source, int route_only, int entire, int postponed,
                                const MMActorCopyServices *copy, const MMActorClipboardServices *s)
{
    const MMActorOwnerServices *owner = &copy->create.owner;
    MMActorOwnerResult result = {0};
    MMActorClipboard *storage;
    void *clipboard = s->create(s->context, &storage);
    MMActorOwner *original;
    int ok;
    if (!clipboard)
        return NULL;
    original = owner->owner(owner->context, source);
    ok = route_only ? mm_actor_copy_route(original, entire, &result, copy) :
                      mm_actor_copy(original, entire, postponed, &result, copy);
    if (!ok)
    {
        s->release_clipboard(s->context, clipboard);
        return NULL;
    }
    if (!route_only && !copy_name(storage, source, s))
    {
        s->name_too_long(s->context);
        s->release_clipboard(s->context, clipboard);
        return NULL;
    }
    storage->actor = result.actor;
    storage->route_only = route_only;
    return clipboard;
}

void mm_actor_clipboard_dispose(MMActorClipboard *c, const MMActorOwnerServices *owner)
{
    if (c->actor)
        owner->release(owner->context, c->actor);
    c->actor = NULL;
}

int mm_actor_clipboard_paste(const MMActorClipboard *c, void *movie, const MMActorOwnerServices *owner,
                             const MMActorClipboardServices *s)
{
    const MMMovieEditServices *edit = s->movie_edit;
    MMActorOwnerResult result = {0};
    MMActorOwner *actor;
    const void *name;
    MMI32 count;
    int ok;
    if (c->route_only)
        return edit ? mm_movie_paste_route(c->actor, edit) : s->paste_route(s->context, movie, c->actor);
    if (!mm_actor_owner_duplicate(c->actor, 1, &result, owner))
        return 0;
    ok = edit ? mm_movie_paste_actor(result.actor, edit) : s->paste_actor(s->context, movie, result.actor);
    if (ok)
    {
        actor = owner->owner(owner->context, result.actor);
        name = mm_actor_clipboard_name(c, &count);
        ok = edit ? mm_movie_rollcall_rename(actor->runtime.actor, name, count, &edit->rollcall)
                  : s->rename(s->context, movie, actor->runtime.actor, name, count);
        if (!ok)
        {
            if (edit)
                mm_scene_actor_remove(actor->runtime.actor, edit->paste_scene);
            else
                s->remove(s->context, movie, actor->runtime.actor);
        }
    }
    owner->release(owner->context, result.actor);
    return ok != 0;
}
