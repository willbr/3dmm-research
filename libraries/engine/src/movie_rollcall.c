/* Movie roll-call editing converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_rollcall.h"
#include "mm/wide.h"
MMTag mm_movie_rollcall_tag(const MMRollCallActor *row)
{
    MMTag tag;
    tag.sid = row->data.source_id;
    tag.cache = row->cache;
    tag.type = row->data.template_type;
    tag.number = row->data.template_number;
    return tag;
}
static void set_tag(MMRollCallActor *row, const MMTag *tag)
{
    row->data.source_id = tag->sid;
    row->cache = tag->cache;
    row->data.template_type = tag->type;
    row->data.template_number = tag->number;
}
MMI32 mm_movie_rollcall_find(MMI32 id, MMRollCallActor *row, const MMMovieRollCallServices *s)
{
    MMI32 i;
    for (i = 0; i < s->count(s->data->actors); ++i)
    {
        s->row(s->data->actors, i, row);
        if (row->data.actor_id == id)
            return i;
    }
    return -1;
}
int mm_movie_rollcall_get(MMI32 index, MMI32 *id, void *name, MMI32 *refs, MMTag *tag,
                                const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    if (index < 0 || index >= s->count(s->data->actors))
        return 0;
    s->name(s->data->actors, index, name);
    s->row(s->data->actors, index, &row);
    *id = row.data.actor_id;
    *refs = row.data.reference_count;
    if (tag)
        *tag = mm_movie_rollcall_tag(&row);
    return 1;
}
int mm_movie_rollcall_name(MMI32 id, void *name, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    MMI32 index = mm_movie_rollcall_find(id, &row, s);
    if (index == -1)
        return 0;
    s->name(s->data->actors, index, name);
    return 1;
}
int mm_movie_rollcall_rename(MMI32 id, const void *name, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    MMI32 index = mm_movie_rollcall_find(id, &row, s);
    if (index == -1 || !s->rename(s->data->actors, index, name))
        return 0;
    s->changed(s->context);
    return 1;
}
int mm_movie_rollcall_has(MMI32 index, MMU32 flag, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    s->row(s->data->actors, index, &row);
    return (row.data.browser_flags & flag) != 0;
}
void mm_movie_rollcall_change_tag(MMI32 id, const MMTag *tag, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    MMI32 index = mm_movie_rollcall_find(id, &row, s);
    if (index == -1)
    {
        s->diagnostic(s->context, MM_ROLLCALL_NO_ACTOR);
        return;
    }
    set_tag(&row, tag);
    s->put(s->data->actors, index, &row);
}
int mm_movie_rollcall_add(void *actor, const void *name, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    MMTag old, fresh;
    MMI32 index, id = s->actor_id(actor);
    if (id != -1)
    {
        index = mm_movie_rollcall_find(id, &row, s);
        if (index != -1)
        {
            row.data.reference_count = mm_signed_word((MMU32)row.data.reference_count + 1U);
            s->actor_tag(actor, &fresh);
            old = mm_movie_rollcall_tag(&row);
            if (!s->same_tag(&old, &fresh))
            {
                s->close_tag(&old);
                set_tag(&row, &fresh);
                s->duplicate_tag(&fresh);
            }
            s->put(s->data->actors, index, &row);
            s->changed(s->context);
            return 1;
        }
    }
    else
    {
        id = s->data->actorIdLimit;
        s->data->actorIdLimit = mm_signed_word((MMU32)id + 1U);
        s->set_actor_id(actor, id);
    }
    row.data.actor_id = s->actor_id(actor);
    row.data.browser_flags = 0;
    if (s->is_prop(actor))
    {
        row.data.browser_flags |= MM_MOVIE_PROP;
        if (s->is_text(actor))
            row.data.browser_flags |= MM_MOVIE_TEXT;
    }
    row.data.reference_count = 1;
    s->actor_tag(actor, &fresh);
    s->open_tag(s->context, &fresh, s->data->resources);
    set_tag(&row, &fresh);
    if (!s->append(s->data->actors, name, &row))
    {
        s->close_tag(&fresh);
        return 0;
    }
    s->changed(s->context);
    return 1;
}
void mm_movie_rollcall_remove(void *actor, int delete_last, const MMMovieRollCallServices *s)
{
    MMRollCallActor row;
    MMTag tag;
    MMI32 index = mm_movie_rollcall_find(s->actor_id(actor), &row, s);
    if (index == -1)
    {
        s->diagnostic(s->context, MM_ROLLCALL_BAD_REMOVE);
        return;
    }
    row.data.reference_count = mm_signed_word((MMU32)row.data.reference_count - 1U);
    if (row.data.reference_count < 0)
        s->diagnostic(s->context, MM_ROLLCALL_UNDERFLOW);
    if (delete_last && row.data.reference_count == 0)
    {
        tag = mm_movie_rollcall_tag(&row);
        s->close_tag(&tag);
        s->erase(s->data->actors, index);
    }
    else
        s->put(s->data->actors, index, &row);
    if (row.data.reference_count == 0)
        s->changed(s->context);
}
