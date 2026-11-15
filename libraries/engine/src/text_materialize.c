#include "mm/text_materialize.h"
static MMU32 swap(MMU32 v)
{
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}
int mm_text_materialize(const MMTextDocumentConfig *config, MMU32 type, MMU32 number, int copy,
                        const MMTextMaterializeServices *s)
{
    MMRichTextState stored;
    MMTextLocation location;
    void *original, *group, *stream;
    MMI32 i, n;
    MMU32 fixed;
    unsigned char *bytes, b;
    int result;
    result = s->init_base(s->context);
    if (result <= 0)
        return result;
    mm_rich_text_init(&stored, config->unit, config->system);
    result = mm_rich_text_read(&stored, type, number, config->system, config->big, &s->read);
    if (result <= 0)
        goto done;
    result = s->defaults(s->context, &stored.defaults, &stored.font);
    if (result <= 0)
        goto done;
    location = stored.text;
    original = location.file;
    s->base.objects.retain(s->base.objects.context, MM_TEXT_OWNER_FILE, original);
    result = s->base.translate(s->base.context, &location, stored.text_system);
    if (result > 0)
    {
        stream = s->base.objects.get(s->base.objects.context, MM_TEXT_OWNER_STREAM);
        result = s->replace(s->context, &location, copy && original == location.file,
                            s->base.stream_bytes(s->base.context, stream) / (MMI32)config->unit - 1);
    }
    if (location.file)
        s->base.objects.release(s->base.objects.context, MM_TEXT_OWNER_FILE, location.file);
    if (result <= 0)
        goto done;
    group = stored.properties;
    s->base.objects.set(s->base.objects.context, MM_TEXT_OWNER_PROPERTIES, group);
    stored.properties = 0;
    if (stored.property_order == 256)
    {
        n = s->count(s->context, MM_TEXT_PROPERTIES, group);
        if (n)
        {
            bytes = (unsigned char *)s->properties(s->context, group);
            for (i = 0; i < n; ++i, bytes += 8)
            {
                b = bytes[0];
                bytes[0] = bytes[3];
                bytes[3] = b;
                b = bytes[1];
                bytes[1] = bytes[2];
                bytes[2] = b;
                b = bytes[4];
                bytes[4] = bytes[7];
                bytes[7] = b;
                b = bytes[5];
                bytes[5] = bytes[6];
                bytes[6] = b;
            }
        }
    }
    group = stored.arguments;
    if (group)
    {
        s->base.objects.set(s->base.objects.context, MM_TEXT_OWNER_ARGUMENTS, group);
        stored.arguments = 0;
        for (i = s->count(s->context, MM_TEXT_ARGUMENTS, group); i-- > 0;)
        {
            if (s->free_argument(s->context, group, i))
                continue;
            fixed = s->fixed(s->context, group, i);
            if (stored.argument_order == 256)
            {
                fixed = swap(fixed);
                s->put_fixed(s->context, group, i, fixed);
            }
            result =
                s->open_argument(s->context, i, (unsigned)(fixed >> 24), stored.argument_order, stored.argument_system);
            if (result <= 0)
                goto done;
        }
    }
    result = 1;
done:
    mm_rich_text_dispose(&stored, &s->read.objects);
    return result;
}
int mm_text_open_font_argument(unsigned unit, MMI32 index, unsigned kind, const MMTextFontArgumentServices *s)
{
    MMI32 bytes, font;
    MMMovieTitle name;
    const unsigned char *data;
    int result, system;
    bytes = s->bytes(s->context, index);
    if (kind != 2)
        return 0;
    if (bytes < 6)
    {
        s->diagnostic(s->context);
        return 0;
    }
    system = s->system(s->context, index);
    data = (const unsigned char *)s->lock(s->context, index);
    mm_movie_title_init(&name, unit);
    result = s->decode(s->context, data + 6, bytes - 6, &name);
    if (!result)
        s->diagnostic(s->context);
    s->unlock(s->context);
    if (result <= 0)
        return result;
    result = s->map(s->context, &name, system, &font);
    if (result > 0)
        s->put(s->context, index, font);
    return result;
}
