/* Movie content transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_content.h"
int mm_movie_insert_material(void *material, MMTag *output, int *written,
                                   const MMMovieContentServices *s)
{
    void *resources;
    MMU32 number;
    *written = 0;
    if (!s->ensure(s->edit.context, &resources) || !s->write_material(material, resources, &number))
        return 0;
    output->sid = 0;
    output->cache = 0;
    output->type = 0x4d54524cU;
    output->number = number;
    *written = 1;
    return mm_tag_open(output, s->edit.data->resources, NULL, s->tags);
}
static int author(const MMStringValue *name, MMI32 shape, const MMTag *font, MMTag *tag,
                  const MMMovieContentServices *s)
{
    MMTemplateOwner *text;
    MMResourceCache *resources;
    MMArchiveWriter writer;
    MMU32 number;
    int ok = 0;
    if (!font)
        return 0;
    text = mm_text_create(s->text_host, name, shape, font, NULL, &s->text_generation);
    if (!text)
        return 0;
    if (s->make_valid(s->edit.context))
    {
        resources = s->edit.data->resources;
        if (s->use_temp(s->edit.context))
        {
            writer = mm_resource_cache_writer(resources);
            ok = mm_text_write_owner(text, MM_TMPL, &number, &writer);
        }
    }
    mm_template_owner_release(text);
    if (!ok)
        return 0;
    tag->sid = 0;
    tag->cache = 0;
    tag->type = MM_TMPL;
    tag->number = number;
    return mm_tag_open(tag, s->edit.data->resources, NULL, s->tags);
}
int mm_movie_insert_text(const MMStringValue *name, MMI32 shape, const MMTag *font,
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
int mm_movie_change_text(void *actor, const MMStringValue *name, MMI32 shape, const MMTag *font,
                               const MMMovieContentServices *s)
{
    MMU32 i, character, count;
    MMTag tag;
    MMMovieEditRequest request = {0};
    MMActorOwnerResult snapshot = {NULL, NULL, NULL};
    const MMActorOwnerServices *actors = &s->edit.actors;
    int ok = 0;
    const unsigned char *units = (const unsigned char *)name->buffer + name->unit;
    count = name->unit == 1 ? *(const unsigned char *)name->buffer : *(const unsigned short *)name->buffer;
    for (i = 0; i < count; ++i)
    {
        character = name->unit == 1 ? units[i] : ((const unsigned short *)units)[i];
        if ((unsigned char)character != ' ')
            break;
    }
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
