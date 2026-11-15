#include "mm/text_live.h"
#include <string.h>
static int group(MMU32 root, MMU32 kind, void *p, int order, int system, const MMRichTextWriteServices *s)
{
    int r = s->add_child(s->context, root, kind, s->group_bytes(s->context, kind, p));
    return r > 0 ? s->group(s->context, kind, p, order, system) : r;
}
int mm_rich_text_save(const MMRichTextState *state, MMTextLiveOutput *out, int big, int redirect,
                      const MMRichTextLiveServices *s)
{
    const MMRichTextWriteServices *w = &s->write;
    MMTextChunk created;
    MMTextLocation location;
    unsigned char marker[2];
    int r;
    created.type = created.number = 0;
    out->chunk.type = MM_TEXT_ROOT;
    out->number_changed = 0;
    r = mm_rich_text_create(&state->defaults, &state->font, &created, big, w);
    if (created.type == MM_TEXT_ROOT)
    {
        out->chunk.number = created.number;
        out->number_changed = 1;
    }
    if (r <= 0)
    {
        if (!r)
            s->error(s->context);
        return r;
    }
    r = w->add_child(w->context, created.number, MM_TEXT_BYTES, (MMU32)state->text.bytes + 2);
    if (r <= 0)
        goto fail;
    marker[big ? 1 : 0] = (unsigned char)state->text_system;
    marker[big ? 0 : 1] = (unsigned char)((MMU32)state->text_system >> 8);
    r = w->write(w->context, 0, marker, 2);
    if (r <= 0)
        goto fail;
    r = s->move_text(s->context, 2);
    if (r < 0)
        goto fail;
    if (!r)
        s->assertion(s->context, MM_TEXT_ASSERT_MOVE);
    r = s->text(s->context);
    if (r <= 0)
        goto fail;
    r = group(created.number, MM_TEXT_PROPERTIES, state->properties, 1, state->text_system, w);
    if (r <= 0)
        goto fail;
    if (state->arguments && w->count(w->context, state->arguments) > 0)
    {
        r = group(created.number, MM_TEXT_ARGUMENTS, state->arguments, 1, state->text_system, w);
        if (r <= 0)
            goto fail;
    }
    if (redirect)
    {
        memset(&location, 0, sizeof(location));
        r = s->destination(s->context, &location);
        if (r <= 0)
        {
            if (!r)
                s->assertion(s->context, MM_TEXT_ASSERT_LOCATION);
            if (location.file)
                w->objects.release(w->objects.context, MM_TEXT_BYTES, location.file);
            return r;
        }
        r = s->redirect(s->context, &location, state->text.bytes);
        if (location.file)
            w->objects.release(w->objects.context, MM_TEXT_BYTES, location.file);
        if (r < 0)
            return r;
    }
    return 1;
fail:
    w->remove(w->context, created.number);
    if (!r)
        s->error(s->context);
    return r;
}
int mm_text_box_store(MMU32 box, const MMTextBoxStoreServices *s)
{
    MMTextChunk chunk;
    int r;
    s->reset_child(s->context);
    r = s->save(s->context, &chunk);
    if (r <= 0)
        return r;
    if (chunk.type != MM_TEXT_ROOT)
        s->assert_type(s->context);
    r = s->adopt(s->context, box, &chunk);
    if (r <= 0)
    {
        s->remove(s->context, box, &chunk, 0);
        return r;
    }
    r = s->metadata(s->context, box);
    if (r <= 0)
    {
        s->remove(s->context, box, &chunk, 1);
        return r;
    }
    return 1;
}
