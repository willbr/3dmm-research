/* Movie content transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_content.h"
int mm_movie_insert_material(void *material, MMTag *output, int *written,
                                   const MMMovieContentServices *s)
{
    void *resources;
    MMU32 number;
    *written = 0;
    if (!s->ensure(s->edit.context, &resources) || !s->write_material(material, s->file(resources), &number))
        return 0;
    output->sid = 0;
    output->cache = 0;
    output->type = 0x4d54524cU;
    output->number = number;
    *written = 1;
    return mm_tag_open(output, s->edit.data->resources, NULL, s->tags);
}
static int author(const void *name, MMI32 shape, const void *font, MMTag *tag,
                  const MMMovieContentServices *s)
{
    void *text = s->new_text(name, shape, font), *file;
    MMU32 number;
    int ok;
    if (!text)
        return 0;
    if (!s->make_valid(s->edit.context))
    {
        s->release_text(text);
        return 0;
    }
    file = s->file(s->edit.data->resources);
    ok = s->use_temp(s->edit.context) && s->write_text(text, file, &number);
    s->release_text(text);
    if (!ok)
        return 0;
    tag->sid = 0;
    tag->cache = 0;
    tag->type = 0x544d504cU;
    tag->number = number;
    return mm_tag_open(tag, s->edit.data->resources, NULL, s->tags);
}
int mm_movie_insert_text(const void *name, MMI32 shape, const void *font,
                               const MMMovieContentServices *s)
{
    MMTag tag;
    int ok;
    if (!author(name, shape, font, &tag, s))
        return 0;
    ok = mm_movie_insert_actor(&tag, &s->edit);
    mm_tag_close(&tag, s->tags);
    return ok;
}
int mm_movie_change_text(void *actor, const void *name, MMI32 shape, const void *font,
                               const MMMovieContentServices *s)
{
    MMU32 i, count;
    MMTag tag;
    MMMovieEditRequest request = {0};
    MMActorOwnerResult snapshot = {NULL, NULL, NULL};
    const MMActorOwnerServices *actors = &s->edit.actors;
    int ok = 0;
    const void *units = s->name_units(name, &count);
    for (i = 0; i < count; ++i)
        if (s->character(name, i) != ' ')
            break;
    if (i == count)
        return mm_movie_edit_actor(MM_MOVIE_EDIT_REMOVE, &request, &s->edit);
    if (!author(name, shape, font, &tag, s))
        return 0;
    if (!mm_actor_owner_duplicate(actor, 0, &snapshot, actors))
    {
        mm_tag_close(&tag, s->tags);
        return 0;
    }
    if (!s->change_template(actor, &tag))
    {
        mm_tag_close(&tag, s->tags);
        actors->release(actors->context, snapshot.actor);
        return 0;
    }
    s->edit.rollcall.set_actor_id(actor, -1);
    if (!mm_movie_rollcall_add(actor, units, count, &s->edit.rollcall))
        s->restore_template_edit(actor, snapshot.actor);
    else
    {
        mm_movie_rollcall_remove(snapshot.actor, 0, &s->edit.rollcall);
        ok = 1;
    }
    actors->release(actors->context, snapshot.actor);
    mm_tag_close(&tag, s->tags);
    if (ok)
        s->edit.dirty(s->edit.context);
    return ok;
}
int mm_movie_insert_caption(const void *bounds, int story, const MMMovieContentServices *s)
{
    void *caption = s->new_caption(s->edit.scenes->open, bounds, story);
    if (!caption)
        return 0;
    s->caption_font(caption, s->default_font(s->edit.context));
    if (!s->add_caption(s->edit.scenes->open, caption))
    {
        s->release_caption(caption);
        return 0;
    }
    s->select_caption(s->edit.scenes->open, caption);
    s->attach_caption(caption);
    s->release_caption(caption);
    return 1;
}
int mm_movie_remove_caption(int hide, const MMMovieContentServices *s)
{
    void *caption = s->selected_caption(s->edit.scenes->open);
    if (!caption)
    {
        s->missing_caption(s->edit.context);
        return 0;
    }
    return hide ? s->hide_caption(caption) : s->remove_caption(s->edit.scenes->open, caption);
}
void mm_movie_select_caption(MMI32 index, const MMMovieContentServices *s)
{
    MMI32 i = 0, visible = 0;
    void *caption;
    if (index < 0)
        return;
    while ((caption = s->caption_at(s->edit.scenes->open, i)) != 0)
    {
        if (s->visible_caption(caption) && visible++ == index)
        {
            s->select_caption(s->edit.scenes->open, caption);
            return;
        }
        ++i;
    }
}
